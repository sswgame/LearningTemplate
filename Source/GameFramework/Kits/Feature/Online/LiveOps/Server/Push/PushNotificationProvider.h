/**
 * @file PushNotificationProvider.h
 * @brief 푸시 알림 제공자 계약입니다. 기기 토큰으로 보내고 결과는 나중에 회수합니다. 구현은 `Provider/<제품>/` 안에 있으며 제품 이름은 그 폴더 밖에 쓰지 않습니다.
 * @details `send` 는 서비스 스레드를 막지 않는다. 실제 제공자는 자기 워커, HTTP/2 연결, 인증을 가진다. `pollResults` 는 발송기가 서비스 스레드의 틱마다 부른다.
 *          지금은 `Provider/Fake/` 하나뿐이다. 실제 제공자는 공통 HTTPS 클라이언트(기반 계층의 `Online/HTTP`) 위에 운영 비밀(인증서, 서비스 계정 키)과 함께 추가할 예정이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsTypes.h"

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
        IPushNotificationProvider()                                                                                                            = default;
        IPushNotificationProvider( const IPushNotificationProvider& )                                                                          = delete;
        IPushNotificationProvider& operator=( const IPushNotificationProvider& )                                                               = delete;
        virtual ~IPushNotificationProvider()                                                                                                   = default;
        virtual string_view getProviderID() const                                                                                              = 0;
        virtual void        send( uint64 deliveryID, const string& deviceToken, const string& locale, const PushNotificationMessage& message ) = 0;
        /** @brief 끝난 배달의 결과를 @p outListResult 뒤에 붙이고 붙인 개수를 반환합니다. */
        virtual int32 pollResults( vector<PushDeliveryResult>& outListResult ) = 0;
    };
} // namespace sw
