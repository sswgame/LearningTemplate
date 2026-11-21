/**
 * @file LoadBot.h
 * @brief 봇 하나 — 연결 하나의 `OnlineServiceClient`(공유 엔드포인트 모드)와 키트 클라이언트 열(계정 · 서버 디렉터리 · 채팅 · 친구 · 순위 · 매칭 · 라이브 운영 · 경제 · 우편 · 거래),
 *        단계 스택(반복), 기다림 · 응답 대기 상태입니다. 응답이 오면 지연을 지표에 적고 다음 단계로 갑니다.
 * @details - 키트 클라이언트를 그대로 쓴다 — 봇이 측정하는 것은 실제 게임 클라이언트가 내는 와이어다.
 *          - 받은 알림(채팅 메시지 · 친구 알림 · 파티 갱신)은 틱마다 비운다. 친구 신청을 받으면 바로 수락한다(측정하지 않는다).
 *          - 원장 경로(감사 O-12): 구매 · 가짜 스토어 영수증 · 우편 모두 받기 · 거래 · 길드 창고 · 점수 제출. 거래는 짝 봇(번호 ^ 1)과 둘이서 한다 — 짝수 봇이 신청하고 내놓고,
 *            둘 다 받은 스냅샷(`TradeClient::getSnapshot`)을 보고 다음 명령(수락 · 제시 · 잠금 · 확정)을 하나씩 낸다. 짝이 없으면(홀수 마지막 봇) 건너뛴다.
 *          - 실행기 스레드 하나에서 돈다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Event/ConsumerLock.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/StreamFrame.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatClient.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyClient.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardClient.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsClient.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxClient.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingClient.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryClient.h"
#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialClient.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeClient.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Online/Identity/AccountIdentity.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"

#include "Programs/OnlineLoadBot/LoadBotGameSession.h"
#include "Programs/OnlineLoadBot/LoadBotScenario.h"
#include "Programs/OnlineLoadBot/LoadBotSchedule.h"

namespace sw
{
    class LoadBotRunner;
    class NetRequestClient;
    class StreamMessageEndpoint;

    /**
     * @class LoadBot
     * @brief 봇 하나입니다.
     */
    class LoadBot
    {
    public:
        LoadBot( LoadBotRunner* pRunner, int32 botIndex );
        ~LoadBot();

        LoadBot( const LoadBot& )            = delete;
        LoadBot& operator=( const LoadBot& ) = delete;

        /** @brief 서버에 연결을 겁니다(실행기가 늘리는 순서대로 부른다). */
        void start();
        void shutdown();

        /** @brief 단계를 진행합니다(기다림 · 응답 대기면 시한만 본다). 시나리오를 다 돌았거나 연결을 잃고 멈췄으면 false. */
        bool update( MonotonicMilliseconds nowMs );

        // 실행기가 넘긴다 — 이 봇의 연결 사건
        void handleOpened( StreamMessageEndpoint* pEndpoint, NetRequestClient* pRequestClient, StreamConnectionHandle handle );
        void handleFrame( StreamFrameKind kind, const uint8* pBody, int32 bodySize );
        void handleClosed( StreamCloseReason reason );

        bool                   isFinished() const { return _bFinished == SW_TRUE; }
        bool                   isConnectionOpen() const { return _bConnectionOpen == SW_TRUE; }
        StreamConnectionHandle getConnection() const { return _connection; }
        AccountID              getAccountID() const { return _accountID; }
        int32                  getIndex() const { return _botIndex; }
        const string&          getDisplayName() const { return _displayName; }
        /** @brief 거래 단계에서 짝의 신청을 기다리는 중인가 — 신청하는 짝이 이것을 보고 신청을 낸다(받을 쪽이 단계에 오기 전에 내면 그 쪽이 단계를 지나 버린다). */
        bool isWaitingTradeInvite() const { return _awaitKind == AwaitKind::Trade && _tradeID == 0; }

    private:
        struct Frame
        {
            const vector<LoadBotStep>* _pListStep{ nullptr };
            int32                      _stepIndex{ 0 };
            int32                      _repeatRemaining{ 1 };
            uint8                      _bSingle{ SW_FALSE }; ///< `pick` 이 고른 단계 하나만 돈다
        };

        enum class AwaitKind : uint8
        {
            None = 0,
            Response, ///< 키트 응답 · 계정 응답
            Match,    ///< 매칭 결과 알림
            Closed,   ///< Disconnect — 닫힘
            Ready,    ///< Reconnect — Hello(+ 재접속)
            Game,     ///< GameConnect — 게임 UDP 연결이 열리거나 실패할 때까지
            Trade     ///< Trade — 거래가 닫히거나(정산 · 실패 · 취소) 시한까지
        };

        const LoadBotStep* findCurrentStep() const;
        void               advance();
        void               startStep( const LoadBotStep& step, MonotonicMilliseconds nowMs );
        void               startLogin( const LoadBotStep& step );
        bool               startRemoteStep( const LoadBotStep& step );
        void               finishStep( string_view errorKey );
        void               finishWithResult( uint16 errorCode, uint8 result );
        string             expandText( const string& text );
        void               pollNotifications();
        void               pollAccountReplies();
        void               pollMatchResult( MonotonicMilliseconds nowMs );
        void               pollGameSession();
        void               startPick( const LoadBotStep& step );
        void               startTrade( const LoadBotStep& step, MonotonicMilliseconds nowMs );
        void               pollTrade( MonotonicMilliseconds nowMs );
        /** @brief 비밀번호 로그인의 계정 이름입니다 — 가입과 로그인이 같은 이름을 써야 한다(씨앗 · 봇 번호). */
        string makeLoginName() const;
        string makeGuildName() const;
        string makeReceiptPayload( const string& productID );
        void   stop();

        /** @brief 키트 응답 처리기입니다(지연 · 오류만 본다). 응답 타입마다 하나가 생긴다 — 결과 칸이 없는 응답은 끝났으면 0 이다. */
        template <typename TReply>
        void onKitReply( const OnlineResult<TReply>& result )
        {
            if constexpr ( OnlineReplyHasResult<TReply>::value )
                finishWithResult( result._errorCode, static_cast<uint8>( result._reply._result ) );
            else
                finishWithResult( result._errorCode, 0 );
        }

        /** @brief 키트 @p client 의 메서드 @p TMethod 를 부르고 응답을 `onKitReply` 로 받습니다. */
        template <typename TMethod, typename TClient>
        void callKit( TClient& client, const typename TMethod::Request& request )
        {
            (void)client.template call<TMethod>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<TMethod>, &LoadBot::onKitReply<typename TMethod::Reply>, this ) );
        }

        // 응답 처리기 — 키트마다 하나(지연 · 오류만 본다)
        void onDirectoryStatus( const OnlineResult<ServerDirectoryStatus>& result );
        void onDirectoryAssignment( const OnlineResult<ServerAssignment>& result );
        void onFriendAccepted( const OnlineResult<SocialLinkReply>& reply );
        void onGuildCreated( const OnlineResult<GuildInfoReply>& result );

    private:
        OnlineServiceClient        _client;
        AccountClient              _accountClient;
        ServerDirectoryClient      _directoryClient;
        ChatClient                 _chatClient;
        SocialClient               _socialClient;
        LeaderboardClient          _leaderboardClient;
        MatchmakingClient          _matchmakingClient;
        LiveOpsClient              _liveOpsClient;
        EconomyClient              _economyClient;
        MailboxClient              _mailboxClient;
        TradeClient                _tradeClient;
        LoadBotGameSession         _gameSession;
        ConsumerLease              _chatLease;
        ConsumerLease              _socialLease;
        ConsumerLease              _matchmakingLease;
        ConsumerLease              _mailboxLease;
        GameRandom                 _random;
        LoadBotSchedule            _schedule; ///< 기다림이 끝나는 시각과 지연을 잴 기준 시각이다(시나리오 `_pacing`)
        vector<Frame>              _listFrame;
        vector<AccountClientReply> _listAccountReply;
        vector<ChatMessage>        _listChatMessage;
        vector<SocialNotification> _listSocialNotification;
        vector<MatchAssignment>    _listMatchAssignment;
        vector<PartySnapshot>      _listPartySnapshot;
        vector<PartyInvite>        _listPartyInvite;
        vector<LobbySnapshot>      _listLobbySnapshot;
        vector<MailboxNewMail>     _listNewMail;
        vector<TradeClientReply>   _listTradeReply;
        vector<TradeClientUpdate>  _listTradeUpdate;
        vector<TradeLeg>           _listTradeLeg; ///< 이 거래에서 내놓을 것(신청하는 짝수 봇만)
        string                     _displayName;  ///< 로그인 응답의 표시 이름 — 짝이 거래 신청에 쓴다
        LoadBotRunner*             _pRunner;
        StreamConnectionHandle     _connection;
        AccountID                  _accountID;
        uint64                     _tradeID; ///< 이 거래 단계의 거래(0 이면 아직 없다)
        MonotonicMilliseconds      _waitUntilMs;
        int64                      _requestStartUs; ///< 지연을 잴 기준 시각이다 — 열린 루프면 보내려던 시각이다
        int32                      _botIndex;
        int32                      _sendSequence;
        AccountMethodName          _pendingAccountOperation;
        LoadBotAction              _pendingAction;
        AwaitKind                  _awaitKind;
        uint8                      _bPasswordLogin;
        uint8                      _bConnecting; ///< 연결을 걸었고 열림 · 닫힘을 기다린다
        uint8                      _bConnectionOpen;
        uint8                      _bFinished;
        uint8                      _bTradeCallPending; ///< 거래 명령 하나의 답을 기다린다 — 한 번에 하나만 낸다
        uint8                      _bTradeInvited;     ///< 신청하는 봇이 신청을 냈다
    };
} // namespace sw
