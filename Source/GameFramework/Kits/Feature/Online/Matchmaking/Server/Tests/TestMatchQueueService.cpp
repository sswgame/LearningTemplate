// MatchQueueServiceTest — 매칭 대기열(메모리 캐시 + 프로세스 안 버스): 줄 서기 · 빠지기, 경기 배정 · 팀 · 샤드 포트, 서버 없음 대기와 포기, 시한, 실력 통계,
// 서버 둘의 임대 · 버스 티켓 · 결과 · 취소, 권한 넘어감과 잃은 티켓의 시한, 백필, 로비 배정, 파티 티켓과 파티를 찾는 동안의 빠지기.
// 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountStatSource.h"
#include "GameFramework/Runtime/Online/Bus/Server/LocalServerBus.h"
#include "GameFramework/Runtime/Online/Bus/ServerBus.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"
#include "GameFramework/Runtime/Online/Directory/Server/ServerRegistration.h"

using namespace sw;

namespace
{
    constexpr int64     kNowMs   = 1700000000000ll;
    constexpr int64     kMaxWait = 60000;
    constexpr uint64    kGameID  = 500;
    constexpr AccountID kAlice   = 11;
    constexpr AccountID kBob     = 12;
    constexpr AccountID kCarol   = 13;
    constexpr uint64    kServerA = 1;
    constexpr uint64    kServerB = 2;

    /** @brief 1 대 1 모드입니다. @p teamSize 를 키우면 파티 모드입니다. */
    struct MatchQueueTestUtil
    {
        static MatchModeDef makeMode( string_view modeID = "duel", int32 teamSize = 1, string_view ratingStat = "" )
        {
            MatchModeDef mode;
            mode._modeID     = string( modeID );
            mode._teamCount  = 2;
            mode._teamSize   = teamSize;
            mode._maxWaitMs  = kMaxWait;
            mode._ratingStat = string( ratingStat );
            return mode;
        }
    };

    /** @brief 통계를 손으로 넣는 창입니다. 넣지 않은 계정은 NotLoaded 입니다. */
    class FakeStatSource final : public IAccountStatSource
    {
    public:
        unordered_map<AccountID, int64> _mapStat{};     ///< 음수는 Missing 이다
        unordered_map<AccountID, int32> _mapRetained{}; ///< 올려 달라고 한 수이다

        AccountStatLookup findStat( AccountID accountID, string_view /*statName*/, int64& outValue ) const override
        {
            const auto loadedIt = _mapStat.find( accountID );
            if ( loadedIt == _mapStat.end() )
                return AccountStatLookup::NotLoaded;
            if ( loadedIt->second < 0 )
                return AccountStatLookup::Missing;
            outValue = loadedIt->second;
            return AccountStatLookup::Found;
        }

        void retainStats( AccountID accountID ) override { ++_mapRetained[accountID]; }
        void releaseStats( AccountID accountID ) override { --_mapRetained[accountID]; }
    };

    /** @brief 서버 하나입니다. 캐시 데이터와 버스 허브는 빌립니다. @p pHub 가 없으면 서버 한 대(항상 권한)입니다. */
    struct QueueServer
    {
        MemoryEphemeralStore       _cache;
        EphemeralStoreRouter       _router;
        unique_ptr<LocalServerBus> _transport;
        unique_ptr<ServerBus>      _bus;
        MatchQueueService          _queue;

        QueueServer( MemoryEphemeralDatabase& cacheData, LocalServerBusHub* pHub, uint64 serverID )
            : _cache{ &cacheData }
            , _router{}
            , _transport{}
            , _bus{}
            , _queue{}
            , _serverID{ serverID }
        {
            _router.initialize( &_cache );
            if ( pHub != nullptr )
            {
                _transport = make_unique<LocalServerBus>( pHub, serverID );
                _bus       = make_unique<ServerBus>( *_transport );
            }
        }

        ~QueueServer()
        {
            _queue.shutdown();
            _router.shutdown();
        }

        MatchQueueDependencies makeDependencies()
        {
            MatchQueueDependencies dependencies;
            dependencies._pRouter  = &_router;
            dependencies._pBus     = _bus.get();
            dependencies._serverID = _serverID;
            return dependencies;
        }

        void start( const vector<MatchModeDef>& listMode ) { SW_EXPECT_TRUE( _queue.initialize( makeDependencies(), listMode ) ); }

        /** @brief 임대 · 서버 목록 · 매처를 한 번 돌리고, 캐시 답과 버스 메시지를 끝까지 나눠 줍니다. */
        void tick( int64 nowMs )
        {
            _queue.update( UnixMilliseconds{ nowMs } );
            pump( nowMs );
        }

