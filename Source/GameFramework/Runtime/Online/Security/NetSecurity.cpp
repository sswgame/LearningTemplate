#include "pch.h"

#include "GameFramework/Runtime/Online/Security/NetSecurity.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Network/DevOnlineExtension.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Content/AssetPaths.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct NetSecurityInternal
        {
            [[nodiscard]] static expected<void> readPem( const string& fileName, string& outText )
            {
                if ( FileUtil::readTextFile( fileName, outText ) )
                    return {};
                return unexpected( Error{ ErrorKind::NotFound, "cannot read the PEM file", fileName } );
            }
        };

        struct NetSecurityDevOnlineInternal
        {
            static constexpr int32 kDevCertificateValidDays = 825; ///< 브라우저 상한과 같은 값 — 개발 PC 에서 2 년 남짓 쓰고 지워 다시 만든다

            [[nodiscard]] static expected<void> ensureDevCertificate( INetSecurityProvider& provider, string& outCertificateFile, string& outPrivateKeyFile )
            {
                const string folder = FileUtil::joinPath( ProcessRoots::get()._derivedData.str(), NetSecurity::kDevCertificateFolder );
                outCertificateFile  = FileUtil::joinPath( folder, NetSecurity::kDevCertificateBundleFile );
                outPrivateKeyFile   = outCertificateFile;
                if ( FileUtil::isRegularFile( outCertificateFile ) )
                    return {};
                string     certificatePem;
                string     privateKeyPem;
                const bool bCreated = FileUtil::ensureDirectoryExists( folder ) &&
                                      provider.createSelfSignedCertificate( "localhost", kDevCertificateValidDays, certificatePem, privateKeyPem ) &&
                                      FileUtil::writeTextFile( outCertificateFile, certificatePem + privateKeyPem ); // 한 번의 원자적 쓰기 — 늘 한 쌍
                if ( bCreated == false )
                    return unexpected( Error{ ErrorKind::ExternalFailure, "cannot create the development certificate", folder } );
                SW_LOG_INFO_ID( "NetSecurity.CreatedDevelopmentTLSCertificate", "Created a development TLS certificate: %#", outCertificateFile.c_str() );
                return {};
            }

            static constexpr NetSecurityDevOptions kDevOptions{ &ensureDevCertificate };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_DEFINE_DEV_ONLINE_EXTENSION( NetSecurityDevOptions, "the development TLS certificate (empty certificate / trust file)" )

    expected<string> NetSecurity::resolveProjectPath( string_view path )
    {
        // 설정 파일의 상대 경로는 프로젝트 기준이다(작업 폴더가 아니다). 해석 규칙은 `AbsolutePath::tryResolve` 하나 - 드라이브 상대(`C:foo`)는 풀지 않는다.
        if ( path.empty() )
            return string{};
        const optional<AbsolutePath> resolved = AbsolutePath::tryResolve( path, ProcessRoots::get()._project );
        if ( resolved.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "not a file path (absolute or relative to the project root)", path } );
        return string( resolved->str() );
    }

    expected<void> NetSecurity::ensureDevCertificate( INetSecurityProvider& provider, string& outCertificateFile, string& outPrivateKeyFile )
    {
        expected<const NetSecurityDevOptions*> devOptions = DevOnlineExtension<NetSecurityDevOptions>::require();
        if ( devOptions.has_value() == false )
            return unexpected( std::move( devOptions.error().appendDetail( " - configure the TLS certificate and private key files" ) ) );
        return ( *devOptions )->_pEnsureDevCertificate( provider, outCertificateFile, outPrivateKeyFile );
    }

    expected<unique_ptr<ITLSContext>> NetSecurity::createServerTLSContext( INetSecurityProvider& provider, string_view certificateFile, string_view privateKeyFile,
                                                                           string_view privateKeyPassphrase )
    {
        using Internal                   = NetSecurityInternal;
        expected<string> certificatePath = resolveProjectPath( certificateFile );
        if ( certificatePath.has_value() == false )
            return unexpected( certificatePath.error() );
        expected<string> privateKeyPath = resolveProjectPath( privateKeyFile );
        if ( privateKeyPath.has_value() == false )
            return unexpected( privateKeyPath.error() );
        if ( certificatePath->empty() != privateKeyPath->empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the TLS certificate and private key files must be configured together" } );
        if ( certificatePath->empty() )
        {
            if ( expected<void> dev = ensureDevCertificate( provider, *certificatePath, *privateKeyPath ); dev.has_value() == false )
                return unexpected( dev.error() );
        }
        TLSContextConfig settings;
        settings._role                 = TLSRole::Server;
        settings._privateKeyPassphrase = string( privateKeyPassphrase );
        expected<void> pem             = Internal::readPem( *certificatePath, settings._certificatePem );
        if ( pem.has_value() )
            pem = Internal::readPem( *privateKeyPath, settings._privateKeyPem );
        if ( pem.has_value() == false )
        {
            settings._privateKeyPassphrase.assign( settings._privateKeyPassphrase.size(), '\0' );
            return unexpected( pem.error() );
        }
        expected<unique_ptr<ITLSContext>> context = provider.createTLSContext( settings );
        settings._privateKeyPassphrase.assign( settings._privateKeyPassphrase.size(), '\0' );
        return context;
    }

    expected<unique_ptr<ITLSContext>> NetSecurity::createClientTLSContext( INetSecurityProvider& provider, string_view trustFile, string_view serverName, string_view alpnProtocol )
    {
        using Internal = NetSecurityInternal;
        TLSContextConfig settings;
        settings._role             = TLSRole::Client;
        settings._serverName       = string( serverName );
        settings._alpnProtocol     = string( alpnProtocol );
        expected<string> trustPath = resolveProjectPath( trustFile );
        if ( trustPath.has_value() == false )
            return unexpected( trustPath.error() );
        if ( trustPath->empty() )
        {
            string privateKeyPath;
            if ( expected<void> dev = ensureDevCertificate( provider, *trustPath, privateKeyPath ); dev.has_value() == false )
                return unexpected( dev.error() );
            if ( expected<void> pem = Internal::readPem( *trustPath, settings._trustPem ); pem.has_value() == false )
                return unexpected( pem.error() );
            if ( provider.computeCertificateSHA256( settings._trustPem, settings._pinnedCertificateSHA256Hex ) == false )
                return unexpected( Error{ ErrorKind::ExternalFailure, "cannot compute the SHA-256 of the development certificate", *trustPath } );
        }
        else if ( expected<void> pem = Internal::readPem( *trustPath, settings._trustPem ); pem.has_value() == false )
        {
            return unexpected( pem.error() );
        }
        return provider.createTLSContext( settings );
    }

    SW_REGISTER_DEV_ONLINE_EXTENSION( NetSecurityDevOptions, NetSecurityDevOnlineInternal::kDevOptions );
} // namespace sw
