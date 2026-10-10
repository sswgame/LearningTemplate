/**
 * @file OidcLoginProvider.h
 * @brief OIDC ID 토큰 제공자 — JWT 서명(RS256 · ES256, JWKS 캐시) · `iss` · `aud` · `exp` · `iat` · `nonce` 를 보고 주체(`sub`)를 돌려줍니다. 구글 · 애플 · 카카오가 설정만으로 된다.
 * @details - JWKS 는 처음 · `_jwksRefreshMs` 마다 · 모르는 kid 가 오면(키 회전 — `_minRefetchMs` 간격 이상) 다시 받는다. 받는 동안 온 표는 기다린다.
 *          - JWKS 서버에 닿지 못해도 캐시 키로 확인할 수 있는 표는 계속 확인한다. 캐시에 없는 kid 는 받기가 실패했으면 `_bUnavailable`, 받았는데도 없으면 `_bRejected`.
 *          - 표 글 = ID 토큰, 또는 `ID 토큰|nonce`(클라이언트가 인증 요청에 실은 값 그대로 — 토큰의 `nonce` 와 같아야 한다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/JSONWebToken.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderSettings.h"

namespace sw
{
    class HttpClient;
    class INetSecurityProvider;

    /**
     * @class OidcLoginProvider
     * @brief OIDC ID 토큰 제공자입니다.
     */
    class SW_GF_API OidcLoginProvider final : public IPlatformLoginProvider
    {
    public:
        /** @brief @p pProvider · @p pHttpClient 는 빌려 쓴다. @p pHttpClient 의 응답은 이 객체 혼자 거둔다. */
        OidcLoginProvider( const PlatformLoginProviderSettings& settings, INetSecurityProvider* pProvider, HttpClient* pHttpClient );

        const utf8* getName() const override { return _settings._name.c_str(); }
        uint64      submitVerification( const vector<uint8>& ticketBytes, int64 nowMs ) override;
        int32       pollVerifications( vector<PlatformLoginVerification>& outListVerification ) override;
        void        tick( int64 nowMs ) override;

        const JwksKeyCache& getKeyCache() const { return _keyCache; }
        int32               getJwksFetchCount() const { return _jwksFetchCount; }

    private:
        struct PendingTicket
        {
            string _token{};
            string _nonce{};
            uint64 _verificationId{ 0 };
            uint32 _waitedFetchGeneration{ 0 }; ///< 이 세대의 받기가 끝나면 다시 본다(0 = 기다리지 않는다)
        };

        enum class Decision : uint8
        {
            Done = 0,
            NeedKey
        };

        void     startJwksFetch( int64 nowMs );
        Decision evaluate( const PendingTicket& pending, int64 nowMs, PlatformLoginVerification& outVerification );

        PlatformLoginProviderSettings     _settings;
        JwksKeyCache                      _keyCache;
        vector<PendingTicket>             _listPending;
        vector<PlatformLoginVerification> _listDone;
        INetSecurityProvider*             _pProvider;
        HttpClient*                       _pHttpClient;
        uint64                            _nextVerificationId;
        uint64                            _jwksRequestId;
        int64                             _lastFetchStartMs;
        uint32                            _fetchGeneration; ///< 끝난 받기 수
        int32                             _jwksFetchCount;
        uint8                             _bLastFetchFailed;
    };
} // namespace sw