        void pump( int64 nowMs )
        {
            for ( int32 round = 0; round < 8; ++round )
            {
                int32 work = _router.pump();
                if ( _transport != nullptr )
                {
                    vector<MatchQueueTopicChange> listChange;
                    _queue.pollTopicChanges( listChange );
                    for ( const MatchQueueTopicChange& change : listChange )
                    {
                        if ( change._bSubscribe == SW_TRUE )
                            _transport->subscribe( change._topic );
                        else
                            _transport->unsubscribe( change._topic );
                    }
                    vector<ServerBusMessage> listMessage;
                    work += _transport->pollMessages( listMessage );
                    for ( const ServerBusMessage& message : listMessage )
                    {
                        _queue.handleBusMessage( message, UnixMilliseconds{ nowMs } );
                    }
                }
                if ( work == 0 )
                    return;
            }
        }

        MatchQueueCompletion join( AccountID accountID, int64 nowMs = kNowMs, string_view modeID = "duel", string_view region = "kr" )
        {
            _queue.joinQueue( accountID, modeID, region, UnixMilliseconds{ nowMs }, 7 );
            return takeCompletion();
        }

        MatchQueueCompletion takeCompletion()
        {
            vector<MatchQueueCompletion> listCompletion;
            _queue.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            return listCompletion.empty() ? MatchQueueCompletion{} : listCompletion[0];
        }

        vector<MatchQueueNotification> takeNotifications()
        {
            vector<MatchQueueNotification> listNotification;
            _queue.pollNotifications( listNotification );
            return listNotification;
        }

    private:
        uint64 _serverID;
    };

    /** @brief 게임 서버 하나를 디렉터리에 올립니다. */
    struct GameServerEntry
    {
        ServerRegistration _registration;

        void start( EphemeralStoreRouter& router, int64 nowMs, string_view region = "kr", uint64 serverID = kGameID )
        {
            ServerDescriptor descriptor;
            descriptor._serverID  = serverID;
            descriptor._kind      = "game";
            descriptor._region    = string( region );
            descriptor._address   = "10.0.0.9";
            descriptor._capacity  = 100;
            descriptor._port      = 7000;
            descriptor._portCount = 1;
            SW_REQUIRE_TRUE( _registration.initialize( &router, descriptor ) );
            _registration.setState( ServerState::Open );
            _registration.update( UnixMilliseconds{ nowMs } );
        }
    };

    /** @brief 서버 한 대 묶음입니다. */
    struct SoloWorld
    {
        ManualClock             _clock;
        MemoryEphemeralDatabase _cacheData;
        QueueServer             _server;
        GameServerEntry         _game;

        SoloWorld()
            : _clock{ MonotonicMilliseconds{ 1000 } }
            , _cacheData{ _clock }
            , _server{ _cacheData, nullptr, kServerA }
            , _game{}
        {
        }

        /** @brief 게임 서버를 올리고 목록을 읽어 들입니다. */
        void startWithGameServer( const vector<MatchModeDef>& listMode )
        {
            _server.start( listMode );
            _game.start( _server._router, kNowMs );
            _server.pump( kNowMs );
            _server.tick( kNowMs ); // 첫 읽기를 맡긴다
            _server.tick( kNowMs ); // 답을 스냅숏에 넣는다
        }
    };

    /** @brief 서버 둘(캐시 · 버스를 같이 씀) 묶음입니다. 임대 시한을 시계로 민다. */
    struct PairWorld
    {
        ManualClock             _clock;
        MemoryEphemeralDatabase _cacheData;
        LocalServerBusHub       _hub;
        QueueServer             _serverA;
        QueueServer             _serverB;
        GameServerEntry         _game;

        PairWorld()
            : _clock{ MonotonicMilliseconds{ 1000 } }
            , _cacheData{ _clock }
            , _hub{}
            , _serverA{ _cacheData, &_hub, kServerA }
            , _serverB{ _cacheData, &_hub, kServerB }
            , _game{}
        {
        }

        void tickBoth( int64 nowMs )
        {
            for ( int32 round = 0; round < 3; ++round )
            {
                _serverA.tick( nowMs );
                _serverB.tick( nowMs );
            }
        }

