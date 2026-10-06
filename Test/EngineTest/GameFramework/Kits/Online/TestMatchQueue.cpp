// 매칭 대기열 — 서버 하나: 넷이 줄 서면 등록된 게임 서버로 배정 + 결과 넷. 서버 둘: 권한은 하나만(임대), 다른 서버의 표가 버스로 권한에 가고 결과가 낸 서버로 돌아옴,
// 게임 서버 에이전트가 올 사람을 앎. 권한 서버가 멈추면 TTL 뒤 다른 서버가 권한을 잡고, 잃은 표는 낸 서버의 시한으로 Timeout. 자리가 없으면 기다렸다 서버가 생기면 배정,
// 끝내 없으면 NoServer. 파티 장이 줄 서면 파티 전체가 한 팀, 장이 아니면 거절. 로비 시작은 로비의 편 그대로 배정.
#include "pch.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Directory/ServerRegistration.h"
#include "GameFramework/Kits/Online/Server/Matchmaking/MatchQueueService.h"
#include "GameFramework/Kits/Online/Server/Matchmaking/MatchServerAgent.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct MatchQueueTestInternal
    {
        static MatchModeDefinition makeDuoMode( int64 maxWaitMs = 120000 )
        {
            MatchModeDefinition mode;
            mode._modeId    = "duo";
            mode._teamCount = 2;
            mode._teamSize  = 2;
            mode._maxWaitMs = maxWaitMs;
            return mode;
        }

        static void registerGameServer( EphemeralStoreRouter& router, ServerRegistration& registration, int64 nowMs )
        {
            ServerDescriptor descriptor;
            descriptor._serverId = 50;
            descriptor._kind     = "game";
            descriptor._region   = "kr";
            descriptor._address  = "10.0.0.5";
            descriptor._port     = 7777;
            descriptor._capacity = 10;
            SW_EXPECT_TRUE( registration.initialize( &router, descriptor ) );
            registration.setState( ServerState::Open );
            registration.tick( nowMs );
            (void)router.pump();
        }
    };

    /** @brief 매칭 서버 하나 — 캐시 앞 · 라우터 · (있으면) 로컬 버스 · 파티 · 대기열. 버스 구독 바뀜과 메시지는 호스트 대신 시험이 처리한다. */
    struct QueueNode
    {
        MemoryEphemeralStore           _cache;
        EphemeralStoreRouter           _router;
        unique_ptr<LocalServerBus>     _bus;
        PartyLobbyService              _partyLobby;
        MatchQueueService              _service;
        vector<MatchQueueNotification> _listNotification;
        vector<MatchQueueCompletion>   _listCompletion;

        QueueNode( MemoryEphemeralDatabase* pCacheDatabase, LocalServerBusHub* pHub, uint64 serverId, int64 maxWaitMs = 120000 )
            : _cache{ pCacheDatabase }
            , _router{}
            , _bus{}
            , _partyLobby{}
            , _service{}
            , _listNotification{}
            , _listCompletion{}
        {
            _router.initialize( &_cache );
            if ( pHub != nullptr )
                _bus = make_unique<LocalServerBus>( pHub, serverId );
            _partyLobby.initialize( &_router, serverId );
            MatchQueueDependencies dependencies;
            dependencies._pRouter     = &_router;
            dependencies._pBus        = _bus.get();
            dependencies._pPartyLobby = &_partyLobby;
            dependencies._serverId    = serverId;
            _service.initialize( dependencies, vector<MatchModeDefinition>{ MatchQueueTestInternal::makeDuoMode( maxWaitMs ) } );
        }

        ~QueueNode()
        {
            _service.shutdown();
            _partyLobby.shutdown();
            _router.shutdown();
        }

        void step( int64 nowMs )
        {
            for ( int32 round = 0; round < 8; ++round )
            {
                if ( _bus != nullptr )
                {
                    vector<MatchQueueTopicChange> listChange;
                    _service.drainTopicChanges( listChange );
                    for ( const MatchQueueTopicChange& change : listChange )
                    {
                        if ( change._bSubscribe != SW_FALSE )
                            _bus->subscribe( change._topic );
                        else
                            _bus->unsubscribe( change._topic );
                    }
                    vector<ServerBusMessage> listMessage;
                    (void)_bus->pollMessages( listMessage );
                    for ( const ServerBusMessage& message : listMessage )
                        _service.handleBusMessage( message._topic, message._bytes, nowMs );
                }
                _service.tick( nowMs );
                (void)_router.pump();
                vector<LobbyStartRequest> listStart;
                _partyLobby.drainLobbyStarts( listStart );
                for ( const LobbyStartRequest& start : listStart )
                    _service.placeLobby( start._lobby, nowMs );
            }
            _service.drainNotifications( _listNotification );
            _service.drainCompletions( _listCompletion );
            vector<PartyLobbyCompletion> listPartyCompletion;
            _partyLobby.drainCompletions( listPartyCompletion );
            vector<PartyLobbyNotification> listPartyNotification;
            _partyLobby.drainNotifications( listPartyNotification );
        }
    };

    /** @brief 게임 서버 쪽 — 버스 하나에 배정 주제만 구독하고 에이전트에 넘긴다. */
    struct GameServerNode
    {
        LocalServerBus   _bus;
        MatchServerAgent _agent;

        GameServerNode( LocalServerBusHub* pHub, uint64 serverId )
            : _bus{ pHub, serverId }
            , _agent{}
        {
            _agent.initialize( serverId );
            _bus.subscribe( _agent.getAssignTopic() );
        }

        void step( int64 nowMs )
        {
            vector<ServerBusMessage> listMessage;
            (void)_bus.pollMessages( listMessage );
            for ( const ServerBusMessage& message : listMessage )
                SW_EXPECT_TRUE( _agent.handleAssign( message._bytes, nowMs ) );
            _agent.tick( nowMs );
        }
    };
} // namespace

