/**
 * @file ChatLogModel.h
 * @brief 채팅 화면이 읽는 받은 메시지 기록과 마지막 보내기 결과입니다. `ChatClient::drainMessages` 로 꺼낸 메시지를 시간 순으로 쌓고(`kMaxLines` 줄까지, 넘치면 가장 오래된 줄부터 버린다),
 *        `ChatClient::send` 의 결과를 보관합니다(온라인 키트 U1).
 * @details 화면(`ChatScreen`)은 요청을 직접 보내지 않고 이 객체로 보낸다 — 응답 델리게이트가 화면 수명에 묶이지 않게 한다. `update` 가 클라이언트의 메시지 버퍼를 꺼내므로 한 클라이언트에 모델 하나만 붙을 수 있다(`initialize` 가 false).
 *          채널에 들어가는 일(`ChatClient::join`)은 부르는 게임의 몫이다 — 길드와 파티 채널은 서버가 멤버로 넣고, 서버가 보낸 채널의 메시지는 이 기록에 채널 id 와 함께 들어온다.
 *          보낸 사람도 자기 메시지를 채널 알림으로 받으므로 보내기가 성공해도 기록에 따로 더하지 않는다(두 번 보이지 않게 한다).
 *          이 객체는 클라이언트와 함께(또는 먼저 요청이 끝난 뒤) 소멸해야 한다. 기록이나 결과가 바뀔 때마다 `getRevision` 이 올라가 화면이 다시 그릴지 판단한다.
 */
#pragma once
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatClient.h"

namespace sw
{
    /**
     * @class ChatLogModel
     * @brief 받은 메시지 기록 한 벌과 마지막 보내기 결과입니다.
     */
    class SW_KIT_API ChatLogModel
    {
    public:
        static constexpr size_t kMaxLines = 200; ///< 기록에 남기는 줄 수의 상한이다

        ChatLogModel();
        ChatLogModel( const ChatLogModel& )            = delete; // 응답 델리게이트가 this 를 가리킨다
        ChatLogModel& operator=( const ChatLogModel& ) = delete;

        /**
         * @brief 클라이언트를 빌려 쓰고 그 메시지 버퍼를 꺼낼 권한을 받습니다(`ChatClient` 가 서비스 클라이언트에 등록 · 초기화된 것).
         * @details 이미 다른 모델이 이 클라이언트의 버퍼를 꺼내고 있으면 false 이고 클라이언트를 붙이지 않습니다(두 모델이 메시지를 서로 가져가는 것을 막는다). nullptr 이면 떼어냅니다.
         */
        [[nodiscard]] bool initialize( ChatClient* pClient );
        /** @brief 클라이언트가 받은 메시지를 꺼내 기록에 더합니다. 화면이 틱마다 부릅니다. */
        void update();
        /**
         * @brief @p channelID 에 @p text 를 보냅니다. 요청을 보냈으면 true 이고 결과는 `getLastResult` 로 옵니다.
         * @details 글이 비었으면 보내지 않고 `Invalid`, 클라이언트가 없으면 `Unavailable` 로 끝나고 false 입니다 — 둘 다 `getLastResult` 에 남아 화면이 이유를 보인다.
         */
        [[nodiscard]] bool send( string_view channelID, string_view text );

        // 클라이언트의 메시지와 응답이 들어오는 길입니다. 시험도 같은 길로 값을 넣습니다.
        void appendMessage( const ChatMessage& message );
        void onSendReply( const ChatClient::Reply& reply );

        const deque<ChatMessage>& getLines() const { return _listLine; }
        ChatResult                getLastResult() const { return _lastResult; }
        int64                     getRetryAfterMs() const { return _retryAfterMs; }
        uint64                    getRevision() const { return _revision; }

    private:
        deque<ChatMessage>  _listLine;
        DrainLease          _drainLease;
        vector<ChatMessage> _listDrained;
        ChatClient*         _pClient;
        uint64              _revision;
        int64               _retryAfterMs;
        ChatResult          _lastResult;
    };
} // namespace sw
