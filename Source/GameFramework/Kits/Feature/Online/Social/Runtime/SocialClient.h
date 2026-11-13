/**
 * @file SocialClient.h
 * @brief 친구 키트의 클라이언트입니다. 요청은 `call<SocialMethod::RequestFriend>( request, onReply )` 이고, 이 클래스는 알림을 쌓아 둡니다.
 * @details - 요청 · 응답 규칙은 `OnlineProtocolClient` 에 있다. 응답 타입은 메서드마다 따로이고 업무 결과는 `_reply._result`(도메인 값만), 전송 오류와 공통 범주는 `_errorCode` 에 있다.
 *          - 관계나 길드를 바꾸는 메서드는 표에 멱등 키가 필수로 적혀 있다. 다시 연결한 뒤 재전송해도 두 번 적용되지 않게 하려는 것이다.
 *          - 로그인한 뒤 `SocialMethod::ListLinks` 를 먼저 부른다. 서버가 그때 이 계정의 관계를 메모리에 로드해야 친구의 접속 상태 알림이 온다.
 *
 *          언리얼 Online Services 의 비동기 호출과 완료 델리게이트에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Event/ConsumerLock.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    /**
     * @class SocialClient
     * @brief 연결 하나의 친구와 길드 클라이언트입니다.
     */
    class SW_KIT_API SocialClient final : public OnlineProtocolClient<SocialClient, SocialProtocol>
    {
    public:
        SocialClient();

        /** @brief 알림 버퍼를 꺼낼 권한을 @p outLease 에 받습니다. 클라이언트마다 한 곳만 받을 수 있어 이미 다른 표가 있으면 false 입니다(알림을 서로 가져가는 일을 막는다). */
        [[nodiscard]] bool claimNotifications( ConsumerLease& outLease ) { return _notificationConsumerLock.claim( outLease ); }
        /** @brief 쌓인 알림을 꺼냅니다. @p lease 는 `claimNotifications` 로 받은 표여야 합니다(아니면 어서션이고 꺼내지 않습니다). */
        void pollNotifications( const ConsumerLease& lease, vector<SocialNotification>& outListNotification )
        {
            SW_ASSERT( _notificationConsumerLock.isHeldBy( lease ) );
            if ( _notificationConsumerLock.isHeldBy( lease ) )
                _notificationBuffer.moveAllTo( outListNotification );
        }

        // OnlineProtocolClient 의 훅
        void onPush( SocialMethod::Notification, const SocialNotification& body );

    private:
        ConsumerLock                    _notificationConsumerLock;
        EventBuffer<SocialNotification> _notificationBuffer;
    };
} // namespace sw
