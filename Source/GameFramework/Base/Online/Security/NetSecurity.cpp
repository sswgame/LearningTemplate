#include "pch.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/FileSystem/Pack/PackPatchTrustSource.h"

#include "GameFramework/Base/Online/Security/OpenSSL/OpenSSLNetSecurityProvider.h"

namespace sw
{
    SW_LOG_CALLER( "NetSecurity" );

    namespace
    {
        struct NetSecurityInternal
        {
#if !defined( SW_SHIPPING )
            static constexpr int32 kDevCertificateValidDays = 825; ///< 브라우저 상한과 같은 값 — 개발 PC 에서 2 년 남짓 쓰고 지워 다시 만든다
#endif

            [[nodiscard]] static bool readPem( const string& fileName, string& outText, string& outError )
            {
                if ( FileUtil::readTextFile( fileName, outText ) )
                    return true;
                outError = "cannot read " + fileName;
                return false;
            }
        };

        /**
         * @brief GameFramework 가 올라와 있는 동안 패치 서명 신뢰 설정의 제공자 함수 칸(`PackPatchTrustSource`)을 채웁니다. 모듈 라이브러리가 올라올 때(정적 초기화)
         *        채우고 내려갈 때 자기 함수만 비운다 - Engine 은 OpenSSL 을 링크하지 않으므로 칸으로만 제공자를 받는다.
         */
        struct PackPatchProviderRegistrar
        {
            PackPatchProviderRegistrar() { PackPatchTrustSource::setProviderGetter( &NetSecurity::getProvider ); }
            ~PackPatchProviderRegistrar() { PackPatchTrustSource::clearProviderGetter( &NetSecurity::getProvider ); }
        };

        const PackPatchProviderRegistrar s_packPatchProviderRegistrar;
    } // namespace
} // namespace sw

namespace sw
{
    string NetSecurity::makeProjectPath( string_view path )
    {
        const string& projectFolder = AssetPaths::getProjectFolderPath();
        const bool    bKeepPathAsIs = path.empty() || projectFolder.empty() || FileUtil::isAbsolutePath( path );
        if ( bKeepPathAsIs )
            return string( path );
        return FileUtil::joinPath( projectFolder, path );
    }

    INetSecurityProvider& NetSecurity::getProvider()
    {
        static OpenSSLNetSecurityProvider s_provider; // GameFramework 안 — 게임 · 키트 모듈 리로드에 살아남는다
        return s_provider;
    }

    bool NetSecurity::ensureDevCertificate( string& outCertificateFile, string& outPrivateKeyFile, string& outError )
    {
#if defined( SW_SHIPPING )
        (void)outCertificateFile;
        (void)outPrivateKeyFile;
        outError = "development certificates are not available in Shipping - configure the TLS certificate and private key files";
        return false;
#else
        const string& projectFolder = AssetPaths::getProjectFolderPath();
        const string  rootFolder    = projectFolder.empty() ? FileUtil::getCurrentPath() : projectFolder;
        const string  folder        = FileUtil::joinPath( FileUtil::joinPath( rootFolder, path::kSavedFolder ), kDevCertificateFolder );
        outCertificateFile          = FileUtil::joinPath( folder, kDevCertificateFile );
        outPrivateKeyFile           = FileUtil::joinPath( folder, kDevPrivateKeyFile );
        if ( FileUtil::isRegularFile( outCertificateFile ) && FileUtil::isRegularFile( outPrivateKeyFile ) )
            return true;
        string     certificatePem;
        string     privateKeyPem;
        const bool bCreated = FileUtil::ensureDirectoryExists( folder ) &&
                              getProvider().createSelfSignedCertificate( "localhost", NetSecurityInternal::kDevCertificateValidDays, certificatePem, privateKeyPem ) &&
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

    unique_ptr<ITLSContext> NetSecurity::createServerTLSContext( string_view certificateFile, string_view privateKeyFile, string_view privateKeyPassphrase,
                                                                 string& outError )
    {
        using Internal         = NetSecurityInternal;
        string certificatePath = makeProjectPath( certificateFile );
        string privateKeyPath  = makeProjectPath( privateKeyFile );
        if ( certificatePath.empty() != privateKeyPath.empty() )
        {
            outError = "the TLS certificate and private key files must be configured together";
            return nullptr;
        }
        if ( certificatePath.empty() && ensureDevCertificate( certificatePath, privateKeyPath, outError ) == false )
            return nullptr;
        TLSContextConfig settings;
        settings._role                 = TLSRole::Server;
        settings._privateKeyPassphrase = string( privateKeyPassphrase );
        if ( Internal::readPem( certificatePath, settings._certificatePem, outError ) == false ||
             Internal::readPem( privateKeyPath, settings._privateKeyPem, outError ) == false )
            return nullptr;
        unique_ptr<ITLSContext> context = getProvider().createTLSContext( settings, outError );
        settings._privateKeyPassphrase.assign( settings._privateKeyPassphrase.size(), '\0' );
        return context;
    }

    unique_ptr<ITLSContext> NetSecurity::createClientTLSContext( string_view trustFile, string_view serverName, string& outError, string_view alpnProtocol )
    {
        using Internal = NetSecurityInternal;
        TLSContextConfig settings;
        settings._role         = TLSRole::Client;
        settings._serverName   = string( serverName );
        settings._alpnProtocol = string( alpnProtocol );
        string trustPath       = makeProjectPath( trustFile );
        if ( trustPath.empty() )
        {
            string privateKeyPath;
            if ( ensureDevCertificate( trustPath, privateKeyPath, outError ) == false || Internal::readPem( trustPath, settings._trustPem, outError ) == false )
                return nullptr;
            if ( getProvider().computeCertificateSHA256( settings._trustPem, settings._pinnedCertificateSHA256Hex ) == false )
            {
                outError = "cannot compute the SHA-256 of the development certificate";
                return nullptr;
            }
        }
        else if ( Internal::readPem( trustPath, settings._trustPem, outError ) == false )
        {
            return nullptr;
        }
        return getProvider().createTLSContext( settings, outError );
    }
} // namespace sw
