#include "pch.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/FileSystem/AssetPaths.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

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
        // 설정 파일의 상대 경로는 프로젝트 기준이다(작업 폴더가 아니다). 해석 규칙은 `AbsolutePath::tryResolve` 하나 - 드라이브 상대(`C:foo`)는 풀지 않는다(빈 글, 읽기가 실패로 알린다).
        if ( path.empty() )
            return {};
        const std::optional<AbsolutePath> resolved = AbsolutePath::tryResolve( path, ProcessRoots::get()._project );
        return resolved.has_value() ? resolved->str() : string{};
    }

    bool NetSecurity::ensureDevCertificate( INetSecurityProvider& provider, string& outCertificateFile, string& outPrivateKeyFile, string& outError )
    {
#if defined( SW_SHIPPING )
        (void)provider;
        (void)outCertificateFile;
        (void)outPrivateKeyFile;
        outError = "development certificates are not available in Shipping - configure the TLS certificate and private key files";
        return false;
#else
        const string folder = FileUtil::joinPath( ProcessRoots::get()._derivedData.str(), kDevCertificateFolder ); // 서버 · 클라이언트(다른 빌드 구성도)가 같은 인증서를 본다
        outCertificateFile  = FileUtil::joinPath( folder, kDevCertificateBundleFile );
        outPrivateKeyFile   = outCertificateFile;
        if ( FileUtil::isRegularFile( outCertificateFile ) )
            return true;
        string     certificatePem;
        string     privateKeyPem;
        const bool bCreated = FileUtil::ensureDirectoryExists( folder ) &&
                              provider.createSelfSignedCertificate( "localhost", NetSecurityInternal::kDevCertificateValidDays, certificatePem, privateKeyPem ) &&
                              FileUtil::writeTextFile( outCertificateFile, certificatePem + privateKeyPem ); // 한 번의 원자적 쓰기 — 늘 한 쌍
        if ( bCreated == false )
        {
            outError = "cannot create the development certificate in " + folder;
            return false;
        }
        SW_LOG_INFO( "Created a development TLS certificate: %#", outCertificateFile.c_str() );
        return true;
#endif
    }

    unique_ptr<ITLSContext> NetSecurity::createServerTLSContext( INetSecurityProvider& provider, string_view certificateFile, string_view privateKeyFile,
                                                                 string_view privateKeyPassphrase, string& outError )
    {
        using Internal         = NetSecurityInternal;
        string certificatePath = makeProjectPath( certificateFile );
        string privateKeyPath  = makeProjectPath( privateKeyFile );
        if ( certificatePath.empty() != privateKeyPath.empty() )
        {
            outError = "the TLS certificate and private key files must be configured together";
            return nullptr;
        }
        if ( certificatePath.empty() && ensureDevCertificate( provider, certificatePath, privateKeyPath, outError ) == false )
            return nullptr;
        TLSContextConfig settings;
        settings._role                 = TLSRole::Server;
        settings._privateKeyPassphrase = string( privateKeyPassphrase );
        if ( Internal::readPem( certificatePath, settings._certificatePem, outError ) == false ||
             Internal::readPem( privateKeyPath, settings._privateKeyPem, outError ) == false )
            return nullptr;
        unique_ptr<ITLSContext> context = provider.createTLSContext( settings, outError );
        settings._privateKeyPassphrase.assign( settings._privateKeyPassphrase.size(), '\0' );
        return context;
    }

    unique_ptr<ITLSContext> NetSecurity::createClientTLSContext( INetSecurityProvider& provider, string_view trustFile, string_view serverName, string& outError,
                                                                 string_view alpnProtocol )
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
            if ( ensureDevCertificate( provider, trustPath, privateKeyPath, outError ) == false || Internal::readPem( trustPath, settings._trustPem, outError ) == false )
                return nullptr;
            if ( provider.computeCertificateSHA256( settings._trustPem, settings._pinnedCertificateSHA256Hex ) == false )
            {
                outError = "cannot compute the SHA-256 of the development certificate";
                return nullptr;
            }
        }
        else if ( Internal::readPem( trustPath, settings._trustPem, outError ) == false )
        {
            return nullptr;
        }
        return provider.createTLSContext( settings, outError );
    }
} // namespace sw