        /** @brief A 가 먼저 임대를 잡게 하고 게임 서버를 올립니다. */
        void start( const vector<MatchModeDef>& listMode )
        {
            _serverA.start( listMode );
            _serverB.start( listMode );
            _game.start( _serverA._router, kNowMs );
            _serverA.tick( kNowMs );
            tickBoth( kNowMs );
            SW_REQUIRE_TRUE( _serverA._queue.isAuthority( "duel" ) );
            SW_REQUIRE_TRUE( _serverB._queue.isAuthority( "duel" ) == false );
        }
    };
} // namespace

/** @brief [MatchQueueServiceTest] 시작: 실력 모드에 통계 창이 없으면 false, id 규칙이 틀린 모드는 빼고 나머지만 등록한다 */
SW_TEST_CASE( MatchQueueServiceTest, InitializeChecksModes )
{
    SW_EXPECT_LOG( "MatchQueueService.MatchQueueModeRatedStat" );
    SW_EXPECT_LOG( "MatchQueueService.MatchQueueModeIdBreaks" );
    SoloWorld world;
    SW_EXPECT_FALSE( world._server._queue.initialize( world._server.makeDependencies(), vector<MatchModeDef>{ MatchQueueTestUtil::makeMode( "rated", 1, "rating" ) } ) );

    SW_REQUIRE_TRUE( world._server._queue.initialize( world._server.makeDependencies(), vector<MatchModeDef>{ MatchQueueTestUtil::makeMode( "Bad Mode" ), MatchQueueTestUtil::makeMode( "duel" ) } ) );
    SW_EXPECT_FALSE( world._server._queue.hasMode( "Bad Mode" ) );
    SW_EXPECT_TRUE( world._server._queue.hasMode( "duel" ) );
    SW_EXPECT_TRUE( world._server._queue.isAuthority( "duel" ) ); // 버스가 없으면 항상 권한이다
}

/** @brief [MatchQueueServiceTest] 줄 서기 · 빠지기: 모르는 모드 · 틀린 지역은 거절, 두 번 서면 AlreadyQueued, 빠지면 Cancelled 를 받고 다시 설 수 있다. 티켓 id 위쪽은 서버 id 다 */
SW_TEST_CASE( MatchQueueServiceTest, JoinAndLeave )
{
    SoloWorld world;
    world._server.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    MatchQueueService& queue = world._server._queue;

    SW_EXPECT_TRUE( world._server.join( kAlice, kNowMs, "nope" )._result == MatchmakingResult::UnknownMode );
    SW_EXPECT_TRUE( world._server.join( kAlice, kNowMs, "duel", "" )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( world._server.join( kAlice, kNowMs, "duel", "Not A Region" )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() );

    const MatchQueueCompletion joined = world._server.join( kAlice );
    SW_EXPECT_TRUE( joined._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( kServerA, joined._ticketID >> 32 );
    SW_EXPECT_EQUAL( 1, queue.getLocalTicketCount() );
    SW_EXPECT_EQUAL( 1, queue.getQueuedTicketCount( "duel" ) );
    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::AlreadyQueued );

    queue.leaveQueue( kBob, 8 );
    SW_EXPECT_TRUE( world._server.takeCompletion()._result == MatchmakingResult::NotQueued );
    queue.leaveQueue( kAlice, 8 );
    const MatchQueueCompletion left = world._server.takeCompletion();
    SW_EXPECT_TRUE( left._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( joined._ticketID, left._ticketID );
    const vector<MatchQueueNotification> listCancelled = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listCancelled.size() );
    SW_EXPECT_TRUE( listCancelled[0]._assignment._result == MatchQueueResult::Cancelled );
    SW_EXPECT_EQUAL( 0, queue.getQueuedTicketCount( "duel" ) );
    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );

    queue.removeAccount( kAlice ); // 떠나도 같이 빠진다
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() );
    SW_EXPECT_EQUAL( 0, queue.getQueuedTicketCount( "duel" ) );
}

/** @brief [MatchQueueServiceTest] 배정: 두 사람이 묶이면 같은 경기 id · 서버 주소를 받고 서로 다른 팀이며, 티켓이 끝나 다시 줄을 설 수 있다 */
SW_TEST_CASE( MatchQueueServiceTest, FormedMatchIsPlacedOnAServer )
{
    SoloWorld world;
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob )._result == MatchmakingResult::Ok );

    world._server.tick( kNowMs + 1 );
    const vector<MatchQueueNotification> listFound = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listFound.size() );
    const MatchAssignment& first  = listFound[0]._assignment;
    const MatchAssignment& second = listFound[1]._assignment;
    SW_EXPECT_TRUE( first._result == MatchQueueResult::Found );
    SW_EXPECT_TRUE( first._matchID != 0 );
    SW_EXPECT_EQUAL( first._matchID, second._matchID );
    SW_EXPECT_EQUAL( kGameID, first._serverID );
    SW_EXPECT_EQUAL( string( "10.0.0.9" ), first._address );
    SW_EXPECT_EQUAL( uint16( 7000 ), first._port );
    SW_EXPECT_TRUE( first._team != second._team );
    SW_REQUIRE_EQUAL( size_t( 1 ), first._listTeammate.size() );
    SW_EXPECT_EQUAL( listFound[0]._recipientID, first._listTeammate[0] );

    SW_EXPECT_EQUAL( 0, world._server._queue.getLocalTicketCount() );
    SW_EXPECT_TRUE( world._server.join( kAlice, kNowMs + 2 )._result == MatchmakingResult::Ok );
}

