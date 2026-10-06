/**
 * @file ChatClient.h
 * @brief 채팅 클라이언트 — 들어가기 · 나가기 · 말하기 · 귓속말 · 기록 요청과 메시지 알림(사건 버퍼)입니다. `OnlineServiceClient` 에 올리는 `IOnlineClientService`(게임 스레드).
 * @details - 요청마다 완료 델리게이트를 한 번 부른다(`OnlineServiceClient::tick` 스레드 — 보내지 못하면 그 자리에서 Unavailable). 결과는 `ChatClientReply::_reply._result`,
 *            공통 오류(로그인 없음 · 전송)는 `_errorCode` 와 그 결과(`ChatProtocol::fromErrorCode`).
 *          - 받은 메시지(채널 · 귓속말)는 `drainMessages` 로 넘긴다. 서버 쪽 헤더(`Server/Chat/…`)는 include 하지 않는다 — 클라이언트 Shipping 에도 든다.
 *          언리얼 Online Services 의 비동기 호출 + 완료 델리게이트와 같은 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Chat/ChatProtocol.h"

namespace sw
{
    /** @brief 채팅 클라이언트 응답 하나입니다. */
    struct ChatClientReply
    {
        ChatReply _reply{};
        uint64    _requestId{ 0 };
        uint16    _method{ 0 };
        uint16    _errorCode{ 0 }; ///< 전송 · 공통 오류(`OnlineError`) — 0 이 아니면 `_reply._result` 는 그 코드의 결과
    };
} // namespace sw

namespace sw
{
    /**
     * @class ChatClient
     * @brief 연결 하나의 채팅 창구입니다.
     */
    class SW_GF_API ChatClient final : public IOnlineClientService
    {
    public:
        using ReplyDelegate = Delegate<void( const ChatClientReply& )>;

        ChatClient();

        /** @brief @p pClient 는 빌려 쓴다 — `registerClientService( this )` 는 부르는 쪽이(초기화 전에). */
        void initialize( OnlineServiceClient* pClient );

        /** @brief 요청 id 입니다(응답의 `_requestId`). */
        uint64 join( string_view channelId, const ReplyDelegate& onReply );
        uint64 leave( string_view channelId, const ReplyDelegate& onReply );
        uint64 send( string_view channelId, string_view text, const ReplyDelegate& onReply );
        uint64 whisper( AccountId recipientId, string_view text, const ReplyDelegate& onReply );
        /** @brief @p channelId 는 채널 id 또는 귓속말 기록 키(`ChatChannelId::makeWhisper`), @p cursor 는 앞 응답의 `_nextCursor`(처음은 빈 글). */
        uint64 requestHistory( string_view channelId, string_view cursor, int32 maxCount, const ReplyDelegate& onReply );

        /** @brief 받은 메시지(채널 · 귓속말)를 @p outListMessage 끝에 넘깁니다. */
        void drainMessages( vector<ChatMessage>& outListMessage ) { _messageBuffer.drainTo( outListMessage ); }

        // IOnlineClientService
        uint16 getMethodRange() const override { return OnlineMethodRange::kChat; }
        uint32 getProtocolVersion() const override { return ChatProtocol::kVersion; }
        void   onServicePush( uint16 kind, BitReader& body ) override;

    private:
        struct PendingCall
        {
            ReplyDelegate _onReply{};
            uint16        _method{ 0 };
        };

        uint64 sendCall( uint16 method, const BitWriter& body, const ReplyDelegate& onReply );
        void   onResponse( const OnlineResponse& response );

        ServiceClientCallTable<PendingCall> _callTable;
        EventBuffer<ChatMessage>            _messageBuffer;
        OnlineServiceClient*                _pClient;
    };
} // namespace sw