SW_TEST_CASE( MatchQueueTest, SingleServerPlacesMatchOnRegisteredGameServer )
{
    using Internal = MatchQueueTestInternal;
    MemoryEphemeralDatabase cacheDatabase;
    cacheDatabase.setManualTimeMs( 0 );
    QueueNode          node( &cacheDatabase, nullptr, 1 );
    ServerRegistration gameServer;
    Internal::registerGameServer( node._router, gameServer, 0 );
    node.step( 0 );                                       // 서버 목록 읽기
    SW_EXPECT_TRUE( node._service.isAuthority( "duo" ) ); // 버스 없음 — 늘 권한

    for ( AccountId accountId = 1; accountId <= 4; ++accountId )
        node._service.joinQueue( accountId, "duo", "kr", 0, accountId );
    node.step( 1000 );
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 4 ) );
    for ( const MatchQueueNotification& notification : node._listNotification )
    {
        SW_EXPECT_TRUE( notification._assignment._outcome == MatchQueueOutcome::Found );
        SW_EXPECT_EQUAL( notification._assignment._serverId, uint64( 50 ) );
        SW_EXPECT_EQUAL( notification._assignment._port, uint16( 7777 ) );
        SW_EXPECT_EQUAL( notification._assignment._matchId, node._listNotification[0]._assignment._matchId );
        SW_EXPECT_EQUAL( notification._assignment._listTeammate.size(), size_t( 2 ) );
    }
    SW_EXPECT_EQUAL( node._service.getQueuedTicketCount( "duo" ), 0 );
    SW_EXPECT_EQUAL( node._service.getLocalTicketCount(), 0 );

    node._service.joinQueue( 9, "nope", "kr", 1000, 9 );
    node._service.joinQueue( 10, "duo", "kr", 1000, 10 );
    node.step( 1000 );
    node._service.joinQueue( 10, "duo", "kr", 1000, 11 ); // 이미 줄 섰다
    node._service.leaveQueue( 12, 12 );                   // 줄 서지 않았다
    node.step( 1000 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 8 ) );
    SW_EXPECT_TRUE( node._listCompletion[4]._result == MatchmakingResult::UnknownMode );
    SW_EXPECT_TRUE( node._listCompletion[5]._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( node._listCompletion[6]._result == MatchmakingResult::AlreadyQueued );
    SW_EXPECT_TRUE( node._listCompletion[7]._result == MatchmakingResult::NotQueued );
    node._service.leaveQueue( 10, 13 );
    node.step( 1000 );
    SW_EXPECT_TRUE( node._listCompletion.back()._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( node._listNotification.back()._assignment._outcome == MatchQueueOutcome::Cancelled );
    SW_EXPECT_EQUAL( node._service.getQueuedTicketCount( "duo" ), 0 );
}

SW_TEST_CASE( MatchQueueTest, TicketsFromAnotherServerReachTheAuthorityAndResultsComeBack )
{
    using Internal = MatchQueueTestInternal;
    MemoryEphemeralDatabase cacheDatabase;
    cacheDatabase.setManualTimeMs( 0 );
    LocalServerBusHub  hub;
    QueueNode          first( &cacheDatabase, &hub, 1 );
    QueueNode          second( &cacheDatabase, &hub, 2 );
    GameServerNode     gameServer( &hub, 50 );
    ServerRegistration registration;
    Internal::registerGameServer( first._router, registration, 0 );
    first.step( 0 ); // 첫 서버가 임대를 잡는다
    second.step( 0 );
    SW_EXPECT_TRUE( first._service.isAuthority( "duo" ) );
    SW_EXPECT_FALSE( second._service.isAuthority( "duo" ) ); // 권한은 하나만

    for ( AccountId accountId = 1; accountId <= 4; ++accountId )
        second._service.joinQueue( accountId, "duo", "kr", 1000, accountId );
    second.step( 1000 ); // 표를 mm.queue.duo 로
    SW_EXPECT_EQUAL( second._service.getQueuedTicketCount( "duo" ), 0 );
    first.step( 1000 ); // 매처 · 배정 → mm.assign.<50> · mm.result.<2>
    second.step( 1000 );
    gameServer.step( 1000 );

    SW_EXPECT_TRUE( first._listNotification.empty() );
    SW_ASSERT_EQUAL( second._listNotification.size(), size_t( 4 ) );
    for ( const MatchQueueNotification& notification : second._listNotification )
    {
        SW_EXPECT_TRUE( notification._assignment._outcome == MatchQueueOutcome::Found );
        SW_EXPECT_EQUAL( notification._assignment._serverId, uint64( 50 ) );
    }
    uint64 matchId = 0;
    int32  team    = -1;
    SW_ASSERT_TRUE( gameServer._agent.findExpected( 1, matchId, team ) );
    SW_EXPECT_EQUAL( matchId, second._listNotification[0]._assignment._matchId );
    SW_EXPECT_FALSE( gameServer._agent.findExpected( 99, matchId, team ) );
    SW_EXPECT_EQUAL( gameServer._agent.getExpectedCount(), 4 );
    gameServer.step( 1000 + MatchServerAgent::kExpectTtlMs ); // 오지 않은 사람은 지운다
    SW_EXPECT_EQUAL( gameServer._agent.getExpectedCount(), 0 );

    first.step( 4000 ); // 권한은 연장(자기 값일 때만)으로 그대로
    second.step( 4000 );
    SW_EXPECT_TRUE( first._service.isAuthority( "duo" ) );
    SW_EXPECT_FALSE( second._service.isAuthority( "duo" ) );
}

SW_TEST_CASE( MatchQueueTest, AuthorityFailoverLosesQueuedTicketsWhichTimeOutAtTheOrigin )
{
    MemoryEphemeralDatabase cacheDatabase;
    cacheDatabase.setManualTimeMs( 0 );
    LocalServerBusHub hub;
    const int64       maxWaitMs = 20000;
    QueueNode         first( &cacheDatabase, &hub, 1, maxWaitMs );
    QueueNode         second( &cacheDatabase, &hub, 2, maxWaitMs );
    first.step( 0 );
    second.step( 0 );
    SW_ASSERT_TRUE( first._service.isAuthority( "duo" ) );

    second._service.joinQueue( 1, "duo", "kr", 1000, 1 ); // 혼자 — 경기가 안 된다
    second.step( 1000 );
    first.step( 1000 );
    SW_EXPECT_EQUAL( first._service.getQueuedTicketCount( "duo" ), 1 );

    // 첫 서버가 멈춘다(더 돌리지 않는다). 임대 TTL 이 지나면 둘째가 잡는다.
    cacheDatabase.advanceTimeMs( MatchQueueLimit::kLeaseTtlMs + 1000 );
    second.step( 12000 );
    SW_EXPECT_TRUE( second._service.isAuthority( "duo" ) );
    SW_EXPECT_EQUAL( second._service.getQueuedTicketCount( "duo" ), 0 ); // 첫 서버의 표는 모른다
    SW_EXPECT_TRUE( second._listNotification.empty() );

    const int64 deadlineMs = 1000 + maxWaitMs + MatchQueueLimit::kNoServerGiveUpMs + MatchQueueLimit::kResultGraceMs;
    second.step( deadlineMs );
    SW_EXPECT_TRUE( second._listNotification.empty() ); // 시한 전
    second.step( deadlineMs + 1 );
    SW_ASSERT_EQUAL( second._listNotification.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( second._listNotification[0]._assignment._outcome == MatchQueueOutcome::Timeout );
    SW_EXPECT_EQUAL( second._listNotification[0]._recipientId, AccountId( 1 ) );
    second._service.joinQueue( 1, "duo", "kr", deadlineMs + 1, 2 ); // 다시 줄 설 수 있다
    second.step( deadlineMs + 1 );
    SW_EXPECT_TRUE( second._listCompletion.back()._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( second._service.getQueuedTicketCount( "duo" ), 1 );
}

SW_TEST_CASE( MatchQueueTest, NoServerWaitsThenPlacesWhenOneAppears )
{
    using Internal = MatchQueueTestInternal;
    {
        MemoryEphemeralDatabase cacheDatabase;
        cacheDatabase.setManualTimeMs( 0 );
        QueueNode node( &cacheDatabase, nullptr, 1 );
        for ( AccountId accountId = 1; accountId <= 4; ++accountId )
            node._service.joinQueue( accountId, "duo", "kr", 0, accountId );
        node.step( 1000 );
        SW_EXPECT_TRUE( node._listNotification.empty() ); // 경기는 만들었지만 자리가 없다 — 기다림

        ServerRegistration gameServer;
        Internal::registerGameServer( node._router, gameServer, 2000 );
        node.step( 3500 ); // 목록 다시 읽기(2 초 주기) 뒤 배정
        SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 4 ) );
        for ( const MatchQueueNotification& notification : node._listNotification )
            SW_EXPECT_TRUE( notification._assignment._outcome == MatchQueueOutcome::Found );
    }
    {
        MemoryEphemeralDatabase cacheDatabase;
        cacheDatabase.setManualTimeMs( 0 );
        QueueNode node( &cacheDatabase, nullptr, 1 );
        for ( AccountId accountId = 1; accountId <= 4; ++accountId )
            node._service.joinQueue( accountId, "duo", "kr", 0, accountId );
        node.step( 1000 );
        node.step( 1000 + MatchQueueLimit::kNoServerGiveUpMs - 1 );
        SW_EXPECT_TRUE( node._listNotification.empty() );
        node.step( 1000 + MatchQueueLimit::kNoServerGiveUpMs ); // 끝내 서버가 없다
        SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 4 ) );
        for ( const MatchQueueNotification& notification : node._listNotification )
            SW_EXPECT_TRUE( notification._assignment._outcome == MatchQueueOutcome::NoServer );
        SW_EXPECT_EQUAL( node._service.getLocalTicketCount(), 0 );
    }
}