/** @brief [MatchQueueServiceTest] 서버가 없으면 경기를 쥐고 기다리다 서버가 뜨면 배정하고, 30 초가 지나면 NoServer 로 끝낸다 */
SW_TEST_CASE( MatchQueueServiceTest, UnplacedMatchWaitsThenGivesUp )
{
    SW_EXPECT_LOG( "MatchQueueService.MatchQueueNoServerMatch" );
    SoloWorld world;
    world._server.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob )._result == MatchmakingResult::Ok );
    world._server.tick( kNowMs );
    SW_EXPECT_TRUE( world._server.takeNotifications().empty() );
    SW_EXPECT_EQUAL( 2, world._server._queue.getLocalTicketCount() );

    world._server.tick( kNowMs + MatchQueueLimit::kNoServerGiveUpMs - 1 );
    SW_EXPECT_TRUE( world._server.takeNotifications().empty() );

    world._server.tick( kNowMs + MatchQueueLimit::kNoServerGiveUpMs );
    const vector<MatchQueueNotification> listGaveUp = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listGaveUp.size() );
    SW_EXPECT_TRUE( listGaveUp[0]._assignment._result == MatchQueueResult::NoServer );
    SW_EXPECT_EQUAL( 0, world._server._queue.getLocalTicketCount() );
}

/** @brief [MatchQueueServiceTest] 상대가 없으면 모드 시한에 Timeout 을 받고 티켓이 끝난다 */
SW_TEST_CASE( MatchQueueServiceTest, TicketTimesOutAtModeDeadline )
{
    SoloWorld world;
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    world._server.tick( kNowMs + kMaxWait + 1 );
    const vector<MatchQueueNotification> listTimedOut = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listTimedOut.size() );
    SW_EXPECT_TRUE( listTimedOut[0]._assignment._result == MatchQueueResult::Timeout );
    SW_EXPECT_EQUAL( string( "duel" ), listTimedOut[0]._assignment._modeID );
    SW_EXPECT_EQUAL( 0, world._server._queue.getLocalTicketCount() );
}

/** @brief [MatchQueueServiceTest] 실력: 값이 있으면 그 값, 읽었지만 없으면 초기 실력으로 묶는다 — 실력 차가 크면 묶이지 않는다 */
SW_TEST_CASE( MatchQueueServiceTest, RatingComesFromStatSource )
{
    SoloWorld              world;
    FakeStatSource         stats;
    MatchQueueDependencies dependencies = world._server.makeDependencies();
    dependencies._pStatSource           = &stats;
    MatchModeDef mode                   = MatchQueueTestUtil::makeMode( "duel", 1, "rating" );
    mode._ratingWindowPerSecond         = 0;
    mode._baseRatingWindow              = 100;
    SW_REQUIRE_TRUE( world._server._queue.initialize( dependencies, vector<MatchModeDef>{ mode } ) );
    world._game.start( world._server._router, kNowMs );
    world._server.pump( kNowMs );
    world._server.tick( kNowMs );
    world._server.tick( kNowMs );

    stats._mapStat[kAlice] = 3000;
    stats._mapStat[kBob]   = -1; // 한 번도 바뀌지 않았다 — 초기 실력 1500
    stats._mapStat[kCarol] = 3050;
    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob )._result == MatchmakingResult::Ok );
    world._server.tick( kNowMs + 1 );
    SW_EXPECT_TRUE( world._server.takeNotifications().empty() ); // 3000 과 1500 은 묶이지 않는다

    SW_EXPECT_TRUE( world._server.join( kCarol )._result == MatchmakingResult::Ok );
    world._server.tick( kNowMs + 2 );
    const vector<MatchQueueNotification> listFound = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listFound.size() );
    for ( const MatchQueueNotification& notification : listFound )
    {
        SW_EXPECT_TRUE( notification._recipientID == kAlice || notification._recipientID == kCarol );
    }
}

