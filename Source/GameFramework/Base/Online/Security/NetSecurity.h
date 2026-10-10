/**
 * @file NetSecurity.h
 * @brief OpenSSL 로 구현한 `INetSecurityProvider` 와 TLS 컨텍스트 도우미입니다. 제공자는 프로세스에 하나 — GameFramework 안이라 게임 · 키트 모듈 리로드에 살아남는다.
 * @note OpenSSL 은 GameFramework 만 링크합니다 — 온라인을 쓰지 않는 게임의 Engine.dll 은 libssl · libcrypto 에 매이지 않는다.
 * @details 인증서 · 키는 PEM 파일 경로로 받는다(두 플랫폼 같은 길 — 전용 서버는 서버 설정 `Config/Server/<게임>.json` 의 `_tlsCertificateFile` ·
 *          `_tlsPrivateKeyFile` 이 경로를 준다). 상대 경로는 프로젝트 루트 기준이다. 경로를 비우면 **Dev 에서만** `Saved/Certificates/devserver.cert.pem` ·
 *          `devserver.key.pem`(없으면 만든다)을 쓰고, Shipping 은 오류다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class INetSecurityProvider;
    class ITLSContext;
} // namespace sw

namespace sw
{
    struct SW_GF_API NetSecurity
    {
        static constexpr const utf8* kDevCertificateFolder = "Certificates"; ///< 프로젝트 루트의 `Saved/` 아래
        static constexpr const utf8* kDevCertificateFile   = "devserver.cert.pem";
        static constexpr const utf8* kDevPrivateKeyFile    = "devserver.key.pem";

        static INetSecurityProvider& getProvider();

        /**
         * @brief 서버 TLS 컨텍스트입니다. 실패하면 nullptr 이고 @p outError 에 까닭.
         * @param privateKeyPassphrase 키가 암호로 잠겼을 때만(서버 설정의 환경 변수에서 `ServerSecret::read` 로 읽은 값). 비우면 암호 없는 키.
         * @details 인증서 · 키 경로가 둘 다 비면 Dev 에서만 개발용 인증서를 쓴다(Shipping 은 오류). 하나만 비면 오류.
         */
        static unique_ptr<ITLSContext> createServerTLSContext( string_view certificateFile, string_view privateKeyFile, string_view privateKeyPassphrase,
                                                               string& outError );
        /**
         * @brief 클라이언트 TLS 컨텍스트 — @p trustFile(CA 묶음 또는 서버 인증서 그 자체)로 서버를 검증하고, @p serverName 이 있으면 이름도 본다.
         *        @p trustFile 을 비우면 Dev 에서만 개발용 인증서를 신뢰 + 고정(SHA-256)한다.
         */
        static unique_ptr<ITLSContext> createClientTLSContext( string_view trustFile, string_view serverName, string& outError );
        /** @brief Dev — 개발용 인증서 · 키가 없으면 만듭니다(서버 · 클라이언트가 같은 PC · 같은 폴더를 보는 경우). 쓴 경로를 돌려준다. Shipping 은 늘 false. */
        [[nodiscard]] static bool ensureDevCertificate( string& outCertificateFile, string& outPrivateKeyFile, string& outError );
    };
} // namespace sw
