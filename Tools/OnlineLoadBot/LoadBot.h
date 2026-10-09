/**
 * @file LoadBot.h
 * @brief 봇 하나 — 연결 하나의 `OnlineServiceClient`(공유 끝점 모드)와 키트 클라이언트 일곱(계정 · 서버 디렉터리 · 채팅 · 친구 · 순위 · 매칭 · 라이브 운영),
 *        단계 스택(반복), 기다림 · 응답 대기 상태입니다. 응답이 오면 지연을 지표에 적고 다음 단계로 갑니다.
 * @details - 키트 클라이언트를 그대로 쓴다 — 봇이 재는 것은 실제 게임 클라이언트가 내는 와이어다.
 *          - 받은 알림(채팅 메시지 · 친구 알림 · 파티 갱신)은 틱마다 비운다. 친구 신청을 받으면 바로 수락한다(재지 않는다).
 *          - 실행기 스레드 하나에서 돈다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/StreamFrame.h"
#include "Core/Network/Transport/StreamTypes.h"

#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Kits/Online/Account/AccountClient.h"
#include "GameFramework/Kits/Online/Chat/ChatClient.h"
#include "GameFramework/Kits/Online/Leaderboard/LeaderboardClient.h"
#include "GameFramework/Kits/Online/LiveOps/LiveOpsClient.h"
#include "GameFramework/Kits/Online/Matchmaking/MatchmakingClient.h"
#include "GameFramework/Kits/Online/ServerDirectory/ServerDirectoryClient.h"
#include "GameFramework/Kits/Online/Social/SocialClient.h"

#include "OnlineLoadBot/LoadBotScenario.h"

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
        bool tick( int64 nowMs );

        // 실행기가 넘긴다 — 이 봇의 연결 사건
        void handleOpened( StreamMessageEndpoint* pEndpoint, NetRequestClient* pRequestClient, StreamConnectionHandle handle );
        void handleFrame( StreamFrameKind kind, const uint8* pBody, int32 bodySize );
        void handleClosed( StreamCloseReason reason );

        bool                   isFinished() const { return _bFinished == SW_TRUE; }
        bool                   isConnectionOpen() const { return _bConnectionOpen == SW_TRUE; }
        StreamConnectionHandle getConnection() const { return _connection; }
        AccountId              getAccountId() const { return _accountId; }
        int32                  getIndex() const { return _botIndex; }

    private:
        struct Frame
        {
            const vector<LoadBotStep>* _pListStep{ nullptr };
            int32                      _stepIndex{ 0 };
            int32                      _repeatRemaining{ 1 };
        };

        enum class AwaitKind : uint8
        {
            None = 0,
            Response, ///< 키트 응답 · 계정 응답
            Match,    ///< 매칭 결과 알림
            Closed,   ///< Disconnect — 닫힘
            Ready     ///< Reconnect — Hello(+ 재접속)
        };

        const LoadBotStep* findCurrentStep() const;
        void               advance();
        void               startStep( const LoadBotStep& step, int64 nowMs );
        void               startLogin( const LoadBotStep& step );
        bool               startRemoteStep( const LoadBotStep& step );
        void               finishStep( string_view errorKey );
        void               finishWithResult( uint16 errorCode, uint8 result );
        string             expandText( const string& text );
        void               drainNotifications();
        void               pollAccountReplies();
        void               pollMatchResult( int64 nowMs );
        void               stop();

        // 응답 처리기 — 키트마다 하나(지연 · 오류만 본다)
        void onChatReply( const ChatClientReply& reply ) { finishWithResult( reply._errorCode, static_cast<uint8>( reply._reply._result ) ); }
        void onSocialReply( const SocialClientReply& reply ) { finishWithResult( reply._errorCode, static_cast<uint8>( reply._reply._result ) ); }
        void onLeaderboardReply( const LeaderboardClientReply& reply ) { finishWithResult( reply._errorCode, static_cast<uint8>( reply._reply._result ) ); }
        void onMatchmakingReply( const MatchmakingClientReply& reply ) { finishWithResult( reply._errorCode, static_cast<uint8>( reply._reply._result ) ); }
        void onLiveOpsReply( const LiveOpsClientReply& reply ) { finishWithResult( reply._errorCode, static_cast<uint8>( reply._reply._result ) ); }
        void onDirectoryStatus( uint16 errorCode, const ServerDirectoryStatus& status );
        void onDirectoryAssignment( uint16 errorCode, const ServerAssignment& assignment );
        void onFriendAccepted( const SocialClientReply& reply );

        OnlineServiceClient        _client;
        AccountClient              _accountClient;
        ServerDirectoryClient      _directoryClient;
        ChatClient                 _chatClient;
        SocialClient               _socialClient;
        LeaderboardClient          _leaderboardClient;
        MatchmakingClient          _matchmakingClient;
        LiveOpsClient              _liveOpsClient;
        GameRandom                 _random;
        vector<Frame>              _listFrame;
        vector<AccountClientReply> _listAccountReply;
        vector<ChatMessage>        _listChatMessage;
        vector<SocialNotification> _listSocialNotification;
        vector<MatchAssignment>    _listMatchAssignment;
        vector<PartySnapshot>      _listPartySnapshot;
        vector<PartyInvite>        _listPartyInvite;
        vector<LobbySnapshot>      _listLobbySnapshot;
        LoadBotRunner*             _pRunner;
        StreamConnectionHandle     _connection;
        AccountId                  _accountId;
        int64                      _waitUntilMs;
        int64                      _requestStartUs;
        int32                      _botIndex;
        int32                      _sendSequence;
        AccountClientOperation     _pendingAccountOperation;
        LoadBotAction              _pendingAction;
        AwaitKind                  _awaitKind;
        uint8                      _bPasswordLogin;
        uint8                      _bConnecting; ///< 연결을 걸었고 열림 · 닫힘을 기다린다
        uint8                      _bConnectionOpen;
        uint8                      _bFinished;
    };
} // namespace sw
