#include "pch.h"

#include "Engine/Network/EngineNetSecurity.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Network/OpenSsl/OpenSslNetSecurityProvider.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "EngineNetSecurity" );

    namespace
    {
        struct EngineNetSecurityInternal
        {
            static constexpr int32 kDevCertificateValidDays = 825; ///< 브라우저 상한과 같은 값 — 개발 PC 에서 2 년 남짓 쓰고 지워 다시 만든다

            /** @brief 상대 경로를 프로젝트 루트 기준으로 펼칩니다(서버 설정과 같은 기준). 루트를 모르면(시험) 그대로. */
            static string makeProjectPath( string_view path )
            {
                const string& projectFolder = ResourceUtil::getProjectFolderPath();
                if ( path.empty() || projectFolder.empty() || FileUtil::isAbsolutePath( path ) )
                    return string( path );
                return FileUtil::joinPath( projectFolder, path );
            }

            [[nodiscard]] static bool readPem( const string& fileName, string& outText, string& outError )
            {
                if ( FileUtil::readTextFile( fileName, outText ) )
                    return true;
                outError = "cannot read " + fileName;
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    INetSecurityProvider& EngineNetSecurity::getProvider()
    {
        static OpenSslNetSecurityProvider s_provider; // Engine 안 — 모듈 리로드에 살아남는다
        return s_provider;
    }

    bool EngineNetSecurity::ensureDevCertificate( string& outCertificateFile, string& outPrivateKeyFile, string& outError )
    {
#if defined( SW_SHIPPING )
        (void)outCertificateFile;
        (void)outPrivateKeyFile;
        outError = "development certificates are not available in Shipping - configure the TLS certificate and private key files";
        return false;
#else
        const string& projectFolder = ResourceUtil::getProjectFolderPath();
        const string  rootFolder    = projectFolder.empty() ? FileUtil::getCurrentPath() : projectFolder;
        const string  folder        = FileUtil::joinPath( FileUtil::joinPath( rootFolder, path::kSavedFolder ), kDevCertificateFolder );
        outCertificateFile          = FileUtil::joinPath( folder, kDevCertificateFile );
        outPrivateKeyFile           = FileUtil::joinPath( folder, kDevPrivateKeyFile );
        if ( FileUtil::isRegularFile( outCertificateFile ) && FileUtil::isRegularFile( outPrivateKeyFile ) )
            return true;
        string     certificatePem;
        string     privateKeyPem;
        const bool bCreated = FileUtil::ensureDirectoryExists( folder ) &&
                              getProvider().createSelfSignedCertificate( "localhost", EngineNetSecurityInternal::kDevCertificateValidDays, certificatePem, privateKeyPem ) &&
                              FileUtil::writeTextFile( outCertificateFile, certificatePem ) && FileUtil::writeTextFile( outPrivateKeyFile, privateKeyPem );
        if ( bCreated == false )
        {
            outError = "cannot create the development certificate in " + folder;
            return false;
        }
        SW_LOG_INFO( "Created a development TLS certificate: %#", outCertificateFile.c_str() );
        return true;
#endif
    }

    unique_ptr<ITlsContext> EngineNetSecurity::createServerTlsContext( string_view certificateFile, string_view privateKeyFile, string_view privateKeyPassphrase,
                                                                       string& outError )
    {
        using Internal         = EngineNetSecurityInternal;
        string certificatePath = Internal::makeProjectPath( certificateFile );
        string privateKeyPath  = Internal::makeProjectPath( privateKeyFile );
        if ( certificatePath.empty() != privateKeyPath.empty() )
        {
            outError = "the TLS certificate and private key files must be configured together";
            return nullptr;
        }
        if ( certificatePath.empty() && ensureDevCertificate( certificatePath, privateKeyPath, outError ) == false )
            return nullptr;
        TlsContextSettings settings;
        settings._role                 = TlsRole::Server;
        settings._privateKeyPassphrase = string( privateKeyPassphrase );
        if ( Internal::readPem( certificatePath, settings._certificatePem, outError ) == false ||
             Internal::readPem( privateKeyPath, settings._privateKeyPem, outError ) == false )
            return nullptr;
        unique_ptr<ITlsContext> context = getProvider().createTlsContext( settings, outError );
        settings._privateKeyPassphrase.assign( settings._privateKeyPassphrase.size(), '\0' );
        return context;
    }

    unique_ptr<ITlsContext> EngineNetSecurity::createClientTlsContext( string_view trustFile, string_view serverName, string& outError )
    {
        using Internal = EngineNetSecurityInternal;
        TlsContextSettings settings;
        settings._role       = TlsRole::Client;
        settings._serverName = string( serverName );
        string trustPath     = Internal::makeProjectPath( trustFile );
        if ( trustPath.empty() )
        {
            string privateKeyPath;
            if ( ensureDevCertificate( trustPath, privateKeyPath, outError ) == false || Internal::readPem( trustPath, settings._trustPem, outError ) == false )
                return nullptr;
            if ( getProvider().computeCertificateSha256( settings._trustPem, settings._pinnedCertificateSha256Hex ) == false )
            {
                outError = "cannot compute the SHA-256 of the development certificate";
                return nullptr;
            }
        }
        else if ( Internal::readPem( trustPath, settings._trustPem, outError ) == false )
        {
            return nullptr;
        }
        return getProvider().createTlsContext( settings, outError );
    }
} // namespace sw
