/**
 * @file AppleLoginProvider.h
 * @brief 애플 로그인 제공자입니다(OnlineKits A4). ID 토큰 확인은 공통 OIDC 구현(`OidcLoginProvider`)이 하고, 인가 코드 교환과 탈퇴 때 토큰 철회를 더합니다.
 * @details 애플은 탈퇴한 계정의 토큰을 철회하라고 요구한다(App Store 심사 지침 5.1.1(v)). 철회에는 ID 토큰이 아니라 인가 코드를 교환해 받은 refresh token 이 필요하다.
 *          - 티켓은 `ID 토큰|nonce|인가 코드`다(nonce 가 없으면 `ID 토큰||인가 코드`, 인가 코드가 없으면 `OidcLoginProvider` 와 같은 `ID 토큰[|nonce]` — 그때는 연동 비밀이 없어 철회할 것이 없다).
 *          - client secret 은 서버가 서명 키(.p8 PEM)로 만드는 JWT 다(`JSONWebToken::makeSigned`, ES256): `kid` = 서명 키 id, `iss` = 팀 id, `sub` = client id(`clientIds` 의 첫 값),
 *            `aud` = `https://appleid.apple.com`, 수명 24 시간(애플 상한 6 개월보다 짧다). 만료 1 시간 전에 다시 만든다.
 *          - 인가 코드 교환(`POST /auth/token`, form `grant_type=authorization_code`)의 `refresh_token` 이 `PlatformLoginVerification::_linkSecret` 이다. 교환이 실패해도 로그인은
 *            성공한다(ID 토큰은 이미 확인했다) — 연동 비밀만 없다.
 *          - 철회(`POST /auth/revoke`, form `token` · `token_type_hint=refresh_token`): 200 이면 철회, 400 `invalid_grant` 는 이미 무효라 철회한 것으로 본다. 5xx · 전송 실패 · `invalid_client`(설정 문제)는
 *            `_bUnavailable` — 대기열에 남아 다시 시도한다.
 *          HTTP 클라이언트가 둘이다(ID 토큰의 JWKS 용과 이 제공자의 교환 · 철회 용) — 클라이언트의 응답은 소유한 쪽이 모두 회수하기 때문이다. 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/OidcLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderConfig.h"

namespace sw
{
    class HTTPClient;
    class INetSecurityProvider;

    /**
     * @class AppleLoginProvider
     * @brief 애플 로그인 제공자입니다.
     */
    class SW_KIT_SERVER_API AppleLoginProvider final : public IPlatformLoginProvider
    {
    public:
        static constexpr const utf8* kAudience         = "https://appleid.apple.com";
        static constexpr const utf8* kDefaultTokenURL  = "https://appleid.apple.com/auth/token";
        static constexpr const utf8* kDefaultRevokeURL = "https://appleid.apple.com/auth/revoke";

        /**
         * @brief 서명 키를 환경 변수(`settings._keySecretEnvironment`)에서 읽어 만듭니다. 읽지 못하면 오류를 남기고 nullptr 입니다.
         * @param pOidcHTTPClient ID 토큰 확인(JWKS)용. @param pAppleHTTPClient 교환 · 철회용. 둘 다 빌려 쓰며 서로 다른 객체여야 합니다.
         */
        static unique_ptr<IPlatformLoginProvider> create( const PlatformLoginProviderConfig& settings, INetSecurityProvider* pSecurity, HTTPClient* pOidcHTTPClient,
                                                          HTTPClient* pAppleHTTPClient );
        /** @brief 서명 키를 이미 가진 쪽(테스트)이 만듭니다. */
        AppleLoginProvider( const PlatformLoginProviderConfig& settings, string signingKeyPem, INetSecurityProvider* pSecurity, HTTPClient* pOidcHTTPClient,
                            HTTPClient* pAppleHTTPClient );
        ~AppleLoginProvider() override;

        const utf8* getName() const override { return _settings._name.c_str(); }
        uint64      submitVerification( const vector<uint8>& ticketBytes, int64 nowMs ) override;
        int32       pollVerifications( vector<PlatformLoginVerification>& outListVerification ) override;
        void        update( int64 nowMs ) override;
        bool        supportsRevocation() const override { return true; }
        uint64      submitRevocation( string_view linkSecret, int64 nowMs ) override;
        int32       pollRevocations( vector<PlatformLoginRevocation>& outListRevocation ) override;

        /** @brief 티켓 `ID 토큰|nonce|인가 코드` 를 나눕니다. 인가 코드가 없으면 @p outCode 가 빕니다. */
        static void splitTicket( string_view ticket, string& outIDToken, string& outNonce, string& outCode );
        /** @brief 지금 쓸 client secret(JWT)을 반환합니다. 서명하지 못하면 빈 글입니다. 테스트가 봅니다. @p now 는 `iat` · `exp` 의 유닉스 시각입니다. */
        string makeClientSecret( UnixMilliseconds now );

    private:
        struct PendingVerification
        {
            PlatformLoginVerification _verified{}; ///< ID 토큰 확인이 끝난 결과(연동 비밀은 교환 뒤에 채운다)
            string                    _code{};
            uint64                    _outerID{ 0 };
            uint64                    _innerID{ 0 };
            uint64                    _exchangeRequestID{ 0 }; ///< 0 이면 교환 전이다
        };

        void   submitExchange( PendingVerification& pending, int64 nowMs );
        void   finishExchange( PendingVerification& pending, const HTTPClientResponse& response );
        void   finishRevocation( uint64 revocationID, const HTTPClientResponse& response );
        uint64 submitForm( const string& url, const vector<HTTPHeader>& listForm, int64 nowMs );

        PlatformLoginProviderConfig       _settings;
        OidcLoginProvider                 _oidc;
        string                            _signingKeyPem;
        string                            _clientSecret;
        string                            _tokenURL;
        string                            _revokeURL;
        vector<PendingVerification>       _listPending;
        vector<PlatformLoginVerification> _listDone;
        vector<PlatformLoginRevocation>   _listRevocationDone;
        vector<PlatformLoginVerification> _listInnerScratch;
        unordered_map<uint64, uint64>     _mapRequestToRevocation;
        INetSecurityProvider*             _pSecurity;
        HTTPClient*                       _pAppleHTTPClient;
        int64                             _secretExpiresMs;
        IDAllocator<uint64>               _verificationIDAllocator;
        IDAllocator<uint64>               _revocationIDAllocator;
    };
} // namespace sw
