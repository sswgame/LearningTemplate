/**
 * @file ChatClient.h
 * @brief 채팅 클라이언트입니다. 요청은 `call<ChatMethod::Send>( request, onReply )` 이고, 이 클래스는 메시지 알림(이벤트 버퍼)을 받습니다. 게임 스레드에서 씁니다.
 * @details - 요청 · 응답 규칙은 `OnlineProtocolClient` 에 있다. 응답은 메서드마다 따로이고 업무 결과는 `_reply._result`(도메인 값만), 공통 범주(로그인 없음, 전송)는 `_errorCode` 로 온다.
 *          - 받은 메시지(채널과 귓속말)는 `pollMessages` 로 넘긴다. 서버 쪽 헤더(`Server/Chat/…`)는 include 하지 않는다. 클라이언트 Shipping 빌드에도 포함되기 때문이다.
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
#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    /**
     * @class ChatClient
     * @brief 연결 하나의 채팅 클라이언트입니다.
     */
    class SW_KIT_API ChatClient final : public OnlineProtocolClient<ChatClient, ChatProtocol>
    {
    public:
        ChatClient();

        /** @brief 메시지 버퍼를 꺼낼 권한을 @p outLease 에 받습니다. 클라이언트마다 한 곳만 받을 수 있어 이미 다른 표가 있으면 false 입니다(메시지를 서로 가져가는 일을 막는다). */
        [[nodiscard]] bool claimMessages( ConsumerLease& outLease ) { return _messageConsumerLock.claim( outLease ); }
        /** @brief 받은 메시지(채널과 귓속말)를 @p outListMessage 끝에 넘깁니다. @p lease 는 `claimMessages` 로 받은 표여야 합니다(아니면 어서션이고 꺼내지 않습니다). */
        void pollMessages( const ConsumerLease& lease, vector<ChatMessage>& outListMessage )
        {
            SW_ASSERT( _messageConsumerLock.isHeldBy( lease ) );
            if ( _messageConsumerLock.isHeldBy( lease ) )
                _messageBuffer.moveAllTo( outListMessage );
        }

        // OnlineProtocolClient 의 훅
        void onPush( ChatMethod::Message, const ChatMessage& body );

    private:
        ConsumerLock             _messageConsumerLock;
        EventBuffer<ChatMessage> _messageBuffer;
    };
} // namespace sw
