/**
 * @file ChatService.h
 * @brief 채팅 로직입니다. 채널 멤버(이 서버), 말하기, 귓속말, 기록 읽기를 처리합니다. 말하기는 제재, 도배, 필터를 거친 뒤 전달, 버스 발행, 기록으로 이어집니다. 전송은 모르며 서비스 스레드 하나에서 씁니다.
 * @details - 결과는 요청 태그와 함께 `drainCompletions` 로, 받을 사람에게 갈 메시지는 `drainDeliveries` 로, 버스 구독 변경은 `drainBusTopicChanges` 로 회수한다. 구독과 해지는 바인딩이 호스트에 한다.
 *            다른 서버가 낸 버스 메시지는 바인딩이 `handleBusMessage` 로 넘긴다.
 *          - 클라이언트가 들어가고 나올 수 있는 채널은 `world.` 와 `custom.` 뿐이다. `guild.` 와 `party.` 는 서버 시스템이 `addMember` 와 `removeMember` 로 넣는다.
 *            게임 조립 단계가 친구와 매칭 키트의 이벤트를 연결하며, 키트끼리는 include 하지 않는다.
 *          - 서버가 여러 대일 때는 채널에 이 서버의 멤버가 있는 동안만 `chat.channel.<id>` 를 구독한다. 말하기는 이 서버의 멤버에게 바로 전달하고 버스에도 발행한다.
 *            버스는 최대 한 번 배달하며 놓친 메시지는 기록에 있다.
 *            귓속말은 상대가 이 서버에 있으면 바로 전달하고, 아니면 접속 상태(`IAccountPresence`)로 서버를 찾아 `chat.server.<서버>` 로 보낸다.
 *            차단은 받는 쪽 서버가 확인해 조용히 버린다. 보낸 사람에게는 Ok 를 반환해서 차단 여부를 드러내지 않는다.
 *          - 채팅 금지는 제재 레코드를 계정의 첫 말하기에서 읽고 `_sanctionCacheMs` 동안 캐시한다. 버스 `sanction.changed` 가 오면 그 계정의 캐시를 버린다.
 *            첫 읽기 전의 말하기는 `_maxPendingSendPerMember` 개까지 대기시켰다가 읽기가 끝나면 처리한다.
 *          - 기록은 영속 테이블 `chat_history` 에 둔다. 키는 `<채널 id>/<보낸 시각 16진>.<서버 16진>.<순번 16진>` 이며 키 순서가 시간 순서이다.
 *            틱마다 쌓인 기록을 트랜잭션 하나로 쓰며 최선 노력이다. 실패하면 경고를 남기고 버린다.
 *            순번이 `_historyTrimEvery` 의 배수인 메시지의 채널은 보존 기간이 지난 기록을 64개까지 지운다.
 *          - `shutdown` 은 맡긴 저장 작업을 회수하고(최대 5초) 접속 상태 찾기를 취소한다. 호스트(접속 상태, 저장소)보다 먼저 종료해도 늦게 종료해도 된다.
 *
 *          Nakama 의 채널 메시지(DB 기록과 노드 간 클러스터 버스)와 같은 구조이다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatSpamGuard.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatWordFilter.h"
#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatProtocol.h"
#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatTypes.h"

namespace sw
{
    struct AccountPresenceResult;

    class IAccountPresence;
    class IServerBus;
    class IServiceStore;

    /**
     * @class IChatPolicy
     * @brief 서버 조립이 연결해 주는 정책입니다. 차단 테이블은 친구 키트(GF_Social)의 것이며, 채팅 키트 조립(`ChatOnlineServerKit`)이 기반의 `IAccountBlockList` 로 받습니다.
     */
    class IChatPolicy
    {
    public:
        IChatPolicy()          = default;
        virtual ~IChatPolicy() = default;

        IChatPolicy( const IChatPolicy& )            = delete;
        IChatPolicy& operator=( const IChatPolicy& ) = delete;

        /** @brief @p recipientID 가 @p senderID 를 막았으면 true 입니다. 받는 사람이 이 서버에 접속해 있을 때 불리므로 메모리 조회여야 합니다. */
        virtual bool isBlocked( AccountID recipientID, AccountID senderID ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 채팅 설정입니다. */
    struct ChatSettings
    {
        ChatSpamSettings _spam{};
        string           _bannedWordsPath{}; ///< 비면 거르지 않는다. 서버 조립 단계는 `Config/Server/chat_banned_words.txt` 를 쓴다
        ChatFilterMode   _filterMode{ ChatFilterMode::Mask };
        int64            _sanctionCacheMs{ 60 * constant::kMillisecondsPerSecond };
        int64            _historyRetentionMs{ 30 * constant::kSecondsPerDay * constant::kMillisecondsPerSecond };
        int32            _historyFlushMax{ 60 };           ///< 트랜잭션 하나에 쓰는 기록 수이다. 상한 64 안이다
        int32            _historyTrimEvery{ 64 };          ///< 순번이 이 수의 배수인 메시지의 채널을 정리한다
        int32            _maxQueuedHistory{ 4096 };        ///< 저장소가 늦을 때 쌓아 둘 기록 수이다. 넘으면 오래된 것부터 버린다
        int32            _maxPendingSendPerMember{ 4 };    ///< 제재 첫 읽기를 기다리는 말하기 요청의 상한이다
        int64            _pendingSendRetryAfterMs{ 1000 }; ///< 대기 큐가 찼을 때 클라이언트에 알려 줄 기다릴 시간(ms)이다
    };
} // namespace sw

namespace sw
{
    /** @brief 빌려 쓰는 객체들입니다. 서비스보다 오래 살아야 합니다. */
    struct ChatServiceDependencies
    {
        IServiceStore*           _pStore{ nullptr };     ///< 필수이다. 기록과 제재에 쓴다
        IServerBus*              _pBus{ nullptr };       ///< 서버가 하나면 null 이다
        IAccountPresence*        _pPresence{ nullptr };  ///< 서버가 하나면 null 이며 다른 서버로 가는 귓속말이 없다
        const IAccountDirectory* _pDirectory{ nullptr }; ///< 필수이다. 표시 이름과 이 서버에 있는지를 조회한다
        const IChatPolicy*       _pPolicy{ nullptr };    ///< null 이면 차단을 쓰지 않는다
        uint64                   _serverID{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 결과입니다. `_method` 는 `ChatMethod::k*` 값입니다. */
    struct ChatCompletion
    {
        ChatReply _reply{};
        uint64    _requestTag{ 0 };
        uint16    _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 받을 사람 하나에게 갈 메시지입니다. 받을 사람은 이 서버에 접속한 계정입니다. */
    struct ChatDelivery
    {
        ChatMessage _message{};
        AccountID   _recipientID{ kInvalidAccountID };
    };
} // namespace sw

namespace sw
{
    /** @brief 버스 구독 변경입니다. */
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
     * @brief 채팅 로직 서비스입니다.
     */
    class SW_KIT_SERVER_API ChatService
    {
    public:
        ChatService();
        ~ChatService();

        ChatService( const ChatService& )            = delete;
        ChatService& operator=( const ChatService& ) = delete;

        /** @brief 저장소나 계정 디렉터리가 없으면 false 를 반환합니다. 버스가 있으면 이 서버의 주제와 제재 주제의 구독을 요청합니다. */
        [[nodiscard]] bool initialize( const ChatServiceDependencies& dependencies, const ChatSettings& settings );
        /** @brief 맡긴 저장 작업이 끝날 때까지 저장소를 회수합니다. 상한은 5초이며 저장소를 종료하기 전에 부릅니다. */
        void shutdown();

        /** @brief 쌓인 기록을 저장소에 씁니다. */
        void tick( int64 nowMs );

        // 클라이언트 요청이다. 결과는 같은 요청 태그로 drainCompletions 에서 받는다
        void joinChannel( AccountID accountID, string_view channelID, int64 nowMs, uint64 requestTag );
        void leaveChannel( AccountID accountID, string_view channelID, uint64 requestTag );
        void sendMessage( AccountID accountID, string_view channelID, string_view text, int64 nowMs, uint64 requestTag );
        void sendWhisper( AccountID accountID, AccountID recipientID, string_view text, int64 nowMs, uint64 requestTag );
        void readHistory( AccountID accountID, string_view channelID, string_view cursor, int32 maxCount, uint64 requestTag );

        // 서버 시스템(길드, 파티, GM)이 부르는 함수이며 완료는 없다
        [[nodiscard]] ChatResult addMember( AccountID accountID, string_view channelID, int64 nowMs );
        void                     removeMember( AccountID accountID, string_view channelID );
        /** @brief GM 이 제재를 바꿨을 때 부릅니다. 다음 말하기 전에 다시 읽습니다. */
        void refreshSanction( AccountID accountID );
        /** @brief 계정이 이 서버를 떠났을 때 부릅니다. 모든 채널에서 뺍니다. */
        void removeAccount( AccountID accountID );

        /** @brief 구독한 버스 메시지를 처리합니다. 바인딩이 넘기며, 자기 서버가 낸 채팅은 메시지의 서버 id 로 거릅니다. 제재 알림은 자기 서버가 낸 것도 처리합니다. */
        void handleBusMessage( string_view topic, const vector<uint8>& bytes );

        void drainCompletions( vector<ChatCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainDeliveries( vector<ChatDelivery>& outListDelivery ) { _deliveryBuffer.drainTo( outListDelivery ); }
        void drainBusTopicChanges( vector<ChatBusTopicChange>& outListChange ) { _busTopicBuffer.drainTo( outListChange ); }

        int32 getLocalMemberCount( string_view channelID ) const;
        /** @brief 맡겨 두고 끝나지 않은 작업(저장, 접속 상태 찾기)의 수입니다. */
        int32 getPendingCount() const { return _pendingStoreCount + static_cast<int32>( _mapPresenceRequestToWhisper.size() ); }
        int32 getQueuedHistoryCount() const { return static_cast<int32>( _listHistoryQueue.size() ); }

        /** @brief 저장 작업의 `complete` 가 호출합니다. 키트 안에서만 씁니다. */
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
            AccountID _recipientID{ kInvalidAccountID }; ///< 귓속말이면 받는 계정이다
        };

        struct Member
        {
            vector<string>      _listChannel{};
            vector<PendingSend> _listPendingSend{};
            string              _displayName{};
            int64               _muteUntilMs{ 0 };
            int64               _sanctionReadMs{ -1 }; ///< -1이면 아직 읽지 않았거나 GM 이 바꿔서 캐시를 버린 것이다
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
