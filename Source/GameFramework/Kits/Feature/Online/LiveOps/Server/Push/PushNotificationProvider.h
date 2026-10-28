/**
 * @file PushNotificationProvider.h
 * @brief 푸시 알림 제공자 계약입니다. 기기 토큰으로 보내고 결과는 나중에 회수합니다. 구현은 `Provider/<제품>/` 안에 있으며 제품 이름은 그 폴더 밖에 쓰지 않습니다.
 * @details `send` 와 `sendToTopic` 은 서비스 스레드를 막지 않는다. 실제 제공자는 자기 HTTP 클라이언트(기반 계층의 `Online/HTTP`), 인증, 재시도 상태를 가진다.
 *          발송기가 서비스 스레드의 틱마다 `update` 로 제공자를 돌리고 `pollResults` 로 끝난 배달을 회수한다. 시각(@p nowMs)은 서비스 시각(UTC 밀리초)이다.
 *          제공자는 `Provider/Fake/`(테스트와 개발)와 `Provider/Fcm/` 이다. 자격(서비스 계정 키)은 운영 비밀(환경 변수)에서만 읽는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsTypes.h"

namespace sw
{
    /** @brief 배달 하나의 결과입니다. */
    enum class PushDeliveryStatus : uint8
    {
        Delivered = 0,
        InvalidToken, ///< 앱이 지워졌거나 토큰이 바뀌었다. 기기를 지운다
        Transient,    ///< 네트워크나 제공자 5xx 오류이다. 백오프 뒤 다시 보낸다
        RateLimited,  ///< 제공자가 늦추라고 했다. 기다릴 시간은 `_retryAfterMs` 이다
        Rejected      ///< 메시지가 틀렸거나 인증에 실패했다. 다시 보내지 않고 로그를 남긴다
    };
} // namespace sw

namespace sw
{
    /** @brief 제공자가 돌려주는 결과 하나입니다. */
    struct PushDeliveryResult
    {
        uint64             _deliveryID{ 0 };
        int64              _retryAfterMs{ 0 };
        PushDeliveryStatus _status{ PushDeliveryStatus::Delivered };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IPushNotificationProvider
     * @brief 푸시 제공자입니다. 서비스 스레드 하나에서 씁니다.
     */
    class IPushNotificationProvider
    {
    public:
        IPushNotificationProvider()                                                                                                                                    = default;
        IPushNotificationProvider( const IPushNotificationProvider& )                                                                                                  = delete;
        IPushNotificationProvider& operator=( const IPushNotificationProvider& )                                                                                       = delete;
        virtual ~IPushNotificationProvider()                                                                                                                           = default;
        virtual string_view getProviderID() const                                                                                                                      = 0;
        virtual void        send( uint64 deliveryID, const string& deviceToken, const string& locale, const PushNotificationMessage& message, UnixMilliseconds nowMs ) = 0;
        /** @brief 이 제공자가 토픽 발송(`sendToTopic`)을 하면 true 입니다. 발송기는 토픽 발송을 토픽을 아는 제공자에게만 맡깁니다. */
        virtual bool supportsTopics() const { return false; }
        /**
         * @brief 토픽을 구독한 모든 기기에 한 번에 보냅니다(전체 대상 알림 — 계정마다 보내지 않는다). 토픽 이름 규칙은 제공자가 정하며, 틀리면 `Rejected` 로 끝납니다.
         * @details `supportsTopics` 가 true 인 제공자만 구현한다. 결과는 `pollResults` 로 같은 배달 id 에 돌려준다(`InvalidToken` 은 토픽에 없다).
         */
        virtual void sendToTopic( uint64 deliveryID, string_view topic, const PushNotificationMessage& message, UnixMilliseconds nowMs )
        {
            (void)deliveryID;
            (void)topic;
            (void)message;
            (void)nowMs;
        }
        /** @brief 서비스 틱마다 한 번 불립니다. 제공자의 HTTP 전송과 인증 갱신을 돌립니다. */
        virtual void update( UnixMilliseconds nowMs ) { (void)nowMs; }
        /** @brief 끝난 배달의 결과를 @p outListResult 뒤에 붙이고 붙인 개수를 반환합니다. */
        virtual int32 pollResults( vector<PushDeliveryResult>& outListResult ) = 0;
    };
} // namespace sw