/** @brief [MatchQueueServiceTest] 서버 둘: 임대를 잡은 A 가 권한이고, B 에서 선 티켓도 버스로 A 의 매처에 들어가 묶이며, 결과는 티켓을 낸 서버가 받는다. 게임 서버 주제로 접속 명단이 간다 */
SW_TEST_CASE( MatchQueueServiceTest, AuthorityRunsMatchesAcrossServers )
{
    PairWorld world;
    world.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    LocalServerBus gameBus( &world._hub, kGameID );
    gameBus.subscribe( MatchQueueService::makeAssignTopic( kGameID ) );

    SW_EXPECT_TRUE( world._serverA.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._serverB.join( kBob )._result == MatchmakingResult::Ok );
    world._serverB.pump( kNowMs );
    world._serverA.pump( kNowMs );
    SW_EXPECT_EQUAL( 2, world._serverA._queue.getQueuedTicketCount( "duel" ) );
    SW_EXPECT_EQUAL( 0, world._serverB._queue.getQueuedTicketCount( "duel" ) );

    world.tickBoth( kNowMs + 1 );
    const vector<MatchQueueNotification> listA = world._serverA.takeNotifications();
    const vector<MatchQueueNotification> listB = world._serverB.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listA.size() );
    SW_REQUIRE_EQUAL( size_t( 1 ), listB.size() );
    SW_EXPECT_EQUAL( kAlice, listA[0]._recipientID );
    SW_EXPECT_EQUAL( kBob, listB[0]._recipientID );
    SW_EXPECT_EQUAL( listA[0]._assignment._matchID, listB[0]._assignment._matchID );
    SW_EXPECT_TRUE( listB[0]._assignment._result == MatchQueueResult::Found );
    SW_EXPECT_EQUAL( 0, world._serverB._queue.getLocalTicketCount() );

    vector<ServerBusMessage> listGame;
    SW_EXPECT_EQUAL( 1, gameBus.pollMessages( listGame ) );
}

/** @brief [MatchQueueServiceTest] B 에서 빠지면 취소가 버스로 권한 매처까지 간다 */
SW_TEST_CASE( MatchQueueServiceTest, CancelReachesAuthorityOverBus )
{
    PairWorld world;
    world.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    SW_EXPECT_TRUE( world._serverB.join( kBob )._result == MatchmakingResult::Ok );
    world._serverA.pump( kNowMs );
    SW_EXPECT_EQUAL( 1, world._serverA._queue.getQueuedTicketCount( "duel" ) );

    world._serverB._queue.leaveQueue( kBob, 8 );
    world._serverA.pump( kNowMs );
    SW_EXPECT_EQUAL( 0, world._serverA._queue.getQueuedTicketCount( "duel" ) );
    SW_EXPECT_EQUAL( 0, world._serverB._queue.getLocalTicketCount() );
}

/** @brief [MatchQueueServiceTest] 권한 서버가 멎으면 임대가 끝나 B 가 이어받는다. A 가 돌아와도 임대를 잃은 줄 알고 매처를 비우며, 사라진 티켓은 낸 서버의 시한이 Timeout 으로 알린다 */
SW_TEST_CASE( MatchQueueServiceTest, AuthorityFailsOverAndLostTicketsTimeOut )
{
    PairWorld world;
    world.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    SW_EXPECT_TRUE( world._serverB.join( kBob )._result == MatchmakingResult::Ok );
    world._serverA.pump( kNowMs );
    SW_EXPECT_EQUAL( 1, world._serverA._queue.getQueuedTicketCount( "duel" ) );

    world._clock.advanceMilliseconds( MatchQueueLimit::kLeaseTTLMs + 1 );
    const int64 takeoverMs = kNowMs + MatchQueueLimit::kLeaseTTLMs + 1;
    world._serverB.tick( takeoverMs ); // A 는 돌지 않는다
    SW_EXPECT_TRUE( world._serverB._queue.isAuthority( "duel" ) );

    world._serverA.tick( takeoverMs ); // 돌아온 A 는 비교 후 쓰기에 진다
    SW_EXPECT_FALSE( world._serverA._queue.isAuthority( "duel" ) );
    SW_EXPECT_EQUAL( 0, world._serverA._queue.getQueuedTicketCount( "duel" ) );

    SW_EXPECT_TRUE( world._serverB.takeNotifications().empty() );
    const int64 deadlineMs = kNowMs + kMaxWait + MatchQueueLimit::kNoServerGiveUpMs + MatchQueueLimit::kResultGraceMs;
    world._serverB.tick( deadlineMs );
    SW_EXPECT_TRUE( world._serverB.takeNotifications().empty() ); // 시한과 같은 시각은 아직 안 끝났다
    world._serverB.tick( deadlineMs + 1 );
    const vector<MatchQueueNotification> listTimedOut = world._serverB.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listTimedOut.size() );
    SW_EXPECT_TRUE( listTimedOut[0]._assignment._result == MatchQueueResult::Timeout );
}

