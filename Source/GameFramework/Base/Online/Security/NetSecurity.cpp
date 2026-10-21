#include "pch.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Net/GameConnectSecurity.h"
#include "Engine/Resource/ResourcePaths.h"

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
    } // namespace
} // namespace sw

namespace sw
{
    string NetSecurity::makeProjectPath( string_view path )
    {
        const string& projectFolder = ResourcePaths::getProjectFolderPath();
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

    /**
     * @brief 전용 서버의 접속 티켓 게임 포트(`GameConnectSecurity`, Engine)가 이 제공자를 쓰게, GameFramework 모듈이 서고 내려갈 때 꺼내는 함수를 넣고 뺍니다.
     * @details Engine 은 OpenSSL 구현을 모른다. 함수를 넣는 것이라 제공자는 처음 쓸 때 만든다(정적 초기화 순서에 기대지 않는다).
     */
    struct NetSecurityGameConnectRegistrar
    {
        NetSecurityGameConnectRegistrar() { GameConnectSecurity::get().setProviderAccessor( &NetSecurity::getProvider ); }
        ~NetSecurityGameConnectRegistrar() { GameConnectSecurity::get().setProviderAccessor( nullptr ); }
    };
    const NetSecurityGameConnectRegistrar s_netSecurityGameConnectRegistrar;

    bool NetSecurity::ensureDevCertificate( string& outCertificateFile, string& outPrivateKeyFile, string& outError )
    {
#if defined( SW_SHIPPING )
        (void)outCertificateFile;
        (void)outPrivateKeyFile;
        outError = "development certificates are not available in Shipping - configure the TLS certificate and private key files";
        return false;
#else
        const string& projectFolder = ResourcePaths::getProjectFolderPath();
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

    unique_ptr<ITLSContext> NetSecurity::createClientTLSContext( string_view trustFile, string_view serverName, string& outError )
    {
        using Internal = NetSecurityInternal;
        TLSContextConfig settings;
        settings._role       = TLSRole::Client;
        settings._serverName = string( serverName );
        string trustPath     = makeProjectPath( trustFile );
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
        return getProvider().createTLSContext( settings, outError );
    }
} // namespace sw
