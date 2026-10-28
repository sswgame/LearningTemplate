/**
 * @file ApnsPushProvider.h
 * @brief iOS 푸시 제공자(APNs 토큰 기반 인증 HTTP/2 API)입니다(OnlineKits G4 · H2-5). 인증 키(.p8)로 서명한 제공자 토큰(JWT ES256)을 캐시하고, 기기 토큰마다 `POST /3/device/<토큰>` 을 HTTP/2 로 보냅니다.
 * @details - 자격(인증 키 PEM)은 설정 파일에 쓰지 않고 환경 변수(서버 비밀)에서 읽는다(`PushProviderSet`). 키 id · 팀 id · 번들 id(`apns-topic`)는 비밀이 아니라 설정에 쓴다. 설정이 없으면 이 제공자를 만들지 않는다.
 *          - 제공자 토큰은 `iss`(팀 id) · `iat` 만 있는 JWT 다(헤더 `kid` = 키 id). APNs 는 20 분보다 자주 갱신하면 429, 60 분을 넘기면 403 으로 거절하므로 40 분마다 새로 서명한다(`kTokenRefreshMs`).
 *            서명은 로컬이라 기다림이 없다. 서명하지 못하면(키가 틀림) 오류 로그와 함께 그 배달을 `Rejected` 로 끝낸다.
 *          - 문구는 현지화 키다. `aps.alert` 의 `title-loc-key` · `loc-key` · `loc-args` 에 싣고 기기가 자기 언어로 푼다 — 서버는 언어를 모르고 `send` 의 @p locale 을 쓰지 않는다.
 *            문구가 없으면 조용한 알림(`apns-push-type: background`, `content-available`, 우선순위 5)이다. `deep_link` 와 인자는 사용자 키로 싣는다(앱이 화면을 연다).
 *          - 기기 토큰은 16진 글자만 받는다(경로에 들어가므로). 아니면 요청을 보내지 않고 `InvalidToken` 이다.
 *          - 상태 대응: 200 Delivered, 410 InvalidToken(앱이 지워졌다), 429 RateLimited(`Retry-After` 가 있으면 그 초), 5xx · 전송 실패 Transient, 403 Rejected(제공자 토큰을 버려 다음 배달이 새로 서명),
 *            그 밖의 4xx Rejected(400 `BadDeviceToken` 은 개발 · 운영 환경이 어긋난 토큰일 수 있어 기기를 지우지 않는다).
 *          - 토픽 발송은 없다(APNs 에 토픽 구독이 없다) — `supportsTopics` 는 false 다.
 *          제품 이름(`apns`)은 이 설정의 `kind` 값과 `Provider/Apns/` 안에만 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationProvider.h"

namespace sw
{
    class HTTP2Client;
    class INetSecurityProvider;

    /** @brief APNs 제공자의 설정입니다. 시간은 밀리초입니다. */
    struct SW_KIT_SERVER_API ApnsPushSettings
    {
        static constexpr const utf8* kDefaultBaseURL = "https://api.push.apple.com"; ///< 개발 환경은 `https://api.sandbox.push.apple.com`(설정이 덮어쓴다)

        string _privateKeyPem{}; ///< 인증 키(.p8, PKCS#8 PEM). 환경 변수에서 읽는다
        string _keyID{};         ///< 인증 키 id(JWT 헤더 `kid`)
        string _teamID{};        ///< 팀 id(JWT `iss`)
        string _topic{};         ///< 번들 id(`apns-topic`)
        string _baseURL{ kDefaultBaseURL };
        int64  _requestTimeoutMs{ 10000 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ApnsPushProvider
     * @brief id 가 "apns" 인 APNs 제공자입니다. 서비스 스레드 하나에서 씁니다.
     */
    class SW_KIT_SERVER_API ApnsPushProvider final : public IPushNotificationProvider
    {
    public:
        static constexpr int64 kTokenRefreshMs = 40LL * 60 * 1000; ///< 제공자 토큰을 새로 서명하는 간격(APNs 허용 20~60 분의 가운데)

        /** @brief @p pHTTPClient 는 빌려 쓰며 이 제공자만 응답을 회수합니다. @p pSecurity 는 JWT 서명에 씁니다. */
        ApnsPushProvider( ApnsPushSettings settings, INetSecurityProvider* pSecurity, HTTP2Client* pHTTPClient );
        ~ApnsPushProvider() override;

        string_view getProviderID() const override { return "apns"; }
        void        send( uint64 deliveryID, const string& deviceToken, const string& locale, const PushNotificationMessage& message, UnixMilliseconds nowMs ) override;
        void        update( UnixMilliseconds nowMs ) override;
        int32       pollResults( vector<PushDeliveryResult>& outListResult ) override;

        /** @brief 알림 본문(JSON)입니다. 문구가 없으면 조용한 알림(`content-available`)입니다. 테스트가 봅니다. */
        static string makePayloadJSON( const PushNotificationMessage& message );
        /** @brief 문구가 있어 보이는 알림인가(`apns-push-type` 이 `alert`) — 없으면 `background` 다. */
        static bool hasAlert( const PushNotificationMessage& message );
        /** @brief 기기 토큰 규칙(16진 글자 1..200 자)입니다. */
        static bool isValidDeviceToken( string_view token );
        /** @brief 응답을 배달 결과로 바꿉니다(`Retry-After` 는 따로 읽는다). */
        static PushDeliveryStatus mapStatus( int32 statusCode, bool bTransportFailed );

    private:
        /** @brief 제공자 토큰이 없거나 낡았으면 새로 서명합니다. 서명하지 못하면 false 입니다. */
        [[nodiscard]] bool ensureProviderToken( UnixMilliseconds nowMs );
        void               finishSend( uint64 deliveryID, const HTTPClientResponse& response );
        void               complete( uint64 deliveryID, PushDeliveryStatus status, int64 retryAfterMs = 0 );

        ApnsPushSettings              _settings;
        string                        _providerToken;
        string                        _deviceURLPrefix; ///< `<기준 주소>/3/device/`
        vector<PushDeliveryResult>    _listDone;
        unordered_map<uint64, uint64> _mapRequestToDelivery;
        INetSecurityProvider*         _pSecurity;
        HTTP2Client*                  _pHTTPClient;
        int64                         _tokenIssuedMs;
    };
} // namespace sw