/** @brief [MatchQueueServiceTest] 백필: 경기 중 빈 슬롯을 새 경기보다 먼저 채우고, 들어갈 사람은 그 경기 id · 게임 서버 주소를 받는다. 모르는 모드 · 모양이 틀린 요청은 거절한다 */
SW_TEST_CASE( MatchQueueServiceTest, BackfillFillsRunningMatchFirst )
{
    SW_EXPECT_LOG( "MatchQueueService.MatchQueueBackfillUnknownMode" );
    SW_EXPECT_LOG( "MatchQueueService.MatchQueueBackfillMatchDoes" );
    SoloWorld world;
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );

    MatchBackfillRequest request;
    request._matchID       = 77;
    request._modeID        = "duel";
    request._region        = "kr";
    request._serverID      = kGameID;
    request._address       = "10.0.0.5";
    request._port          = 7100;
    request._portCount     = 1;
    request._averageRating = 1500;
    request._listOpenSlot  = vector<int32>{ 0, 1 };

    MatchBackfillRequest unknownMode = request;
    unknownMode._modeID              = "nope";
    world._server._queue.submitBackfill( unknownMode );
    MatchBackfillRequest badShape = request;
    badShape._listOpenSlot        = vector<int32>{ 1 };
    world._server._queue.submitBackfill( badShape );

    world._server._queue.submitBackfill( request );
    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    world._server.tick( kNowMs + 1 );
    const vector<MatchQueueNotification> listFound = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listFound.size() );
    SW_EXPECT_TRUE( listFound[0]._assignment._result == MatchQueueResult::Found );
    SW_EXPECT_EQUAL( uint64( 77 ), listFound[0]._assignment._matchID );
    SW_EXPECT_EQUAL( string( "10.0.0.5" ), listFound[0]._assignment._address );
    SW_EXPECT_EQUAL( 1, listFound[0]._assignment._team );
    SW_EXPECT_EQUAL( 0, world._server._queue.getLocalTicketCount() );
}

/** @brief [MatchQueueServiceTest] 로비 배정: 팀은 로비 그대로이고 멤버마다 알린다. 서버가 없거나 모르는 모드면 NoServer 다. 경기 id 는 매처의 것과 겹치지 않는다 */
SW_TEST_CASE( MatchQueueServiceTest, LobbyPlacementKeepsTeams )
{
    SoloWorld world;
    world._server.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );

    LobbySnapshot lobby;
    lobby._lobbyID = 9;
    lobby._modeID  = "duel";
    lobby._listMember.push_back( LobbyMember{ kAlice, 0, SW_TRUE } );
    lobby._listMember.push_back( LobbyMember{ kBob, 1, SW_TRUE } );

    world._server._queue.placeLobby( lobby, UnixMilliseconds{ kNowMs } ); // 서버가 없다
    vector<MatchQueueNotification> listResult = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._assignment._result == MatchQueueResult::NoServer );

    world._game.start( world._server._router, kNowMs, "eu" ); // 로비는 다른 지역도 받는다
    world._server.pump( kNowMs );
    world._server.tick( kNowMs + 2001 );
    world._server.tick( kNowMs + 2001 );
    world._server._queue.placeLobby( lobby, UnixMilliseconds{ kNowMs + 2001 } );
    listResult = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._assignment._result == MatchQueueResult::Found );
    SW_EXPECT_EQUAL( kGameID, listResult[0]._assignment._serverID );
    SW_EXPECT_EQUAL( 0, listResult[0]._assignment._team );
    SW_EXPECT_EQUAL( 1, listResult[1]._assignment._team );
    SW_EXPECT_TRUE( ( listResult[0]._assignment._matchID & MatchQueueLimit::kLobbyMatchBit ) != 0 );

    lobby._modeID = "nope";
    world._server._queue.placeLobby( lobby, UnixMilliseconds{ kNowMs + 2001 } );
    listResult = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._assignment._result == MatchQueueResult::NoServer );
}

namespace
{
    /** @brief 서버 한 대 + 실제 파티 서비스입니다. */
    struct PartyQueueWorld
    {
        SoloWorld         _world;
        PartyLobbyService _party;

