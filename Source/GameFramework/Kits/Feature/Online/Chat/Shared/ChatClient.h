/**
 * @file ChatClient.h
 * @brief 채팅 클라이언트입니다. 입장, 퇴장, 말하기, 귓속말, 기록 요청을 보내고 메시지 알림(이벤트 버퍼)을 받습니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며 게임 스레드에서 씁니다.
 * @details - 요청 · 응답 규칙은 `OnlineKitClient` 에 있다. 공통 오류(로그인 없음, 전송)는 `_errorCode` 와 그에 대응하는 결과(`ChatProtocol::fromErrorCode`)로 온다.
 *          - 받은 메시지(채널과 귓속말)는 `drainMessages` 로 넘긴다. 서버 쪽 헤더(`Server/Chat/…`)는 include 하지 않는다. 클라이언트 Shipping 빌드에도 포함되기 때문이다.
 *
 *          언리얼 Online Services 의 비동기 호출과 완료 델리게이트에 해당한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineKitClient.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatProtocol.h"

namespace sw
{
    /**
     * @class ChatClient
     * @brief 연결 하나의 채팅 클라이언트입니다.
     */
    class SW_KIT_API ChatClient final : public OnlineKitClient<ChatProtocol, ChatReply>
    {
    public:
        ChatClient();

        /** @brief 요청 id 를 반환합니다. 응답의 `_requestID` 와 같습니다. */
        uint64 join( string_view channelID, const ReplyDelegate& onReply );
        uint64 leave( string_view channelID, const ReplyDelegate& onReply );
        uint64 send( string_view channelID, string_view text, const ReplyDelegate& onReply );
        uint64 whisper( AccountID recipientID, string_view text, const ReplyDelegate& onReply );
        /** @brief @p channelID 는 채널 id 나 귓속말 기록 키(`ChatChannelID::makeWhisper`)이고, @p cursor 는 앞 응답의 `_nextCursor` 입니다. 처음에는 빈 문자열을 넘깁니다. */
        uint64 requestHistory( string_view channelID, string_view cursor, int32 maxCount, const ReplyDelegate& onReply );

        /** @brief 받은 메시지(채널과 귓속말)를 @p outListMessage 끝에 넘깁니다. */
        void drainMessages( vector<ChatMessage>& outListMessage ) { _messageBuffer.drainTo( outListMessage ); }

        // IOnlineClientService
        const OnlineProtocolEntry& getProtocol() const override { return ChatProtocol::kOnlineEntry; }
        void                       onServicePush( uint16 kind, BitReader& body ) override;

    private:
        EventBuffer<ChatMessage> _messageBuffer;
    };
} // namespace sw