SW_TEST_CASE( MatchQueueTest, PartyLeaderQueuesTheWholePartyAsOneTeam )
{
    using Internal = MatchQueueTestInternal;
    MemoryEphemeralDatabase cacheDatabase;
    cacheDatabase.setManualTimeMs( 0 );
    QueueNode          node( &cacheDatabase, nullptr, 1 );
    ServerRegistration gameServer;
    Internal::registerGameServer( node._router, gameServer, 0 );
    node._partyLobby.createParty( 10, 1 );
    node.step( 0 );
    node._partyLobby.inviteToParty( 10, 11, 2 );
    node.step( 0 );
    // 파티 id 는 계정 색인에서 — 수락에 필요하다
    MemoryEphemeralStore probe{ &cacheDatabase };
    (void)probe.submit( EphemeralRequest::makeGet( PartyLobbyService::makeAccountPartyKey( 10 ) ) );
    vector<EphemeralReply> listReply;
    (void)probe.pollReplies( listReply );
    uint64 partyId = 0;
    SW_ASSERT_TRUE( listReply.size() == 1 && MatchmakingProtocol::decodeId( listReply[0]._value, partyId ) );
    node._partyLobby.acceptPartyInvite( 11, partyId, 3 );
    node.step( 0 );

    node._service.joinQueue( 11, "duo", "kr", 0, 20 ); // 장이 아니다
    node.step( 0 );
    SW_EXPECT_TRUE( node._listCompletion.back()._result == MatchmakingResult::NotPartyLeader );
    node._service.joinQueue( 10, "duo", "kr", 0, 21 );
    node.step( 0 );
    SW_ASSERT_TRUE( node._listCompletion.back()._result == MatchmakingResult::Ok );
    node._service.joinQueue( 11, "duo", "kr", 0, 22 ); // 파티 표에 들어 있다
    node.step( 0 );
    SW_EXPECT_TRUE( node._listCompletion.back()._result == MatchmakingResult::AlreadyQueued );

    node._service.joinQueue( 12, "duo", "kr", 0, 23 );
    node._service.joinQueue( 13, "duo", "kr", 0, 24 );
    node.step( 1000 );
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 4 ) );
    int32 leaderTeam = -1;
    int32 memberTeam = -2;
    for ( const MatchQueueNotification& notification : node._listNotification )
    {
        SW_EXPECT_TRUE( notification._assignment._outcome == MatchQueueOutcome::Found );
        if ( notification._recipientId == 10 )
            leaderTeam = notification._assignment._team;
        if ( notification._recipientId == 11 )
            memberTeam = notification._assignment._team;
    }
    SW_EXPECT_EQUAL( leaderTeam, memberTeam ); // 파티는 쪼개지 않는다
}