        explicit PartyQueueWorld( int32 teamSize )
            : _world{}
            , _party{}
        {
            SW_EXPECT_TRUE( _party.initialize( &_world._server._router, kServerA ) );
            MatchQueueDependencies dependencies = _world._server.makeDependencies();
            dependencies._pPartyLobby           = &_party;
            SW_EXPECT_TRUE( _world._server._queue.initialize( dependencies, vector<MatchModeDef>{ MatchQueueTestUtil::makeMode( "duel", teamSize ) } ) );
        }

        ~PartyQueueWorld() { _party.shutdown(); }

        void pump() { _world._server.pump( kNowMs ); }

        /** @brief 파티 하나를 만듭니다. 파티 id 입니다. */
        uint64 makeParty( AccountID leaderID, AccountID memberID )
        {
            _party.createParty( leaderID, 1 );
            pump();
            vector<PartyLobbyCompletion> listCompletion;
            _party.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            const uint64 partyID = listCompletion.empty() ? 0 : listCompletion[0]._party._partyID;
            _party.inviteToParty( leaderID, memberID, 2 );
            pump();
            _party.acceptPartyInvite( memberID, partyID, 3 );
            pump();
            listCompletion.clear();
            _party.takeCompletions( listCompletion );
            for ( const PartyLobbyCompletion& completion : listCompletion )
            {
                SW_EXPECT_TRUE( completion._result == MatchmakingResult::Ok );
            }
            return partyID;
        }

