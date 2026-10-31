/**
 * @file NetSecurityDevOnline.cpp
 * @brief DevOnline 단위 — 개발 TLS 인증서(자체 서명, DerivedData 의 `Certificates/devserver.pem`)를 확장점 `NetSecurityDevOptions` 에 올립니다.
 * @details 배포 구성은 이 파일을 짓지 않는다(`SW_DEV_ONLY_SOURCE_PATTERNS`) — 빈 인증서 · 신뢰 파일 경로는 그 구성에서 오류다(`NetSecurity::ensureDevCertificate`).
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "GameFramework/Base/Online/Config/DevOnlineExtension.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

namespace sw
{
    SW_LOG_CALLER( "NetSecurity" );

    namespace
    {
        struct NetSecurityDevOnlineInternal
        {
            static constexpr int32 kDevCertificateValidDays = 825; ///< 브라우저 상한과 같은 값 — 개발 PC 에서 2 년 남짓 쓰고 지워 다시 만든다

            [[nodiscard]] static bool ensureDevCertificate( INetSecurityProvider& provider, string& outCertificateFile, string& outPrivateKeyFile, string& outError )
            {
                const string folder = FileUtil::joinPath( ProcessRoots::get()._derivedData.str(), NetSecurity::kDevCertificateFolder );
                outCertificateFile  = FileUtil::joinPath( folder, NetSecurity::kDevCertificateBundleFile );
                outPrivateKeyFile   = outCertificateFile;
                if ( FileUtil::isRegularFile( outCertificateFile ) )
                    return true;
                string     certificatePem;
                string     privateKeyPem;
                const bool bCreated = FileUtil::ensureDirectoryExists( folder ) &&
                                      provider.createSelfSignedCertificate( "localhost", kDevCertificateValidDays, certificatePem, privateKeyPem ) &&
                                      FileUtil::writeTextFile( outCertificateFile, certificatePem + privateKeyPem ); // 한 번의 원자적 쓰기 — 늘 한 쌍
                if ( bCreated == false )
                {
                    outError = "cannot create the development certificate in " + folder;
                    return false;
                }
                SW_LOG_INFO( "Created a development TLS certificate: %#", outCertificateFile.c_str() );
                return true;
            }

            static constexpr NetSecurityDevOptions kDevOptions{ &ensureDevCertificate };
        };
    } // namespace

    SW_REGISTER_DEV_ONLINE_EXTENSION( NetSecurityDevOptions, NetSecurityDevOnlineInternal::kDevOptions );
} // namespace sw
