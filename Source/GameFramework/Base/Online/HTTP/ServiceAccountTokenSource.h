/**
 * @file ServiceAccountTokenSource.h
 * @brief 서비스 계정 키로 서명한 JWT 를 OAuth 액세스 토큰으로 바꾸고 캐시합니다(JWT 베어러 교환, RFC 7523). 푸시(FCM)와 구글 영수증 같은 제공자가 함께 씁니다.
 * @details - 자격은 서비스 계정 JSON(`project_id`, `client_email`, `private_key`, `private_key_id`, `token_uri`)이며 운영 비밀(환경 변수)에서 읽어 넘긴다(`ServiceAccountCredentials::read`).
 *          - 토큰 요청: `iss` = 서비스 계정 이메일, `scope`, `aud` = 토큰 주소의 JWT(RS256)를 `grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer` 로 보내
 *            `access_token` · `expires_in` 을 받는다. 만료 1분 전부터 새 토큰이 필요한 것으로 본다(`hasValidToken`).
 *          - 이 객체는 자기 요청의 응답만 알아본다(`handleResponse` 가 아니면 `None`). HTTP 클라이언트는 빌려 쓰고, 응답을 회수하는 쪽(제공자)이 모든 응답을 이 함수에 먼저 보인다.
 *          서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class HTTPClient;
    class INetSecurityProvider;

    /** @brief 서비스 계정 자격입니다. */
    struct SW_GF_API ServiceAccountCredentials
    {
        static constexpr const utf8* kDefaultTokenURL = "https://oauth2.googleapis.com/token";

        string _projectID{};
        string _clientEmail{};
        string _keyID{};
        string _privateKeyPem{};
        string _tokenURL{ kDefaultTokenURL };

        /** @brief 서비스 계정 JSON 을 읽습니다. `project_id` · `client_email` · `private_key` 가 빠졌거나 JSON 이 아니면 false 와 까닭입니다. */
        [[nodiscard]] static bool read( string_view json, ServiceAccountCredentials& outCredentials, string& outError );
        /** @brief 서명 키를 메모리에서 지웁니다. */
        void wipe();
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServiceAccountTokenSource
     * @brief 액세스 토큰 하나와 그 요청 상태입니다.
     */
    class SW_GF_API ServiceAccountTokenSource
    {
    public:
        /** @brief `handleResponse` 의 결과입니다. */
        enum class Outcome : uint8
        {
            None = 0,  ///< 이 객체의 토큰 요청에 대한 응답이 아니다
            Granted,   ///< 새 토큰을 받았다
            Retryable, ///< 서버 오류 · 한도 · 전송 실패다 — 곧 나아질 수 있다
            Rejected   ///< 키나 범위가 틀렸다(4xx) — 다시 해도 같다
        };

        /** @brief @p scope 는 토큰이 쓰일 권한 범위입니다. @p pSecurity · @p pHTTPClient 는 빌려 씁니다. */
        ServiceAccountTokenSource( ServiceAccountCredentials credentials, string scope, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient, int64 requestTimeoutMs );
        ~ServiceAccountTokenSource();

        ServiceAccountTokenSource( const ServiceAccountTokenSource& )            = delete;
        ServiceAccountTokenSource& operator=( const ServiceAccountTokenSource& ) = delete;

        /** @brief 쓸 수 있는 토큰이 있고 만료 1분 전이 아니면 true 입니다. */
        bool          hasValidToken( UnixMilliseconds now ) const { return _accessToken.empty() == false && now.count() + kRefreshMarginMs < _expiresMs; }
        const string& getToken() const { return _accessToken; }
        bool          isRequesting() const { return _requestID != 0; }
        const string& getProjectID() const { return _credentials._projectID; }

        /**
         * @brief 토큰 요청을 보냅니다. 이미 보냈으면 아무 일도 없습니다. JWT 를 서명하지 못하면 오류 로그와 false 입니다.
         * @param now JWT 의 `iat` · `exp` 가 되는 유닉스 시각입니다. 형식이 단조 시각을 막는다.
         */
        [[nodiscard]] bool request( UnixMilliseconds now );
        /** @brief 응답이 이 객체의 토큰 요청의 것이면 처리합니다(아니면 `None`). 받은 토큰은 `getToken` 으로 읽는다. */
        Outcome handleResponse( const HTTPClientResponse& response, UnixMilliseconds now );
        /** @brief 거절된 토큰을 버립니다. 다음에 `hasValidToken` 이 false 가 되어 새로 받는다. */
        void invalidate();

    private:
        static constexpr int64 kRefreshMarginMs = 60000;
        static constexpr int64 kJWTLifetimeMs   = 3600000; ///< 구글 상한 1시간

        ServiceAccountCredentials _credentials;
        string                    _scope;
        string                    _accessToken;
        INetSecurityProvider*     _pSecurity;
        HTTPClient*               _pHTTPClient;
        int64                     _requestTimeoutMs;
        int64                     _expiresMs;
        uint64                    _requestID; ///< 0 이면 토큰 요청이 없다
    };
} // namespace sw