        MatchQueueCompletion joinAndWait( AccountID accountID )
        {
            _world._server._queue.joinQueue( accountID, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
            pump();
            return _world._server.takeCompletion();
        }
    };
} // namespace

/** @brief [MatchQueueServiceTest] 파티: 파티장이 서면 파티 전체의 티켓이고 파티원은 NotPartyLeader, 파티원이 떠나면 티켓이 함께 끝나며 모두에게 Cancelled 가 간다 */
SW_TEST_CASE( MatchQueueServiceTest, PartyLeaderQueuesWholeParty )
{
    PartyQueueWorld party( 2 );
    (void)party.makeParty( kAlice, kBob );

    SW_EXPECT_TRUE( party.joinAndWait( kBob )._result == MatchmakingResult::NotPartyLeader );
    SW_EXPECT_EQUAL( 0, party._world._server._queue.getLocalTicketCount() );
    SW_EXPECT_TRUE( party.joinAndWait( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( 1, party._world._server._queue.getLocalTicketCount() );
    SW_EXPECT_TRUE( party.joinAndWait( kAlice )._result == MatchmakingResult::AlreadyQueued );

    party._world._server._queue.leaveQueue( kBob, 8 ); // 파티원이 빠져도 파티 티켓이 빠진다
    SW_EXPECT_TRUE( party._world._server.takeCompletion()._result == MatchmakingResult::Ok );
    const vector<MatchQueueNotification> listCancelled = party._world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listCancelled.size() );
    SW_EXPECT_TRUE( listCancelled[0]._assignment._result == MatchQueueResult::Cancelled );
    SW_EXPECT_EQUAL( 0, party._world._server._queue.getLocalTicketCount() );
    party.pump();
    SW_EXPECT_TRUE( party.joinAndWait( kAlice )._result == MatchmakingResult::Ok ); // 파티 기록의 티켓도 지워졌다
}

/** @brief [MatchQueueServiceTest] 파티 인원이 팀 인원을 넘으면 PartyTooLarge 이고 티켓이 없다 */
SW_TEST_CASE( MatchQueueServiceTest, PartyLargerThanTeamIsRefused )
{
    PartyQueueWorld party( 1 );
    (void)party.makeParty( kAlice, kBob );
    SW_EXPECT_TRUE( party.joinAndWait( kAlice )._result == MatchmakingResult::PartyTooLarge );
    SW_EXPECT_EQUAL( 0, party._world._server._queue.getLocalTicketCount() );
}

/** @brief [MatchQueueServiceTest] 파티를 찾는 동안 떠나거나 빠지면 티켓이 생기지 않는다 — 없는 사람이 경기에 묶이지 않는다. 줄 서기 요청은 NotQueued 로 끝난다 */
SW_TEST_CASE( MatchQueueServiceTest, LeavingDuringPartyLookupLeavesNoTicket )
{
    PartyQueueWorld    party( 1 );
    MatchQueueService& queue = party._world._server._queue;

    queue.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
    queue.removeAccount( kAlice ); // 파티 답이 오기 전
    SW_EXPECT_TRUE( party._world._server.takeCompletion()._result == MatchmakingResult::NotQueued );
    party.pump();
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() );

    queue.joinQueue( kBob, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
    queue.leaveQueue( kBob, 8 );
    vector<MatchQueueCompletion> listCompletion;
    queue.takeCompletions( listCompletion );
    SW_REQUIRE_EQUAL( size_t( 2 ), listCompletion.size() );
    SW_EXPECT_TRUE( listCompletion[0]._result == MatchmakingResult::NotQueued );
    SW_EXPECT_EQUAL( uint64( 7 ), listCompletion[0]._requestTag );
    SW_EXPECT_TRUE( listCompletion[1]._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( uint64( 8 ), listCompletion[1]._requestTag );
    party.pump();
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() );

    queue.joinQueue( kCarol, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 ); // 평소의 길은 그대로다
    party.pump();
    SW_EXPECT_TRUE( party._world._server.takeCompletion()._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( 1, queue.getLocalTicketCount() );
}

/** @brief [MatchQueueServiceTest] 메모리에 통계가 없는 계정은 올려 달라고 한 뒤 기다렸다 줄을 선다 — 다른 서버의 파티원도 실력 모드에 설 수 있다. 끝나면 내려 달라고 한다 */
SW_TEST_CASE( MatchQueueServiceTest, MissingRatingIsLoadedBeforeQueueing )
{
    SoloWorld              world;
    FakeStatSource         stats;
    MatchQueueDependencies dependencies = world._server.makeDependencies();
    dependencies._pStatSource           = &stats;
    SW_REQUIRE_TRUE( world._server._queue.initialize( dependencies, vector<MatchModeDef>{ MatchQueueTestUtil::makeMode( "duel", 1, "rating" ) } ) );
    MatchQueueService& queue = world._server._queue;

    queue.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
    SW_EXPECT_EQUAL( 1, stats._mapRetained[kAlice] );
    vector<MatchQueueCompletion> listCompletion;
    queue.takeCompletions( listCompletion );
    SW_EXPECT_TRUE( listCompletion.empty() ); // 기다린다
    queue.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs }, 8 );
    SW_EXPECT_TRUE( world._server.takeCompletion()._result == MatchmakingResult::AlreadyQueued );

    world._server.tick( kNowMs + 1 );
    queue.takeCompletions( listCompletion );
    SW_EXPECT_TRUE( listCompletion.empty() ); // 아직 읽지 못했다

    stats._mapStat[kAlice] = 1600;
    world._server.tick( kNowMs + 2 );
    const MatchQueueCompletion joined = world._server.takeCompletion();
    SW_EXPECT_TRUE( joined._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( uint64( 7 ), joined._requestTag );
    SW_EXPECT_EQUAL( 1, queue.getQueuedTicketCount( "duel" ) );
    SW_EXPECT_EQUAL( 0, stats._mapRetained[kAlice] );
}

/** @brief [MatchQueueServiceTest] 시한까지 통계를 읽지 못하면 RatingUnavailable 이고 티켓이 없다. 기다리는 동안 떠나면 NotQueued 로 끝나고 내려 달라고 한다 */
SW_TEST_CASE( MatchQueueServiceTest, RatingWaitGivesUpOrIsDropped )
{
    SoloWorld              world;
    FakeStatSource         stats;
    MatchQueueDependencies dependencies = world._server.makeDependencies();
    dependencies._pStatSource           = &stats;
    SW_REQUIRE_TRUE( world._server._queue.initialize( dependencies, vector<MatchModeDef>{ MatchQueueTestUtil::makeMode( "duel", 1, "rating" ) } ) );
    MatchQueueService& queue = world._server._queue;

    queue.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
    world._server.tick( kNowMs + MatchQueueLimit::kRatingWaitMs );
    SW_EXPECT_TRUE( world._server.takeCompletion()._result == MatchmakingResult::RatingUnavailable );
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() );
    SW_EXPECT_EQUAL( 0, stats._mapRetained[kAlice] );

    queue.joinQueue( kBob, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
    SW_EXPECT_EQUAL( 1, stats._mapRetained[kBob] );
    queue.removeAccount( kBob );
    SW_EXPECT_TRUE( world._server.takeCompletion()._result == MatchmakingResult::NotQueued );
    SW_EXPECT_EQUAL( 0, stats._mapRetained[kBob] );
    stats._mapStat[kBob] = 1500;
    world._server.tick( kNowMs + 1 );
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() ); // 떠난 사람의 티켓이 생기지 않는다
}
