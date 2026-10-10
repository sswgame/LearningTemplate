/**
 * @file ChatService.h
 * @brief 채팅 로직 — 채널 회원(이 서버) · 말하기(제재 → 도배 → 거르개 → 전달 · 버스 · 기록) · 귓속말(이 서버 · 다른 서버) · 기록 읽기입니다. 전송을 모른다(서비스 스레드 하나).
 * @details - 결과는 요청 꼬리표와 함께 `drainCompletions`, 받을 이에게 갈 메시지는 `drainDeliveries`, 버스 구독 바뀜은 `drainBusTopicChanges`(바인딩이 호스트에 구독 · 해지).
 *            다른 서버가 낸 버스 메시지는 바인딩이 `handleBusMessage` 로 넘긴다.
 *          - 클라이언트가 들어가고 나오는 채널은 `world.` · `custom.` 만. `guild.` · `party.` 는 서버 시스템이 `addMember` · `removeMember` 로 넣는다(게임 조립이 친구 ·
 *            매칭 키트의 사건을 잇는다 — 키트끼리 include 하지 않는다).
 *          - 서버 여럿: 채널에 이 서버 회원이 있는 동안만 `chat.channel.<id>` 를 구독하고, 말하기는 이 서버 회원에게 바로 + 버스 발행(최대 한 번 — 놓친 것은 기록에 있다).
 *            귓속말은 상대가 이 서버면 바로, 아니면 접속 상태(`IAccountPresence`)로 서버를 찾아 `chat.server.<서버>` 로. 차단은 받는 쪽 서버가 보고 조용히 버린다
 *            — 보낸 이에게는 Ok(차단 여부를 드러내지 않는다).
 *          - 채팅 금지: 제재 레코드를 계정의 첫 말하기 때 읽고 `_sanctionCacheMs` 묵힌다. 버스 `sanction.changed` 가 오면 그 계정의 묵힌 값을 버린다.
 *            첫 읽기 전의 말하기는 `_maxPendingSendPerMember` 까지 줄 세웠다가 읽기가 끝나면 처리한다.
 *          - 기록: 영속 표 `chat_history`, 키 `<채널 id>/<보낸 시각 16 진>.<서버 16 진>.<순번 16 진>`(키 순서 = 시간 순). 틱마다 쌓인 것을 트랜잭션 하나로 쓰고
 *            최선 노력이다(실패는 경고 뒤 버린다). 순번이 `_historyTrimEvery` 의 배수인 메시지의 채널은 보존 기간이 지난 것을 64 개까지 지운다.
 *          - `shutdown` 은 맡긴 저장 일을 거두고(5 초까지) 접속 상태 찾기를 취소한다 — 호스트(접속 상태 · 저장소)보다 먼저든 늦게든 내려도 된다.
 *          Nakama 채널 메시지(DB 기록 + 노드 사이 클러스터 버스)와 같은 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatSpamGuard.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatWordFilter.h"
#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatProtocol.h"
#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatTypes.h"

namespace sw
{
    class IServerBus;
    class IServiceStore;

    /**
     * @class IChatPolicy
     * @brief 게임 조립이 이어 주는 정책입니다 — 차단 표는 친구 키트(GF_Social)의 것이다.
     */
    class SW_GF_API IChatPolicy
    {
    public:
        IChatPolicy()          = default;
        virtual ~IChatPolicy() = default;

        IChatPolicy( const IChatPolicy& )            = delete;
        IChatPolicy& operator=( const IChatPolicy& ) = delete;

        /** @brief @p recipientID 가 @p senderID 를 막았는가입니다(받는 이가 이 서버에 붙어 있을 때 불린다 — 메모리 조회여야 한다). */
        virtual bool isBlocked( AccountID recipientID, AccountID senderID ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 채팅 설정입니다. */
    struct ChatSettings
    {
        ChatSpamSettings _spam{};
        string           _bannedWordsPath{}; ///< 비면 거르지 않는다(서버 조립은 `Config/Server/chat_banned_words.txt`)
        ChatFilterMode   _filterMode{ ChatFilterMode::Mask };
        int64            _sanctionCacheMs{ 60 * 1000 };
        int64            _historyRetentionMs{ 30ll * 24 * 60 * 60 * 1000 };
        int32            _historyFlushMax{ 60 };           ///< 트랜잭션 하나의 쓰기(상한 64 안)
        int32            _historyTrimEvery{ 64 };          ///< 순번이 이 수의 배수인 메시지의 채널을 정리한다
        int32            _maxQueuedHistory{ 4096 };        ///< 저장소가 늦을 때 쌓아 둘 기록 — 넘으면 오래된 것부터 버린다
        int32            _maxPendingSendPerMember{ 4 };    ///< 제재 첫 읽기를 기다리는 말하기
        int64            _pendingSendRetryAfterMs{ 1000 }; ///< 그 줄이 찼을 때 알려 줄 기다릴 ms
    };
} // namespace sw

namespace sw
{
    /** @brief 빌려 쓰는 것들입니다(서비스보다 오래 산다). */
    struct ChatServiceDependencies
    {
        IServiceStore*           _pStore{ nullptr };     ///< 필수(기록 · 제재)
        IServerBus*              _pBus{ nullptr };       ///< 서버 하나면 null
        IAccountPresence*        _pPresence{ nullptr };  ///< 서버 하나면 null(다른 서버 귓속말 없음)
        const IAccountDirectory* _pDirectory{ nullptr }; ///< 필수(표시 이름 · 이 서버에 있나)
        const IChatPolicy*       _pPolicy{ nullptr };    ///< null 이면 차단 없음
        uint64                   _serverID{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 결과입니다(`_method` 는 `ChatMethod::k*`). */
    struct ChatCompletion
    {
        ChatReply _reply{};
        uint64    _requestTag{ 0 };
        uint16    _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 받을 이(이 서버에 붙은 계정) 하나에게 갈 메시지입니다. */
    struct ChatDelivery
    {
        ChatMessage _message{};
        AccountID   _recipientID{ kInvalidAccountID };
    };
} // namespace sw

namespace sw
{
    /** @brief 버스 구독 바뀜입니다. */
    struct ChatBusTopicChange
    {
        string _topic{};
        uint8  _bSubscribe{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ChatService
     * @brief 채팅 로직입니다.
     */
    class SW_GF_API ChatService
    {
    public:
        ChatService();
        ~ChatService();

        ChatService( const ChatService& )            = delete;
        ChatService& operator=( const ChatService& ) = delete;

        /** @brief 저장소 · 계정 창구가 없으면 false 입니다. 버스가 있으면 이 서버 주제 · 제재 주제 구독을 낸다. */
        [[nodiscard]] bool initialize( const ChatServiceDependencies& dependencies, const ChatSettings& settings );
        /** @brief 맡긴 저장 일이 끝날 때까지 저장소를 거둡니다(상한 5 초) — 저장소를 내리기 전에. */
        void shutdown();

        /** @brief 쌓인 기록을 씁니다. */
        void tick( int64 nowMs );

        // 클라이언트 요청 — 결과는 drainCompletions(같은 꼬리표)
        void joinChannel( AccountID accountID, string_view channelID, int64 nowMs, uint64 requestTag );
        void leaveChannel( AccountID accountID, string_view channelID, uint64 requestTag );
        void sendMessage( AccountID accountID, string_view channelID, string_view text, int64 nowMs, uint64 requestTag );
        void sendWhisper( AccountID accountID, AccountID recipientID, string_view text, int64 nowMs, uint64 requestTag );
        void readHistory( AccountID accountID, string_view channelID, string_view cursor, int32 maxCount, uint64 requestTag );

        // 서버 시스템(길드 · 파티 · GM) — 완료 없음
        [[nodiscard]] ChatResult addMember( AccountID accountID, string_view channelID, int64 nowMs );
        void                     removeMember( AccountID accountID, string_view channelID );
        /** @brief 제재가 바뀌었다(GM) — 다음 말하기 전에 다시 읽는다. */
        void refreshSanction( AccountID accountID );
        /** @brief 계정이 이 서버를 떠났다 — 모든 채널에서 뺀다. */
        void removeAccount( AccountID accountID );

        /** @brief 구독한 버스 메시지(바인딩이 넘긴다 — 자기 서버가 낸 채팅은 메시지의 서버 id 로 거른다, 제재 알림은 자기 서버 것도 본다). */
        void handleBusMessage( string_view topic, const vector<uint8>& bytes );

        void drainCompletions( vector<ChatCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainDeliveries( vector<ChatDelivery>& outListDelivery ) { _deliveryBuffer.drainTo( outListDelivery ); }
        void drainBusTopicChanges( vector<ChatBusTopicChange>& outListChange ) { _busTopicBuffer.drainTo( outListChange ); }

        int32 getLocalMemberCount( string_view channelID ) const;
        /** @brief 맡겨 두고 끝나지 않은 일(저장 · 접속 상태 찾기) 수입니다. */
        int32 getPendingCount() const { return _pendingStoreCount + static_cast<int32>( _mapPresenceRequestToWhisper.size() ); }
        int32 getQueuedHistoryCount() const { return static_cast<int32>( _listHistoryQueue.size() ); }

        /** @brief 저장 일의 `complete` 가 부릅니다(키트 안). */
        void applySanction( AccountID accountID, int64 muteUntilMs, bool bReadOk, int64 nowMs );
        void applyHistoryRead( uint64 requestTag, ChatResult result, vector<ChatMessage>&& listMessage, string&& nextCursor );
        void applyHistoryWrite( bool bCommitted, int32 messageCount );

    private:
        struct PendingSend
        {
            string    _channelID{};
            string    _text{};
            uint64    _requestTag{ 0 };
            int64     _nowMs{ 0 };
            AccountID _recipientID{ kInvalidAccountID }; ///< 귓속말이면
        };

        struct Member
        {
            vector<string>      _listChannel{};
            vector<PendingSend> _listPendingSend{};
            string              _displayName{};
            int64               _muteUntilMs{ 0 };
            int64               _sanctionReadMs{ -1 }; ///< −1 = 아직 읽지 않음(또는 GM 이 바꿔 버렸다)
            uint8               _bSanctionReading{ SW_FALSE };
        };

        struct PendingWhisper
        {
            ChatMessage _message{};
            uint64      _requestTag{ 0 };
        };

        void       onPresenceFound( const AccountPresenceResult& result );
        Member*    ensureMember( AccountID accountID, int64 nowMs );
        bool       isSanctionFresh( const Member& member, int64 nowMs ) const;
        void       startSanctionRead( AccountID accountID, Member& member, int64 nowMs );
        void       submitSend( AccountID accountID, PendingSend&& send );
        ChatResult prepareText( AccountID accountID, Member& member, string_view text, int64 nowMs, string& outText, int64& outRetryAfterMs );
        void       processSend( AccountID accountID, Member& member, const PendingSend& send );
        void       publishMessage( const string& topic, const ChatMessage& message );
        void       deliverToChannel( const ChatMessage& message );
        void       deliverWhisperLocally( const ChatMessage& message );
        void       queueHistory( const ChatMessage& message );
        void       completeMessage( uint16 method, uint64 requestTag, ChatMessage&& message );
        void       completeSimple( uint16 method, uint64 requestTag, ChatResult result, int64 retryAfterMs = 0 );
        void       addLocalMember( AccountID accountID, Member& member, const string& channelID );
        void       removeLocalMember( AccountID accountID, Member& member, const string& channelID );
        bool       isWhisperParticipant( AccountID accountID, string_view channelID ) const;

        unordered_map<AccountID, Member>         _mapAccountToMember;
        unordered_map<string, vector<AccountID>> _mapChannelToMember;
        unordered_map<uint64, PendingWhisper>    _mapPresenceRequestToWhisper;
        vector<ChatMessage>                      _listHistoryQueue;
        EventBuffer<ChatCompletion>              _completionBuffer;
        EventBuffer<ChatDelivery>                _deliveryBuffer;
        EventBuffer<ChatBusTopicChange>          _busTopicBuffer;
        ChatWordFilter                           _wordFilter;
        ChatSpamGuard                            _spamGuard;
        ChatSettings                             _settings;
        ChatServiceDependencies                  _dependencies;
        string                                   _serverTopic;
        uint32                                   _sequence;
        int32                                    _pendingStoreCount;
        uint8                                    _bHistoryWriting;
    };
} // namespace sw
