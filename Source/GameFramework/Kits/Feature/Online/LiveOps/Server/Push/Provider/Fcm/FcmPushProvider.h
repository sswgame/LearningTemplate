/**
 * @file FcmPushProvider.h
 * @brief 안드로이드 푸시 제공자(FCM HTTP v1)입니다(OnlineKits G4). 서비스 계정 키로 서명한 JWT 를 OAuth 토큰으로 바꿔 캐시하고, 메시지를 `messages:send` 에 보냅니다.
 * @details - 자격(서비스 계정 JSON)은 설정 파일에 쓰지 않고 환경 변수(서버 비밀)에서 읽는다(`ServiceAccountCredentials::read`). 설정이 없으면 이 제공자를 만들지 않는다.
 *          - 토큰은 `ServiceAccountTokenSource`(범위 `firebase.messaging`)가 받아 캐시한다. 토큰을 기다리는 배달은 토큰이 오면 보낸다.
 *          - 문구는 현지화 키다. 안드로이드 `title_loc_key` · `body_loc_key` 와 iOS(`apns`) `title-loc-key` · `loc-key` 에 싣고, 기기가 자기 언어로 푼다 —
 *            그래서 서버는 언어를 모르고 `send` 의 @p locale 을 쓰지 않는다. `deep_link` 와 인자는 `data` 에도 싣는다(앱이 화면을 연다).
 *          - 상태 대응: 200 Delivered, 404 · `UNREGISTERED` InvalidToken, 429 RateLimited(`Retry-After` 초), 5xx · 전송 실패 Transient, 401 · 403 Rejected(토큰을 버려 다음에 다시 받는다),
 *            그 밖의 4xx Rejected. 토큰 요청이 5xx · 전송 실패면 기다리던 배달이 Transient, 4xx 면 Rejected 다.
 *          - 토픽 발송(`sendToTopic`): `message.topic` 으로 한 번 보낸다. 토픽 이름은 `[A-Za-z0-9-_.~%]+` 이다.
 *          APNs 는 HTTP/2 가 필수라 공통 HTTP 클라이언트에 HTTP/2 가 들어온 뒤에 따로 둔다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Base/Online/HTTP/ServiceAccountTokenSource.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationProvider.h"

namespace sw
{
    class HTTPClient;
    class INetSecurityProvider;

    /** @brief FCM 제공자의 설정입니다. 시간은 밀리초입니다. */
    struct SW_KIT_SERVER_API FcmPushSettings
    {
        static constexpr const utf8* kDefaultSendBaseURL = "https://fcm.googleapis.com";
        static constexpr const utf8* kScope              = "https://www.googleapis.com/auth/firebase.messaging";

        ServiceAccountCredentials _credentials{};                      ///< `_tokenURL` 은 서비스 계정의 `token_uri` 이고 설정이 덮어쓸 수 있다(가짜 서버)
        string                    _sendBaseURL{ kDefaultSendBaseURL }; ///< 설정이 덮어쓸 수 있다(가짜 서버)
        int64                     _requestTimeoutMs{ 10000 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class FcmPushProvider
     * @brief id 가 "fcm" 인 FCM HTTP v1 제공자입니다. 서비스 스레드 하나에서 씁니다.
     */
    class SW_KIT_SERVER_API FcmPushProvider final : public IPushNotificationProvider
    {
    public:
        /** @brief @p pHTTPClient 는 빌려 쓰며 이 제공자만 응답을 회수합니다. @p pSecurity 는 JWT 서명에 씁니다. */
        FcmPushProvider( FcmPushSettings settings, INetSecurityProvider* pSecurity, HTTPClient* pHTTPClient );
        ~FcmPushProvider() override;

        string_view getProviderID() const override { return "fcm"; }
        void        send( uint64 deliveryID, const string& deviceToken, const string& locale, const PushNotificationMessage& message, int64 nowMs ) override;
        bool        supportsTopics() const override { return true; }
        void        sendToTopic( uint64 deliveryID, string_view topic, const PushNotificationMessage& message, int64 nowMs ) override;
        void        update( int64 nowMs ) override;
        int32       pollResults( vector<PushDeliveryResult>& outListResult ) override;

        /** @brief 메시지 본문(JSON)입니다. @p token 과 @p topic 중 하나만 채웁니다. 테스트가 봅니다. */
        static string makeMessageJSON( string_view token, string_view topic, const PushNotificationMessage& message );
        /** @brief 토픽 이름 규칙(`[A-Za-z0-9-_.~%]+`)입니다. */
        static bool isValidTopic( string_view topic );
        /** @brief 응답을 배달 결과로 바꿉니다(`Retry-After` 는 따로 읽는다). */
        static PushDeliveryStatus mapStatus( int32 statusCode, bool bTransportFailed, string_view body );

    private:
        struct Waiting
        {
            vector<uint8> _bodyBytes{};
            uint64        _deliveryID{ 0 };
        };

        void queueSend( uint64 deliveryID, string&& bodyJSON, int64 nowMs );
        void submitSend( uint64 deliveryID, const vector<uint8>& bodyBytes, int64 nowMs );
        /** @brief 토큰을 기다리던 배달을 새 토큰으로 보내거나(Granted) 결과로 끝냅니다. */
        void flushWaiting( ServiceAccountTokenSource::Outcome outcome, int64 nowMs );
        void finishSend( uint64 deliveryID, const HTTPClientResponse& response );
        void complete( uint64 deliveryID, PushDeliveryStatus status, int64 retryAfterMs = 0 );

        ServiceAccountTokenSource     _tokenSource;
        string                        _sendURL;
        vector<Waiting>               _listWaiting;
        vector<PushDeliveryResult>    _listDone;
        unordered_map<uint64, uint64> _mapRequestToDelivery;
        HTTPClient*                   _pHTTPClient;
        int64                         _requestTimeoutMs;
    };
} // namespace sw
