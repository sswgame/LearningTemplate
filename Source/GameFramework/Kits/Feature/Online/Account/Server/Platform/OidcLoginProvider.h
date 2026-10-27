/**
 * @file OidcLoginProvider.h
 * @brief OIDC ID 토큰 제공자입니다. JWT 서명(RS256, ES256, JWKS 캐시)과 `iss`, `aud`, `exp`, `iat`, `nonce` 를 확인해 주체(`sub`)를 돌려줍니다. 구글, 애플, 카카오는 설정만으로 쓸 수 있습니다.
 * @details - JWKS 는 처음 확인할 때, `_jwksRefreshMs` 마다, 모르는 kid 가 올 때 다시 받는다. 모르는 kid 는 키 회전일 수 있으며 `_minRefetchMs` 이상 간격을 두고 받는다. 받는 동안 도착한 티켓은 기다린다.
 *          - JWKS 서버에 닿지 못해도 캐시된 키로 확인할 수 있는 티켓은 계속 확인한다. 캐시에 없는 kid 는 받기가 실패했으면 `_bUnavailable`, 받았는데도 없으면 `_bRejected` 이다.
 *          - 티켓 문자열은 ID 토큰이거나 `ID 토큰|nonce` 이다. nonce 는 클라이언트가 인증 요청에 실은 값 그대로이며 토큰의 `nonce` 와 같아야 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/Security/JSONWebToken.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderConfig.h"

namespace sw
{
    class HTTPClient;
    class INetSecurityProvider;

    /**
     * @class OidcLoginProvider
     * @brief OIDC ID 토큰 제공자입니다.
     */
    class SW_KIT_SERVER_API OidcLoginProvider final : public IPlatformLoginProvider
    {
    public:
        /** @brief @p pProvider 와 @p pHTTPClient 는 빌려 씁니다. @p pHTTPClient 의 응답은 이 객체만 회수합니다. */
        OidcLoginProvider( const PlatformLoginProviderConfig& settings, INetSecurityProvider* pProvider, HTTPClient* pHTTPClient );

        const utf8* getName() const override { return _settings._name.c_str(); }
        uint64      submitVerification( const vector<uint8>& ticketBytes, UnixMilliseconds nowMs ) override;
        int32       pollVerifications( vector<PlatformLoginVerification>& outListVerification ) override;
        void        update( UnixMilliseconds nowMs ) override;

        const JwksKeyCache& getKeyCache() const { return _keyCache; }
        int32               getJwksFetchCount() const { return _jwksFetchCount; }

    private:
        struct PendingTicket
        {
            string _token{};
            string _nonce{};
            uint64 _verificationID{ 0 };
            uint32 _waitedFetchGeneration{ 0 }; ///< 이 세대의 받기가 끝나면 다시 확인한다. 0이면 기다리지 않는다
        };

        enum class Decision : uint8
        {
            Done = 0,
            NeedKey
        };

        void startJwksFetch( UnixMilliseconds nowMs );
        /** @brief 토큰 하나를 확인합니다. @p now 는 `exp` · `iat` 와 견주는 유닉스 시각입니다(형식이 단조 시각을 막는다). */
        Decision evaluate( const PendingTicket& pending, UnixMilliseconds now, PlatformLoginVerification& outVerification );

        PlatformLoginProviderConfig       _settings;
        JwksKeyCache                      _keyCache;
        vector<PendingTicket>             _listPending;
        vector<PlatformLoginVerification> _listDone;
        INetSecurityProvider*             _pProvider;
        HTTPClient*                       _pHTTPClient;
        IDAllocator<uint64>               _verificationIDAllocator;
        uint64                            _jwksRequestID;
        UnixMilliseconds                  _lastFetchStartMs;
        uint32                            _fetchGeneration; ///< 끝난 받기의 횟수이다
        int32                             _jwksFetchCount;
        uint8                             _bLastFetchFailed;
    };
} // namespace sw