SW_TEST_CASE( MatchQueueTest, LobbyStartPlacesTheLobbyTeams )
{
    using Internal = MatchQueueTestInternal;
    MemoryEphemeralDatabase cacheDatabase;
    cacheDatabase.setManualTimeMs( 0 );
    QueueNode     node( &cacheDatabase, nullptr, 1 );
    LobbySnapshot request;
    request._name           = "scrim";
    request._modeId         = "duo";
    request._maxMemberCount = 4;
    node._partyLobby.createLobby( 10, request, 0, 1 );
    node.step( 0 );
    node._partyLobby.listLobbies( "duo", 2 );
    vector<PartyLobbyCompletion> listCompletion;
    for ( int32 round = 0; round < 8; ++round )
        (void)node._router.pump();
    node._partyLobby.drainCompletions( listCompletion );
    SW_ASSERT_TRUE( listCompletion.size() == 1 && listCompletion[0]._listLobby.size() == 1 );
    const uint64 lobbyId = listCompletion[0]._listLobby[0]._lobbyId;
    node._partyLobby.joinLobby( 11, lobbyId, 3 );
    node.step( 0 );
    node._partyLobby.setLobbyReady( 11, lobbyId, true, 4 );
    node.step( 0 );

    node._partyLobby.startLobby( 10, lobbyId, 5 ); // 서버가 없다 — NoServer + 다시 Open
    node.step( 0 );
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 2 ) );
    SW_EXPECT_TRUE( node._listNotification[0]._assignment._outcome == MatchQueueOutcome::NoServer );

    ServerRegistration gameServer;
    Internal::registerGameServer( node._router, gameServer, 3000 );
    node.step( 3000 );
    node._listNotification.clear();
    node._partyLobby.startLobby( 10, lobbyId, 6 ); // 다시 열렸으니 다시 시작할 수 있다
    node.step( 3000 );
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 2 ) );
    SW_EXPECT_TRUE( node._listNotification[0]._assignment._outcome == MatchQueueOutcome::Found );
    SW_EXPECT_NOT_EQUAL( node._listNotification[0]._assignment._team, node._listNotification[1]._assignment._team ); // 로비의 편 그대로(방장 0 · 들어온 이 1)
    SW_EXPECT_EQUAL( node._listNotification[0]._assignment._serverId, uint64( 50 ) );
}
