/**
 * @file NetSecurity.h
 * @brief TLS 컨텍스트 · 개발 인증서 도우미입니다 — 상태 없는 라이브러리라 암호 제공자를 인자로 받습니다.
 * @note 제공자(`OpenSSLNetSecurityProvider`)는 GameFramework 의 모듈 서브시스템(`OpenSSLNetSecuritySubsystem`, 프로세스 범위)이 든다 — `game::getSubsystem<INetSecurityProvider>()` 로 찾아 넘긴다.
 *       OpenSSL 은 GameFramework 만 링크합니다 — 온라인을 쓰지 않는 게임의 Engine.dll 은 libssl · libcrypto 에 매이지 않는다.
 * @details 인증서 · 키는 PEM 파일 경로로 받는다(두 플랫폼 같은 길 — 전용 서버는 서버 설정 `Config/Server/<게임>.json` 의 `_tlsCertificateFile` ·
 *          `_tlsPrivateKeyFile` 이 경로를 준다). 상대 경로는 프로젝트 루트 기준이다. 경로를 비우면 개발 인증서(DerivedData 의 `Certificates/devserver.pem` — 인증서와 키를
 *          한 PEM 에, 없으면 만든다)를 쓴다. 개발 인증서는 DevOnline 단위(`NetSecurity.cpp`)가 확장점 `NetSecurityDevOptions` 에 올리고, 그 단위가 없는
 *          구성(배포)은 오류다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class INetSecurityProvider;
    class ITLSContext;

    template <typename TValue>
    class DevOnlineExtension;
} // namespace sw

namespace sw
{
    struct SW_GF_API NetSecurity
    {
        static constexpr const utf8* kDevCertificateFolder = "Certificates"; ///< DerivedData 아래 — 서버 · 클라이언트(다른 빌드 구성도)가 같은 인증서를 본다(`NetSecurity.cpp`)
        /**
         * @brief 개발 인증서와 그 키를 함께 담은 PEM 하나입니다. 인증서 · 키 경로 둘 다 이 파일이다(PEM 읽기는 다른 종류 블록을 건너뛴다).
         * @details 두 파일로 따로 쓰면 서버 둘이 동시에 처음 뜰 때 한쪽의 인증서와 다른 쪽의 키가 남아 짝이 어긋난다 — 한 파일의 원자적 쓰기 한 번이면 늘 한 쌍이다.
         */
        static constexpr const utf8* kDevCertificateBundleFile = "devserver.pem";

        /**
         * @brief 설정이 준 경로 @p path 를 프로젝트 루트 기준으로 풉니다(`AbsolutePath::tryResolve` - 절대면 그대로, 상대면 프로젝트 아래).
         * @return 빈 글은 "정하지 않음"이라 @p outPath 를 비우고 true 다. 풀 수 없는 글(드라이브 상대)은 false 와 @p outError 다 - 예전처럼 빈 글로 바꾸면 "정하지 않음"으로 읽혀
         *         개발 인증서로 물러났다.
         */
        [[nodiscard]] static bool resolveProjectPath( string_view path, string& outPath, string& outError );

        /**
         * @brief 서버 TLS 컨텍스트입니다. 실패하면 nullptr 이고 @p outError 에 까닭.
         * @param privateKeyPassphrase 키가 암호로 잠겼을 때만(서버 설정의 환경 변수에서 `ServerSecret::read` 로 읽은 값). 비우면 암호 없는 키.
         * @details 인증서 · 키 경로가 둘 다 비면 개발 인증서를 쓴다(`ensureDevCertificate` — DevOnline 단위가 없는 구성은 오류). 하나만 비면 오류.
         */
        static unique_ptr<ITLSContext> createServerTLSContext( INetSecurityProvider& provider, string_view certificateFile, string_view privateKeyFile,
                                                               string_view privateKeyPassphrase, string& outError );
        /**
         * @brief 클라이언트 TLS 컨텍스트 — @p trustFile(CA 번들 또는 서버 인증서 그 자체)로 서버를 검증하고, @p serverName 이 있으면 이름도 본다.
         *        @p trustFile 을 비우면 개발 인증서를 신뢰 + 고정(SHA-256)한다(DevOnline 단위가 없는 구성은 오류).
         */
        static unique_ptr<ITLSContext> createClientTLSContext( INetSecurityProvider& provider, string_view trustFile, string_view serverName, string& outError,
                                                               string_view alpnProtocol = {} );
        /**
         * @brief 개발용 인증서 · 키가 없으면 만듭니다(서버 · 클라이언트가 같은 PC · 같은 폴더를 보는 경우). 쓴 경로를 돌려준다.
         * @details 확장점 `NetSecurityDevOptions` 가 비었으면(DevOnline 단위가 없는 구성 — 배포) false 와 까닭이다.
         */
        [[nodiscard]] static bool ensureDevCertificate( INetSecurityProvider& provider, string& outCertificateFile, string& outPrivateKeyFile, string& outError );
    };

    /**
     * @struct NetSecurityDevOptions
     * @brief 개발 인증서의 확장점입니다. DevOnline 단위(`NetSecurity.cpp`)가 올린다(`DevOnlineExtension.h`). 같은 모듈(GameFramework) 안에서만 쓴다.
     */
    struct NetSecurityDevOptions
    {
        using EnsureDevCertificateFunction = bool ( * )( INetSecurityProvider& provider, string& outCertificateFile, string& outPrivateKeyFile, string& outError );

        EnsureDevCertificateFunction _pEnsureDevCertificate{ nullptr };

        static DevOnlineExtension<NetSecurityDevOptions> s_devOnline;
    };
} // namespace sw
