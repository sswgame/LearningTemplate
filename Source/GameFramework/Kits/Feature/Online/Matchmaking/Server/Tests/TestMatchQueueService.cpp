// MatchQueueServiceTest — 매칭 대기열(메모리 캐시 + 프로세스 안 버스): 줄 서기 · 빠지기, 경기 배정 · 팀 · 샤드 포트, 서버 없음 대기와 포기, 시한, 실력 통계,
// 서버 둘의 임대 · 버스 티켓 · 결과 · 취소, 권한 넘어감과 잃은 티켓의 시한, 백필(게임 서버 입구 포함), 로비 배정, 명단 기록 쓰기 실패, 파티 티켓과 파티를 찾는 동안의 빠지기.
// 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/MatchmakingKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchBackfillSender.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Tests/MatchmakingTestPolicy.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountStatSource.h"
#include "GameFramework/Runtime/Online/Bus/Server/LocalServerBus.h"
#include "GameFramework/Runtime/Online/Bus/ServerBus.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"
#include "GameFramework/Runtime/Online/Directory/Server/ServerRegistration.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

using namespace sw;

namespace
{
    constexpr int64     kNowMs          = 1700000000000ll;
    constexpr int64     kMaxWait        = 60000;
    constexpr int64     kMatchTimeLimit = 600000;
    constexpr uint64    kGameID         = 500;
    constexpr AccountID kAlice          = 11;
    constexpr AccountID kBob            = 12;
    constexpr AccountID kCarol          = 13;
    constexpr AccountID kDave           = 14;
    constexpr uint64    kServerA        = 1;
    constexpr uint64    kServerB        = 2;

    /** @brief 1 대 1 모드입니다. @p teamSize 를 키우면 파티 모드입니다. */
    struct MatchQueueTestUtil
    {
        static MatchModeDef makeMode( string_view modeID = "duel", int32 teamSize = 1, string_view ratingStat = "" )
        {
            MatchModeDef mode;
            mode._modeID           = string( modeID );
            mode._teamCount        = 2;
            mode._teamSize         = teamSize;
            mode._maxWaitMs        = kMaxWait;
            mode._matchTimeLimitMs = kMatchTimeLimit;
            mode._ratingStat       = string( ratingStat );
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

    /** @brief 메모리 캐시 앞에서 명단 기록(`mm/match/`) 쓰기를 지게 할 수 있는 캐시입니다. 진 쓰기는 키를 비워 Invalid 답을 받는다. */
    class RecordFaultEphemeralStore final : public IEphemeralStore
    {
    public:
        uint8 _bFailMatchRecord{ SW_FALSE };

        explicit RecordFaultEphemeralStore( IEphemeralStore* pInner )
            : _pInner{ pInner }
        {
        }

        uint64 submit( const EphemeralRequest& request ) override
        {
            const bool bFail = _bFailMatchRecord != SW_FALSE && request._operation == EphemeralOperation::Set && request._key.rfind( "mm/match/", 0 ) == 0;
            if ( bFail == false )
                return _pInner->submit( request );
            EphemeralRequest broken = request;
            broken._key.clear();
            return _pInner->submit( broken );
        }

        int32 pollReplies( vector<EphemeralReply>& outListReply ) override { return _pInner->pollReplies( outListReply ); }
        void  subscribe( string_view channel ) override { _pInner->subscribe( channel ); }
        void  unsubscribe( string_view channel ) override { _pInner->unsubscribe( channel ); }
        int32 pollMessages( vector<EphemeralMessage>& outListMessage ) override { return _pInner->pollMessages( outListMessage ); }
        int32 getPendingCount() const override { return _pInner->getPendingCount(); }
        void  shutdown() override { _pInner->shutdown(); }

    private:
        IEphemeralStore* _pInner;
    };

    /** @brief 서버 하나입니다. 캐시 데이터와 버스 허브는 빌립니다. @p pHub 가 없으면 서버 한 대(항상 권한)입니다. */
    struct QueueServer
    {
        MemoryEphemeralStore       _cache;
        RecordFaultEphemeralStore  _faultCache;
        EphemeralStoreRouter       _router;
        unique_ptr<LocalServerBus> _transport;
        unique_ptr<ServerBus>      _bus;
        MatchQueueService          _queue;

        QueueServer( MemoryEphemeralDatabase& cacheData, LocalServerBusHub* pHub, uint64 serverID )
            : _cache{ &cacheData }
            , _faultCache{ &_cache }
            , _router{}
            , _transport{}
            , _bus{}
            , _queue{}
            , _serverID{ serverID }
        {
            _router.initialize( &_faultCache );
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
            dependencies._pRouter              = &_router;
            dependencies._seatReservationTTLMs = 30000;
            dependencies._cancelConfirmWaitMs  = 3000;
            dependencies._policy               = test::MatchmakingTestPolicy::makeQueue();
            dependencies._pBus                 = _bus.get();
            dependencies._serverID             = _serverID;
            return dependencies;
        }

        void start( const vector<MatchModeDef>& listMode ) { SW_EXPECT_TRUE( _queue.initialize( makeDependencies(), listMode ) ); }

        /** @brief 임대 · 서버 목록 · 매처를 한 번 돌리고, 캐시 답과 버스 메시지를 끝까지 나눠 줍니다. */
        void update( int64 nowMs )
        {
            _queue.update( UnixMilliseconds{ nowMs } );
            poll( nowMs );
        }

        void poll( int64 nowMs )
        {
            for ( int32 round = 0; round < 8; ++round )
            {
                int32 job = _router.poll();
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
                    job += _transport->pollMessages( listMessage );
                    for ( const ServerBusMessage& message : listMessage )
                    {
                        _queue.handleBusMessage( message, UnixMilliseconds{ nowMs } );
                    }
                }
                if ( job == 0 )
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

        void start( EphemeralStoreRouter& router, int64 nowMs, string_view region = "kr", uint64 serverID = kGameID, int32 capacity = 100 )
        {
            ServerDescriptor descriptor;
            descriptor._serverID  = serverID;
            descriptor._kind      = "game";
            descriptor._region    = string( region );
            descriptor._address   = "10.0.0.9";
            descriptor._capacity  = capacity;
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
            _server.poll( kNowMs );
            _server.update( kNowMs ); // 첫 읽기를 맡긴다
            _server.update( kNowMs ); // 답을 스냅숏에 넣는다
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

        void updateBoth( int64 nowMs )
        {
            for ( int32 round = 0; round < 3; ++round )
            {
                _serverA.update( nowMs );
                _serverB.update( nowMs );
            }
        }

        /** @brief A 가 먼저 임대를 잡게 하고 게임 서버를 올립니다. */
        void start( const vector<MatchModeDef>& listMode )
        {
            _serverA.start( listMode );
            _serverB.start( listMode );
            _game.start( _serverA._router, kNowMs );
            _serverA.update( kNowMs );
            updateBoth( kNowMs );
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

    world._server.update( kNowMs + 1 );
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
    world._server.update( kNowMs );
    SW_EXPECT_TRUE( world._server.takeNotifications().empty() );
    SW_EXPECT_EQUAL( 2, world._server._queue.getLocalTicketCount() );

    world._server.update( kNowMs + test::MatchmakingTestPolicy::makeQueue()._noServerGiveUpMs - 1 );
    SW_EXPECT_TRUE( world._server.takeNotifications().empty() );

    world._server.update( kNowMs + test::MatchmakingTestPolicy::makeQueue()._noServerGiveUpMs );
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
    world._server.update( kNowMs + kMaxWait + 1 );
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
    world._server.poll( kNowMs );
    world._server.update( kNowMs );
    world._server.update( kNowMs );

    stats._mapStat[kAlice] = 3000;
    stats._mapStat[kBob]   = -1; // 한 번도 바뀌지 않았다 — 초기 실력 1500
    stats._mapStat[kCarol] = 3050;
    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 1 );
    SW_EXPECT_TRUE( world._server.takeNotifications().empty() ); // 3000 과 1500 은 묶이지 않는다

    SW_EXPECT_TRUE( world._server.join( kCarol )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 2 );
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
    world._serverB.poll( kNowMs );
    world._serverA.poll( kNowMs );
    SW_EXPECT_EQUAL( 2, world._serverA._queue.getQueuedTicketCount( "duel" ) );
    SW_EXPECT_EQUAL( 0, world._serverB._queue.getQueuedTicketCount( "duel" ) );

    world.updateBoth( kNowMs + 1 );
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
    world._serverA.poll( kNowMs );
    SW_EXPECT_EQUAL( 1, world._serverA._queue.getQueuedTicketCount( "duel" ) );

    world._serverB._queue.leaveQueue( kBob, 8 );
    SW_EXPECT_EQUAL( 1, world._serverB._queue.getLocalTicketCount() ); // 권한 서버의 확인을 기다린다
    world._serverA.poll( kNowMs );
    SW_EXPECT_EQUAL( 0, world._serverA._queue.getQueuedTicketCount( "duel" ) );
    world._serverB.poll( kNowMs );
    SW_EXPECT_EQUAL( 0, world._serverB._queue.getLocalTicketCount() );
    SW_EXPECT_TRUE( world._serverB.takeCompletion()._result == MatchmakingResult::Ok );
}

/** @brief [MatchQueueServiceTest] 빠지기와 경기가 겹치면 경기가 이긴다 — 권한 서버가 이미 짝지었으면 Cancelled 를 내지 않고, 낸 서버는 Found 를 받아 빠지기를 NotQueued 로 끝낸다(감사 O-18: 예전에는 바로 Cancelled 로 끝내 늦게 온 Found 를 버렸다) */
SW_TEST_CASE( MatchQueueServiceTest, LeaveRacingAFormedMatchYieldsToFound )
{
    PairWorld world;
    world.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    SW_EXPECT_TRUE( world._serverB.join( kBob )._result == MatchmakingResult::Ok );
    world._serverA.poll( kNowMs );
    SW_EXPECT_TRUE( world._serverA.join( kAlice )._result == MatchmakingResult::Ok );
    world._serverA.update( kNowMs + 1 ); // 경기가 났다 — 밥의 Found 는 아직 버스 위다
    world._serverB._queue.leaveQueue( kBob, 9 );
    world._serverA.poll( kNowMs + 1 ); // 이미 짝지어졌으니 빼지 않는다
    world._serverB.poll( kNowMs + 1 );
    const vector<MatchQueueNotification> listBob = world._serverB.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listBob.size() );
    SW_EXPECT_TRUE( listBob[0]._assignment._result == MatchQueueResult::Found );
    SW_EXPECT_TRUE( world._serverB.takeCompletion()._result == MatchmakingResult::NotQueued );
}

/** @brief [MatchQueueServiceTest] 확인을 기다리는 동안 다시 빠지기를 낸 요청도 모두 답을 받는다(결함: 앞 요청의 태그를 덮어 그 호출이 답 없이 남았다) */
SW_TEST_CASE( MatchQueueServiceTest, EveryLeaveRequestIsAnswered )
{
    PairWorld world;
    world.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    SW_REQUIRE_TRUE( world._serverB.join( kAlice )._result == MatchmakingResult::Ok );
    world.updateBoth( kNowMs + 1 );

    world._serverB._queue.leaveQueue( kAlice, 8 );
    world._serverB._queue.leaveQueue( kAlice, 9 ); // 권한 서버(A)의 확인이 오기 전
    world.updateBoth( kNowMs + 2 );
    vector<MatchQueueCompletion> listCompletion;
    world._serverB._queue.takeCompletions( listCompletion );
    SW_REQUIRE_EQUAL( size_t( 2 ), listCompletion.size() );
    SW_EXPECT_EQUAL( uint64( 8 ), listCompletion[0]._requestTag );
    SW_EXPECT_TRUE( listCompletion[0]._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( uint64( 9 ), listCompletion[1]._requestTag );
    SW_EXPECT_TRUE( listCompletion[1]._result == MatchmakingResult::Ok );
}

/** @brief [MatchQueueServiceTest] 권한 서버의 답이 시한 안에 오지 않으면(권한이 없다) 빠진 것으로 끝낸다 */
SW_TEST_CASE( MatchQueueServiceTest, LeaveWithoutAuthorityEndsAtTheConfirmDeadline )
{
    PairWorld world;
    world.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    SW_EXPECT_TRUE( world._serverB.join( kBob )._result == MatchmakingResult::Ok );
    // 권한 서버 A 를 돌리지 않는다 — 빼 달라는 버스 메시지에 아무도 답하지 않는다
    world._serverB._queue.leaveQueue( kBob, 8 );
    world._serverB.update( kNowMs + 3000 );
    SW_EXPECT_EQUAL( 1, world._serverB._queue.getLocalTicketCount() );
    world._serverB.update( kNowMs + 3001 );
    SW_EXPECT_EQUAL( 0, world._serverB._queue.getLocalTicketCount() );
    SW_EXPECT_TRUE( world._serverB.takeCompletion()._result == MatchmakingResult::Ok );
}

/** @brief [MatchQueueServiceTest] 권한 서버가 멎으면 임대가 끝나 B 가 이어받는다. A 가 돌아와도 임대를 잃은 줄 알고 매처를 비우며, 사라진 티켓은 낸 서버의 시한이 Timeout 으로 알린다 */
SW_TEST_CASE( MatchQueueServiceTest, AuthorityFailsOverAndLostTicketsTimeOut )
{
    PairWorld world;
    world.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    SW_EXPECT_TRUE( world._serverB.join( kBob )._result == MatchmakingResult::Ok );
    world._serverA.poll( kNowMs );
    SW_EXPECT_EQUAL( 1, world._serverA._queue.getQueuedTicketCount( "duel" ) );

    world._clock.advanceMilliseconds( test::MatchmakingTestPolicy::makeQueue()._leaseTTLMs + 1 );
    const int64 takeoverMs = kNowMs + test::MatchmakingTestPolicy::makeQueue()._leaseTTLMs + 1;
    world._serverB.update( takeoverMs ); // A 는 돌지 않는다
    SW_EXPECT_TRUE( world._serverB._queue.isAuthority( "duel" ) );

    world._serverA.update( takeoverMs ); // 돌아온 A 는 비교 후 쓰기에 진다
    SW_EXPECT_FALSE( world._serverA._queue.isAuthority( "duel" ) );
    SW_EXPECT_EQUAL( 0, world._serverA._queue.getQueuedTicketCount( "duel" ) );

    SW_EXPECT_TRUE( world._serverB.takeNotifications().empty() );
    const int64 deadlineMs = kNowMs + kMaxWait + test::MatchmakingTestPolicy::makeQueue()._noServerGiveUpMs + test::MatchmakingTestPolicy::makeQueue()._resultGraceMs;
    world._serverB.update( deadlineMs );
    SW_EXPECT_TRUE( world._serverB.takeNotifications().empty() ); // 시한과 같은 시각은 아직 안 끝났다
    world._serverB.update( deadlineMs + 1 );
    const vector<MatchQueueNotification> listTimedOut = world._serverB.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listTimedOut.size() );
    SW_EXPECT_TRUE( listTimedOut[0]._assignment._result == MatchQueueResult::Timeout );
}

/** @brief [MatchQueueServiceTest] 게임 서버는 배정 버스를 잃어도 명단 기록으로 접속을 받는다(감사 O-6). 기록은 경기 시한까지 살아 경기 중 재접속도 받고, 시한이 지나면 거절한다 */
SW_TEST_CASE( MatchQueueServiceTest, GameServerAcceptsFromMatchRecordWithoutBus )
{
    SoloWorld world; // 버스가 없다 — 배정 버스 메시지가 하나도 닿지 않는 경우(잃은 것과 같다)
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    MatchServerAgent agent;
    agent.initialize( kGameID, 100, test::MatchmakingTestPolicy::makeAgent() );
    agent.startReading( &world._server._router );

    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 1 );
    const vector<MatchQueueNotification> listFound = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listFound.size() );
    SW_EXPECT_TRUE( listFound[0]._assignment._result == MatchQueueResult::Found );
    SW_EXPECT_FALSE( agent.isAccepted( kAlice ) ); // 아직 기록을 읽지 않았다

    agent.update( UnixMilliseconds{ kNowMs + 1 } );
    world._server.poll( kNowMs + 1 );
    SW_EXPECT_TRUE( agent.isAccepted( kAlice ) ); // Found 인데 거절되던 자리이다
    SW_EXPECT_TRUE( agent.isAccepted( kBob ) );
    SW_EXPECT_FALSE( agent.isAccepted( kCarol ) );

    const int64 reconnectMs = kNowMs + 1 + test::MatchmakingTestPolicy::makeAgent()._expectTTLMs + 1; // 버스 명단이었으면 지워졌을 시각이다
    agent.update( UnixMilliseconds{ reconnectMs } );
    world._server.poll( reconnectMs );
    SW_EXPECT_TRUE( agent.isAccepted( kAlice ) );

    agent.update( UnixMilliseconds{ kNowMs + 1 + kMatchTimeLimit } );
    SW_EXPECT_FALSE( agent.isAccepted( kAlice ) );
    agent.shutdown();
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
    SW_EXPECT_TRUE( world._server._queue.submitBackfill( unknownMode ) == MatchmakingResult::UnknownMode );
    MatchBackfillRequest badShape = request;
    badShape._listOpenSlot        = vector<int32>{ 1 };
    SW_EXPECT_TRUE( world._server._queue.submitBackfill( badShape ).hasError( OnlineError::kInvalidRequest ) );

    SW_EXPECT_TRUE( world._server._queue.submitBackfill( request ) == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 1 );
    const vector<MatchQueueNotification> listFound = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listFound.size() );
    SW_EXPECT_TRUE( listFound[0]._assignment._result == MatchQueueResult::Found );
    SW_EXPECT_EQUAL( uint64( 77 ), listFound[0]._assignment._matchID );
    SW_EXPECT_EQUAL( string( "10.0.0.5" ), listFound[0]._assignment._address );
    SW_EXPECT_EQUAL( 1, listFound[0]._assignment._team );
    SW_EXPECT_EQUAL( 0, world._server._queue.getLocalTicketCount() );
}

/** @brief [MatchQueueServiceTest] 명단 기록 쓰기가 지면 Found 대신 NoServer 다 — 게임 서버가 수락을 판정할 기록이 없는 배정에 Found 를 내면 그 사람은 접속에서 거절된다(감사 O-6). 로비도 같다 */
SW_TEST_CASE( MatchQueueServiceTest, MatchRecordWriteFailureEndsAsNoServer )
{
    SW_EXPECT_LOG( "MatchQueueService.MatchRecordWriteFailed" );
    SoloWorld world;
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    MatchServerAgent agent;
    agent.initialize( kGameID, 100, test::MatchmakingTestPolicy::makeAgent() );
    agent.startReading( &world._server._router );
    world._server._faultCache._bFailMatchRecord = SW_TRUE;

    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 1 );
    const vector<MatchQueueNotification> listResult = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._assignment._result == MatchQueueResult::NoServer );
    SW_EXPECT_TRUE( listResult[1]._assignment._result == MatchQueueResult::NoServer );
    SW_EXPECT_EQUAL( uint64( 0 ), listResult[0]._assignment._serverID );
    SW_EXPECT_EQUAL( 0, world._server._queue.getLocalTicketCount() ); // 티켓이 끝나 다시 줄을 설 수 있다

    agent.update( UnixMilliseconds{ kNowMs + 1 } );
    world._server.poll( kNowMs + 1 );
    SW_EXPECT_FALSE( agent.isAccepted( kAlice ) ); // 기록이 없으니 받을 사람도 없다 — Found 를 받지 않았으므로 맞다

    LobbySnapshot lobby;
    lobby._lobbyID = 9;
    lobby._modeID  = "duel";
    lobby._listMember.push_back( LobbyMember{ kAlice, 0, SW_TRUE } );
    lobby._listMember.push_back( LobbyMember{ kBob, 1, SW_TRUE } );
    world._server._queue.placeLobby( lobby, UnixMilliseconds{ kNowMs + 2 } );
    world._server.poll( kNowMs + 2 );
    const vector<MatchQueueNotification> listLobby = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listLobby.size() );
    SW_EXPECT_TRUE( listLobby[0]._assignment._result == MatchQueueResult::NoServer );
    agent.shutdown();
}

/** @brief [MatchQueueServiceTest] 명단에 없는 계정이 접속을 시도하면 게임 서버가 1 초 주기를 기다리지 않고 다음 틱에 목록을 읽는다 — 버스를 잃은 첫 접속의 재시도가 바로 받아들여진다(감사 O-6) */
SW_TEST_CASE( MatchQueueServiceTest, RefusedConnectReadsMatchListAtOnce )
{
    SoloWorld world; // 버스가 없다 — 배정 버스 메시지를 잃은 것과 같다
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    MatchServerAgent agent;
    agent.initialize( kGameID, 100, test::MatchmakingTestPolicy::makeAgent() );
    agent.startReading( &world._server._router );
    agent.update( UnixMilliseconds{ kNowMs } ); // 첫 읽기 — 목록이 아직 없다. 다음 주기는 1 초 뒤이다
    world._server.poll( kNowMs );

    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 1 );
    SW_REQUIRE_EQUAL( size_t( 2 ), world._server.takeNotifications().size() );

    SW_EXPECT_FALSE( agent.isAccepted( kAlice ) ); // 첫 시도는 거절된다
    agent.update( UnixMilliseconds{ kNowMs + 2 } );
    world._server.poll( kNowMs + 2 );
    SW_EXPECT_TRUE( agent.isAccepted( kAlice ) ); // 재시도는 주기(1 초)를 기다리지 않는다
    agent.shutdown();
}

/** @brief [MatchQueueServiceTest] 게임 서버의 백필 입구: 서버 id · 주소 · 포트는 서버 설명에서 채우고, 모르는 모드 · 모양이 틀린 요청은 버스로 보내기 전에 거절한다 */
SW_TEST_CASE( MatchQueueServiceTest, BackfillSenderFillsServerFromDescriptor )
{
    SW_EXPECT_LOG( "MatchQueueService.MatchQueueBackfillUnknownMode" );
    SW_EXPECT_LOG( "MatchQueueService.MatchQueueBackfillMatchDoes" );
    PairWorld world; // 백필을 내는 B 는 권한이 아니다 — 모양이 틀린 요청이 버스로 나가 권한 서버에서 조용히 버려지던 자리이다
    world.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );

    ServerDescriptor descriptor;
    descriptor._serverID  = kGameID;
    descriptor._kind      = "game";
    descriptor._region    = "kr";
    descriptor._address   = "10.0.0.5";
    descriptor._port      = 7100;
    descriptor._portCount = 1;
    MatchServerAgent    agent; // 이 테스트는 자리를 꺼내지 않는다(빈 슬롯을 직접 적는다)
    MatchBackfillSender sender;
    agent.initialize( kGameID, 100, test::MatchmakingTestPolicy::makeAgent() );
    sender.initialize( &world._serverB._queue, &agent, descriptor );

    MatchBackfillSlots slots;
    slots._matchID       = 77;
    slots._modeID        = "duel";
    slots._averageRating = 1500;
    slots._listOpenSlot  = vector<int32>{ 0, 1 };

    MatchBackfillSlots unknownMode = slots;
    unknownMode._modeID            = "nope";
    SW_EXPECT_TRUE( sender.submitBackfill( unknownMode, UnixMilliseconds{ kNowMs } ) == MatchmakingResult::UnknownMode );
    MatchBackfillSlots badShape = slots;
    badShape._listOpenSlot      = vector<int32>{ 1 };
    SW_EXPECT_TRUE( sender.submitBackfill( badShape, UnixMilliseconds{ kNowMs } ).hasError( OnlineError::kInvalidRequest ) );

    SW_EXPECT_TRUE( sender.submitBackfill( slots, UnixMilliseconds{ kNowMs } ) == MatchmakingResult::Ok );
    world.updateBoth( kNowMs );
    SW_EXPECT_TRUE( world._serverA.join( kAlice )._result == MatchmakingResult::Ok );
    world.updateBoth( kNowMs + 1 );
    const vector<MatchQueueNotification> listFound = world._serverA.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listFound.size() );
    SW_EXPECT_TRUE( listFound[0]._assignment._result == MatchQueueResult::Found );
    SW_EXPECT_EQUAL( uint64( 77 ), listFound[0]._assignment._matchID );
    SW_EXPECT_EQUAL( kGameID, listFound[0]._assignment._serverID );
    SW_EXPECT_EQUAL( string( "10.0.0.5" ), listFound[0]._assignment._address );
    SW_EXPECT_EQUAL( uint16( 7100 ), listFound[0]._assignment._port );
    SW_EXPECT_EQUAL( 1, listFound[0]._assignment._team );
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
    world._server.poll( kNowMs );
    world._server.update( kNowMs + 2001 );
    world._server.update( kNowMs + 2001 );
    world._server._queue.placeLobby( lobby, UnixMilliseconds{ kNowMs + 2001 } );
    world._server.poll( kNowMs + 2001 ); // Found 는 명단 기록이 남은 뒤에 간다
    listResult = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._assignment._result == MatchQueueResult::Found );
    SW_EXPECT_EQUAL( kGameID, listResult[0]._assignment._serverID );
    SW_EXPECT_EQUAL( 0, listResult[0]._assignment._team );
    SW_EXPECT_EQUAL( 1, listResult[1]._assignment._team );
    SW_EXPECT_EQUAL( kServerA, listResult[0]._assignment._matchID >> 32 );

    lobby._modeID = "nope";
    world._server._queue.placeLobby( lobby, UnixMilliseconds{ kNowMs + 2001 } );
    listResult = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._assignment._result == MatchQueueResult::NoServer );
}

/** @brief [MatchQueueServiceTest] 같은 서버의 다른 모드 경기는 id 가 다르고 명단 기록도 따로 남는다 — 게임 서버가 네 사람을 모두 받는다(결함: 대기열마다 순번을 1 부터 세어 두 모드의 첫 경기가 같은 id 였다) */
SW_TEST_CASE( MatchQueueServiceTest, MatchesOfDifferentModesKeepTheirOwnRecords )
{
    SoloWorld world;
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode( "duel" ), MatchQueueTestUtil::makeMode( "arena" ) } );
    MatchServerAgent agent;
    agent.initialize( kGameID, 100, test::MatchmakingTestPolicy::makeAgent() );
    agent.startReading( &world._server._router );

    SW_EXPECT_TRUE( world._server.join( kAlice, kNowMs, "duel" )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob, kNowMs, "duel" )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kCarol, kNowMs, "arena" )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kDave, kNowMs, "arena" )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 1 );
    const vector<MatchQueueNotification> listFound = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 4 ), listFound.size() );
    uint64 duelMatchID  = 0;
    uint64 arenaMatchID = 0;
    for ( const MatchQueueNotification& found : listFound )
    {
        SW_EXPECT_TRUE( found._assignment._result == MatchQueueResult::Found );
        if ( found._assignment._modeID == "duel" )
            duelMatchID = found._assignment._matchID;
        else
            arenaMatchID = found._assignment._matchID;
    }
    SW_EXPECT_TRUE( duelMatchID != arenaMatchID );

    agent.update( UnixMilliseconds{ kNowMs + 1 } );
    world._server.poll( kNowMs + 1 );
    SW_EXPECT_TRUE( agent.isAccepted( kAlice ) );
    SW_EXPECT_TRUE( agent.isAccepted( kBob ) );
    SW_EXPECT_TRUE( agent.isAccepted( kCarol ) );
    SW_EXPECT_TRUE( agent.isAccepted( kDave ) );
    agent.shutdown();
}

/** @brief [MatchQueueServiceTest] 매칭 서버가 다시 떠도 앞 실행의 경기 id 를 다시 쓰지 않는다 — 앞 경기의 명단 기록(경기 시한까지 산다)을 덮지 않고, 게임 서버가 이미 읽은 기록으로 보고 건너뛰지 않는다. 로비 경기도 같다 */
SW_TEST_CASE( MatchQueueServiceTest, RestartedServerDoesNotReuseMatchIDs )
{
    SoloWorld world;
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    LobbySnapshot lobby;
    lobby._lobbyID = 9;
    lobby._modeID  = "duel";
    lobby._listMember.push_back( LobbyMember{ kAlice, 0, SW_TRUE } );
    lobby._listMember.push_back( LobbyMember{ kBob, 1, SW_TRUE } );

    const auto placeOneMatch = []( QueueServer& server ) -> uint64
    {
        SW_EXPECT_TRUE( server.join( kAlice )._result == MatchmakingResult::Ok );
        SW_EXPECT_TRUE( server.join( kBob )._result == MatchmakingResult::Ok );
        server.update( kNowMs + 1 );
        const vector<MatchQueueNotification> listFound = server.takeNotifications();
        SW_EXPECT_EQUAL( size_t( 2 ), listFound.size() );
        return listFound.empty() ? 0 : listFound[0]._assignment._matchID;
    };
    const auto placeOneLobby = [&lobby]( QueueServer& server ) -> uint64
    {
        server._queue.placeLobby( lobby, UnixMilliseconds{ kNowMs + 1 } );
        server.poll( kNowMs + 1 );
        const vector<MatchQueueNotification> listFound = server.takeNotifications();
        SW_EXPECT_EQUAL( size_t( 2 ), listFound.size() );
        return listFound.empty() ? 0 : listFound[0]._assignment._matchID;
    };
    const uint64 firstMatchID = placeOneMatch( world._server );
    const uint64 firstLobbyID = placeOneLobby( world._server );

    QueueServer restarted{ world._cacheData, nullptr, kServerA };
    restarted.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    restarted.update( kNowMs ); // 서버 목록 첫 읽기를 맡긴다
    restarted.update( kNowMs ); // 답을 스냅숏에 넣는다
    const uint64 secondMatchID = placeOneMatch( restarted );
    const uint64 secondLobbyID = placeOneLobby( restarted );
    SW_EXPECT_TRUE( firstMatchID != 0 && secondMatchID != 0 );
    SW_EXPECT_TRUE( firstMatchID != secondMatchID );
    SW_EXPECT_TRUE( firstLobbyID != 0 && secondLobbyID != 0 );
    SW_EXPECT_TRUE( firstLobbyID != secondLobbyID );
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
            SW_EXPECT_TRUE( _party.initialize( &_world._server._router, kServerA, test::MatchmakingTestPolicy::makePartyLobby(), test::MatchmakingTestPolicy::kMaxPartySize ) );
            MatchQueueDependencies dependencies = _world._server.makeDependencies();
            dependencies._pPartyLobby           = &_party;
            SW_EXPECT_TRUE( _world._server._queue.initialize( dependencies, vector<MatchModeDef>{ MatchQueueTestUtil::makeMode( "duel", teamSize ) } ) );
        }

        ~PartyQueueWorld() { _party.shutdown(); }

        void poll() { _world._server.poll( kNowMs ); }

        /** @brief 파티 하나를 만듭니다. 파티 id 입니다. */
        uint64 makeParty( AccountID leaderID, AccountID memberID )
        {
            _party.createParty( leaderID, 1 );
            poll();
            vector<PartyLobbyCompletion> listCompletion;
            _party.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            const uint64 partyID = listCompletion.empty() ? 0 : listCompletion[0]._party._partyID;
            _party.inviteToParty( leaderID, memberID, 2 );
            poll();
            _party.acceptPartyInvite( memberID, partyID, 3 );
            poll();
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
            poll();
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
    party.poll();
    SW_EXPECT_TRUE( party.joinAndWait( kAlice )._result == MatchmakingResult::Ok ); // 파티 기록의 티켓도 지워졌다
}

/** @brief [MatchQueueServiceTest] 줄 선 바로 뒤(파티 기록에 쓰는 동안) 빠져도 기록에 없는 티켓이 남지 않아 다시 설 수 있다(결함: 지우기가 쓰기보다 먼저 읽어 아무것도 하지 않았고, 뒤에 닿은 쓰기가 파티를 "줄 섬" 에 가뒀다) */
SW_TEST_CASE( MatchQueueServiceTest, LeavingWhileThePartyTicketIsWrittenLeavesNoTicket )
{
    PartyQueueWorld party( 2 );
    (void)party.makeParty( kAlice, kBob );
    MatchQueueService& queue = party._world._server._queue;
    queue.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
    (void)party._world._server._router.poll(); // 계정 → 파티
    (void)party._world._server._router.poll(); // 파티를 읽었다 — 기록에 티켓을 쓰는 중이다
    queue.leaveQueue( kAlice, 8 );
    party.poll();
    vector<MatchQueueCompletion> listCompletion;
    queue.takeCompletions( listCompletion );
    SW_REQUIRE_EQUAL( size_t( 2 ), listCompletion.size() );
    SW_EXPECT_EQUAL( uint64( 8 ), listCompletion[1]._requestTag );
    SW_EXPECT_TRUE( listCompletion[1]._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() );
    SW_EXPECT_EQUAL( 0, queue.getQueuedTicketCount( "duel" ) );

    SW_EXPECT_TRUE( party.joinAndWait( kAlice )._result == MatchmakingResult::Ok );
}

/** @brief [MatchQueueServiceTest] 파티를 찾은 뒤 멤버가 떠났으면 옛 명단의 티켓이 서지 않는다(kBusy) — 떠난 사람이 경기에 묶이지 않는다. 다시 서면 남은 사람의 티켓이다 */
SW_TEST_CASE( MatchQueueServiceTest, MemberLeavingAfterTheLookupIsNotQueued )
{
    PartyQueueWorld party( 2 );
    (void)party.makeParty( kAlice, kBob );
    MatchQueueService& queue = party._world._server._queue;
    queue.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
    party._party.leaveParty( kBob, 9 ); // 두 길이 함께 돈다 — 떠나기가 찾기 뒤 · 쓰기 앞에 닿는다
    party.poll();
    SW_EXPECT_TRUE( party._world._server.takeCompletion()._result.hasError( OnlineError::kBusy ) );
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() );

    SW_EXPECT_TRUE( party.joinAndWait( kAlice )._result == MatchmakingResult::Ok );
    queue.leaveQueue( kAlice, 8 );
    const vector<MatchQueueNotification> listCancelled = party._world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listCancelled.size() );
    SW_EXPECT_EQUAL( kAlice, listCancelled[0]._recipientID );
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
    party.poll();
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
    party.poll();
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() );

    queue.joinQueue( kCarol, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 ); // 평소의 길은 그대로다
    party.poll();
    SW_EXPECT_TRUE( party._world._server.takeCompletion()._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( 1, queue.getLocalTicketCount() );
}

namespace
{
    /** @brief 서버 둘 + 서버마다 실제 파티 서비스입니다(캐시는 같이 쓴다). A 가 권한입니다. */
    struct PartyPairWorld
    {
        PairWorld         _pair;
        PartyLobbyService _partyA;
        PartyLobbyService _partyB;

        PartyPairWorld()
            : _pair{}
            , _partyA{}
            , _partyB{}
        {
            SW_EXPECT_TRUE( _partyA.initialize( &_pair._serverA._router, kServerA, test::MatchmakingTestPolicy::makePartyLobby(), test::MatchmakingTestPolicy::kMaxPartySize ) );
            SW_EXPECT_TRUE( _partyB.initialize( &_pair._serverB._router, kServerB, test::MatchmakingTestPolicy::makePartyLobby(), test::MatchmakingTestPolicy::kMaxPartySize ) );
            const vector<MatchModeDef> listMode{ MatchQueueTestUtil::makeMode( "duel", 2 ) };
            MatchQueueDependencies     dependenciesA = _pair._serverA.makeDependencies();
            dependenciesA._pPartyLobby               = &_partyA;
            SW_EXPECT_TRUE( _pair._serverA._queue.initialize( dependenciesA, listMode ) );
            MatchQueueDependencies dependenciesB = _pair._serverB.makeDependencies();
            dependenciesB._pPartyLobby           = &_partyB;
            SW_EXPECT_TRUE( _pair._serverB._queue.initialize( dependenciesB, listMode ) );
            _pair._serverA.update( kNowMs );
            _pair.updateBoth( kNowMs );
            SW_EXPECT_TRUE( _pair._serverA._queue.isAuthority( "duel" ) );
        }

        ~PartyPairWorld()
        {
            _partyA.shutdown();
            _partyB.shutdown();
        }

        /** @brief A 에서 파티 하나를 만듭니다. */
        void makePartyOnA( AccountID leaderID, AccountID memberID )
        {
            _partyA.createParty( leaderID, 1 );
            _pair._serverA.poll( kNowMs );
            vector<PartyLobbyCompletion> listCompletion;
            _partyA.takeCompletions( listCompletion );
            SW_REQUIRE_EQUAL( size_t( 1 ), listCompletion.size() );
            const uint64 partyID = listCompletion[0]._party._partyID;
            _partyA.inviteToParty( leaderID, memberID, 2 );
            _pair._serverA.poll( kNowMs );
            _partyA.acceptPartyInvite( memberID, partyID, 3 );
            _pair._serverA.poll( kNowMs );
            listCompletion.clear();
            _partyA.takeCompletions( listCompletion );
            for ( const PartyLobbyCompletion& completion : listCompletion )
            {
                SW_EXPECT_TRUE( completion._result == MatchmakingResult::Ok );
            }
        }
    };
} // namespace

/** @brief [MatchQueueServiceTest] 티켓을 낸 서버가 죽어도 파티가 "줄 섬" 에 갇히지 않는다(감사 O-7): A 에서 파티 티켓 → A 가 멎음 → B 로 붙은 파티장이 빠지면 파티 기록의 티켓을 풀고 다시 설 수 있다 */
SW_TEST_CASE( MatchQueueServiceTest, LeaveReleasesTicketOfDeadServer )
{
    PartyPairWorld world;
    world.makePartyOnA( kAlice, kBob );
    world._pair._serverA._queue.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
    world._pair._serverA.poll( kNowMs );
    const MatchQueueCompletion joined = world._pair._serverA.takeCompletion();
    SW_REQUIRE_TRUE( joined._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( kServerA, joined._ticketID >> 32 );

    // 여기서 A 가 멎는다 — 이 뒤로 A 는 돌지 않으며 티켓의 결과 · 시한 · 파티 기록 정리가 오지 않는다
    MatchQueueService& queueB = world._pair._serverB._queue;
    queueB.leaveQueue( kAlice, 8 );
    world._pair._serverB.poll( kNowMs );
    const MatchQueueCompletion left = world._pair._serverB.takeCompletion();
    SW_EXPECT_TRUE( left._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( joined._ticketID, left._ticketID );

    queueB.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs + 1 }, 9 );
    world._pair._serverB.poll( kNowMs + 1 );
    const MatchQueueCompletion joinedAgain = world._pair._serverB.takeCompletion();
    SW_EXPECT_TRUE( joinedAgain._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( kServerB, joinedAgain._ticketID >> 32 );
    SW_EXPECT_EQUAL( 1, queueB.getLocalTicketCount() );

    queueB.leaveQueue( kCarol, 10 ); // 파티도 티켓도 없는 계정은 그대로 NotQueued 다
    world._pair._serverB.poll( kNowMs + 1 );
    SW_EXPECT_TRUE( world._pair._serverB.takeCompletion()._result == MatchmakingResult::NotQueued );
}

/**
 * @brief [MatchQueueServiceTest] 낸 서버가 파티 기록에 잡은 티켓을 아직 대기열에 넣지 않았을 때 다른 서버의 빠지기가 닿아도 티켓이 남지 않는다
 * @details 결함: 취소가 권한 서버에만 가 "없는 표" 로 사라지고, 낸 서버(B, 권한 아님)가 뒤늦게 넣은 티켓에 떠난 사람이 묶였다. 낸 서버에 취소와 잡기의 답 중 무엇이 먼저 닿아도 같다.
 */
SW_TEST_CASE( MatchQueueServiceTest, CancelFromAnotherServerReachesAReservedTicket )
{
    for ( const bool bCancelFirst : { true, false } )
    {
        PartyPairWorld world;
        world.makePartyOnA( kAlice, kBob );
        QueueServer& serverA = world._pair._serverA;
        QueueServer& serverB = world._pair._serverB;
        serverB._queue.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
        for ( int32 step = 0; step < 3; ++step ) // 계정의 파티 → 파티 기록 → 잡기(읽고 비교해 쓰기). 쓰기의 답은 아직 받지 않았다
        {
            (void)serverB._router.poll();
        }
        SW_EXPECT_EQUAL( 0, serverB._queue.getLocalTicketCount() );

        serverA._queue.leaveQueue( kAlice, 8 );
        serverA.poll( kNowMs );
        const MatchQueueCompletion left = serverA.takeCompletion();
        SW_EXPECT_TRUE( left._result == MatchmakingResult::Ok );
        SW_REQUIRE_EQUAL( kServerB, left._ticketID >> 32 ); // 기록에 잡힌 B 의 티켓이다

        if ( bCancelFirst )
        {
            vector<ServerBusMessage> listMessage;
            (void)serverB._transport->pollMessages( listMessage );
            for ( const ServerBusMessage& message : listMessage )
            {
                serverB._queue.handleBusMessage( message, UnixMilliseconds{ kNowMs } );
            }
            SW_EXPECT_TRUE( serverB.takeCompletion()._result == MatchmakingResult::NotQueued );
        }
        world._pair.updateBoth( kNowMs );
        SW_EXPECT_EQUAL( 0, serverA._queue.getQueuedTicketCount( "duel" ) );
        SW_EXPECT_EQUAL( 0, serverB._queue.getLocalTicketCount() );

        vector<MatchQueueCompletion> listCompletion;
        serverB._queue.takeCompletions( listCompletion );
        serverB._queue.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs + 1 }, 9 );
        serverB.poll( kNowMs + 1 );
        SW_EXPECT_TRUE( serverB.takeCompletion()._result == MatchmakingResult::Ok ); // 다시 설 수 있다
    }
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

    world._server.update( kNowMs + 1 );
    queue.takeCompletions( listCompletion );
    SW_EXPECT_TRUE( listCompletion.empty() ); // 아직 읽지 못했다

    stats._mapStat[kAlice] = 1600;
    world._server.update( kNowMs + 2 );
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
    world._server.update( kNowMs + test::MatchmakingTestPolicy::makeQueue()._ratingWaitMs );
    SW_EXPECT_TRUE( world._server.takeCompletion()._result == MatchmakingResult::RatingUnavailable );
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() );
    SW_EXPECT_EQUAL( 0, stats._mapRetained[kAlice] );

    queue.joinQueue( kBob, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
    SW_EXPECT_EQUAL( 1, stats._mapRetained[kBob] );
    queue.removeAccount( kBob );
    SW_EXPECT_TRUE( world._server.takeCompletion()._result == MatchmakingResult::NotQueued );
    SW_EXPECT_EQUAL( 0, stats._mapRetained[kBob] );
    stats._mapStat[kBob] = 1500;
    world._server.update( kNowMs + 1 );
    SW_EXPECT_EQUAL( 0, queue.getLocalTicketCount() ); // 떠난 사람의 티켓이 생기지 않는다
}

/** @brief [MatchQueueServiceTest] 버스로 먼저 받은 명단의 자리를 꺼낸 뒤 같은 명단의 기록을 읽어도 자리가 되살아나지 않는다(결함: 두 번 나간 사람의 자리를 두 번 채웠다) */
SW_TEST_CASE( MatchQueueServiceTest, SeatTakenFromTheBusStaysTakenAfterTheRecord )
{
    PairWorld world;
    world.start( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    LocalServerBus gameBus( &world._hub, kGameID );
    gameBus.subscribe( MatchQueueService::makeAssignTopic( kGameID ) );
    MatchServerAgent agent;
    agent.initialize( kGameID, 100, test::MatchmakingTestPolicy::makeAgent() );
    agent.startReading( &world._serverA._router );
    ServerDescriptor descriptor;
    descriptor._serverID  = kGameID;
    descriptor._kind      = "game";
    descriptor._region    = "kr";
    descriptor._address   = "10.0.0.9";
    descriptor._port      = 7000;
    descriptor._portCount = 1;
    MatchBackfillSender sender;
    sender.initialize( &world._serverA._queue, &agent, descriptor );

    SW_EXPECT_TRUE( world._serverA.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._serverA.join( kBob )._result == MatchmakingResult::Ok );
    world.updateBoth( kNowMs + 1 );
    vector<ServerBusMessage> listMessage;
    SW_REQUIRE_EQUAL( 1, gameBus.pollMessages( listMessage ) );
    SW_REQUIRE_TRUE( agent.handleAssign( listMessage[0], UnixMilliseconds{ kNowMs + 1 } ) );
    SW_EXPECT_TRUE( sender.submitPlayerLeft( kAlice, UnixMilliseconds{ kNowMs + 2 } ) == MatchmakingResult::Ok );

    agent.update( UnixMilliseconds{ kNowMs + 2 } ); // 이제 기록을 읽는다
    world._serverA.poll( kNowMs + 2 );
    SW_EXPECT_TRUE( sender.submitPlayerLeft( kAlice, UnixMilliseconds{ kNowMs + 3 } ) == MatchmakingResult::NotQueued );
    agent.shutdown();
}

/** @brief [MatchQueueServiceTest] 두 경기에 잇따라 배정된 계정의 자리는 새 경기의 것이다(결함: 목록은 새 기록부터 나오므로 뒤에 읽은 옛 기록이 새 자리를 덮었다) */
SW_TEST_CASE( MatchQueueServiceTest, OlderRosterDoesNotReplaceTheNewerSeat )
{
    SoloWorld world;
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    MatchServerAgent agent;
    agent.initialize( kGameID, 100, test::MatchmakingTestPolicy::makeAgent() );
    agent.startReading( &world._server._router );

    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 1 );
    const vector<MatchQueueNotification> listOlder = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listOlder.size() );
    SW_EXPECT_TRUE( world._server.join( kAlice, kNowMs + 10 )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kCarol, kNowMs + 10 )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 10 );
    const vector<MatchQueueNotification> listNewer = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listNewer.size() );

    agent.update( UnixMilliseconds{ kNowMs + 11 } ); // 두 기록을 한 번에 읽는다
    world._server.poll( kNowMs + 11 );
    MatchSeat aliceSeat;
    SW_REQUIRE_TRUE( agent.takeSeat( kAlice, aliceSeat ) );
    SW_EXPECT_EQUAL( listNewer[0]._assignment._matchID, aliceSeat._matchID );
    MatchSeat bobSeat;
    SW_REQUIRE_TRUE( agent.takeSeat( kBob, bobSeat ) ); // 옛 경기에만 있는 계정은 그 자리 그대로다
    SW_EXPECT_EQUAL( listOlder[0]._assignment._matchID, bobSeat._matchID );
    agent.shutdown();
}

/** @brief [MatchQueueServiceTest] 나간 사람의 자리: 게임 서버는 계정만 넘기고 경기 · 모드 · 팀은 읽은 명단에서 찾는다. 같은 사람은 한 번만, 명단에 없는 계정은 NotQueued, 채운 사람은 그 경기의 그 팀으로 Found */
SW_TEST_CASE( MatchQueueServiceTest, LeftPlayerSeatIsBackfilledOnce )
{
    SoloWorld world;
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    MatchServerAgent agent;
    agent.initialize( kGameID, 100, test::MatchmakingTestPolicy::makeAgent() );
    agent.startReading( &world._server._router );
    ServerDescriptor descriptor;
    descriptor._serverID  = kGameID;
    descriptor._kind      = "game";
    descriptor._region    = "kr";
    descriptor._address   = "10.0.0.9";
    descriptor._port      = 7000;
    descriptor._portCount = 1;
    MatchBackfillSender sender;
    sender.initialize( &world._server._queue, &agent, descriptor );

    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 1 );
    const vector<MatchQueueNotification> listFound = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listFound.size() );
    const MatchAssignment& aliceFound = listFound[0]._recipientID == kAlice ? listFound[0]._assignment : listFound[1]._assignment;
    agent.update( UnixMilliseconds{ kNowMs + 1 } ); // 명단 기록을 읽는다
    world._server.poll( kNowMs + 1 );
    SW_REQUIRE_TRUE( agent.isAccepted( kAlice ) );

    SW_EXPECT_TRUE( sender.submitPlayerLeft( kCarol, UnixMilliseconds{ kNowMs + 2 } ) == MatchmakingResult::NotQueued ); // 명단에 없다
    SW_EXPECT_TRUE( sender.submitPlayerLeft( kAlice, UnixMilliseconds{ kNowMs + 2 } ) == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( sender.submitPlayerLeft( kAlice, UnixMilliseconds{ kNowMs + 2 } ) == MatchmakingResult::NotQueued ); // 자리 하나는 한 번만

    SW_EXPECT_TRUE( world._server.join( kCarol, kNowMs + 3 )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 3 );
    const vector<MatchQueueNotification> listFilled = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listFilled.size() );
    SW_EXPECT_EQUAL( kCarol, listFilled[0]._recipientID );
    SW_EXPECT_TRUE( listFilled[0]._assignment._result == MatchQueueResult::Found );
    SW_EXPECT_EQUAL( aliceFound._matchID, listFilled[0]._assignment._matchID );
    SW_EXPECT_EQUAL( aliceFound._team, listFilled[0]._assignment._team );
    agent.shutdown();
}

/** @brief [MatchQueueServiceTest] 자리가 하나인 서버에는 한 사람만 배정한다 — 앞사람이 예약 · 접속해 있는 동안 다음 경기는 기다리고, 나가 자리가 비면 간다(결함: 둘째 사람이 몸 없이 같은 서버에 배정됐다) */
SW_TEST_CASE( MatchQueueServiceTest, SingleSeatServerTakesOneMatchAtATime )
{
    SoloWorld    world;
    MatchModeDef solo = MatchQueueTestUtil::makeMode( "solo" );
    solo._teamCount   = 1;
    world._server.start( vector<MatchModeDef>{ solo } );
    world._game.start( world._server._router, kNowMs, "kr", kGameID, 1 );
    world._server.poll( kNowMs );
    world._server.update( kNowMs );
    world._server.update( kNowMs );

    SW_EXPECT_TRUE( world._server.join( kAlice, kNowMs, "solo" )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 1 );
    SW_REQUIRE_EQUAL( size_t( 1 ), world._server.takeNotifications().size() );

    // 앞사람이 들어왔다 — 부하가 자리를 채운다(예약은 확정되어 빠진다).
    world._game._registration.setLoad( 1 );
    world._game._registration.confirmSeats( 1, 30000 );
    world._game._registration.update( UnixMilliseconds{ kNowMs + 2 } );
    SW_EXPECT_TRUE( world._server.join( kBob, kNowMs + 3, "solo" )._result == MatchmakingResult::Ok );
    for ( int64 nowMs = kNowMs + 3000; nowMs <= kNowMs + 9000; nowMs += 3000 )
    {
        world._game._registration.update( UnixMilliseconds{ nowMs } );
        world._server.update( nowMs );
        SW_EXPECT_TRUE( world._server.takeNotifications().empty() );
    }

    // 앞사람이 나갔다 — 기다리던 사람이 그 서버로 간다.
    world._game._registration.setLoad( 0 );
    world._game._registration.update( UnixMilliseconds{ kNowMs + 9001 } );
    world._server.poll( kNowMs + 9001 );
    world._server.update( kNowMs + 12000 ); // 새 목록을 읽는다
    world._server.update( kNowMs + 12001 );
    const vector<MatchQueueNotification> listFound = world._server.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listFound.size() );
    SW_EXPECT_EQUAL( kBob, listFound[0]._recipientID );
    SW_EXPECT_TRUE( listFound[0]._assignment._result == MatchQueueResult::Found );
    SW_EXPECT_EQUAL( kGameID, listFound[0]._assignment._serverID );
}

/** @brief [MatchQueueServiceTest] 나간 사람은 다시 받지 않고, 마지막 사람이 나간 경기는 백필하지 않는다. 명단이 정원을 넘으면 오래된 경기부터 받지 않는다 */
SW_TEST_CASE( MatchQueueServiceTest, AcceptedPlayersNeverExceedTheSeats )
{
    SoloWorld world;
    world.startWithGameServer( vector<MatchModeDef>{ MatchQueueTestUtil::makeMode() } );
    MatchServerAgent agent;
    agent.initialize( kGameID, 100, test::MatchmakingTestPolicy::makeAgent() );
    agent.startReading( &world._server._router );
    ServerDescriptor descriptor;
    descriptor._serverID  = kGameID;
    descriptor._kind      = "game";
    descriptor._region    = "kr";
    descriptor._address   = "10.0.0.9";
    descriptor._port      = 7000;
    descriptor._portCount = 1;
    MatchBackfillSender sender;
    sender.initialize( &world._server._queue, &agent, descriptor );

    SW_EXPECT_TRUE( world._server.join( kAlice )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kBob )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 1 );
    SW_EXPECT_TRUE( world._server.join( kCarol, kNowMs + 2 )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( world._server.join( kDave, kNowMs + 2 )._result == MatchmakingResult::Ok );
    world._server.update( kNowMs + 2 );
    agent.update( UnixMilliseconds{ kNowMs + 3 } );
    world._server.poll( kNowMs + 3 );
    SW_EXPECT_EQUAL( 4, agent.getExpectedCount() );

    // 맵이 두 사람만 받는다 — 앞 경기는 자리를 새 경기에 넘겼다. 정원은 디렉터리에 올릴 설명에도 실린다.
    sender.setSeatCapacity( 2 );
    SW_EXPECT_EQUAL( 2, descriptor._capacity );
    SW_EXPECT_EQUAL( 2, agent.getExpectedCount() );
    SW_EXPECT_FALSE( agent.isAccepted( kAlice ) );
    SW_EXPECT_TRUE( agent.isAccepted( kCarol ) );
    SW_EXPECT_TRUE( agent.isAccepted( kDave ) );

    SW_EXPECT_TRUE( sender.submitPlayerLeft( kCarol, UnixMilliseconds{ kNowMs + 4 } ) == MatchmakingResult::Ok ); // 남은 사람이 있다 — 채운다
    SW_EXPECT_FALSE( agent.isAccepted( kCarol ) );                                                                // 자리를 맡긴 사람은 되돌아오지 않는다
    SW_EXPECT_TRUE( agent.isAccepted( kDave ) );
    SW_EXPECT_TRUE( sender.submitPlayerLeft( kDave, UnixMilliseconds{ kNowMs + 5 } ) == MatchmakingResult::NotQueued ); // 경기가 비었다
    SW_EXPECT_EQUAL( 0, agent.getExpectedCount() );
    agent.shutdown();
}

/** @brief [MatchQueueServiceTest] 통계 대기 시한은 설정이 정한다 — 시한을 1 초로 줄이면 1 초에 포기하고, 연장 주기가 임대 수명 이상인 설정으로는 서지 않는다 */
SW_TEST_CASE( MatchQueueServiceTest, PolicyValuesComeFromSettings )
{
    {
        SoloWorld              world;
        FakeStatSource         stats;
        MatchQueueDependencies dependencies = world._server.makeDependencies();
        dependencies._pStatSource           = &stats;
        dependencies._policy._ratingWaitMs  = 1000;
        SW_REQUIRE_TRUE( world._server._queue.initialize( dependencies, vector<MatchModeDef>{ MatchQueueTestUtil::makeMode( "duel", 1, "rating" ) } ) );
        world._server._queue.joinQueue( kAlice, "duel", "kr", UnixMilliseconds{ kNowMs }, 7 );
        world._server.update( kNowMs + 1000 );
        SW_EXPECT_TRUE( world._server.takeCompletion()._result == MatchmakingResult::RatingUnavailable ); // 기본 테스트 값(3 초)이었다면 아직 기다려 완료가 없다
    }
    SW_EXPECT_LOG( "MatchQueueService.MatchQueueLeaseTimes" );
    SoloWorld              world;
    MatchQueueDependencies dependencies = world._server.makeDependencies();
    dependencies._policy._leaseRenewMs  = dependencies._policy._leaseTTLMs;
    SW_EXPECT_FALSE( world._server._queue.initialize( dependencies, vector<MatchModeDef>{ MatchQueueTestUtil::makeMode( "duel", 1 ) } ) );
}

/** @brief [MatchQueueServiceTest] 운영 값(`_partyLobby` · `_queue` · `_agent`)을 적지 않은 키트 설정은 읽지 못한다 — 코드 기본값으로 조용히 서지 않는다 */
SW_TEST_CASE( MatchQueueServiceTest, KitConfigRequiresThePolicies )
{
    SW_EXPECT_LOG( "Config.KeyMissing" );
    OnlineServerKitEntry entry;
    entry._name     = "matchmaking";
    entry._settings = R"({ "_maxPartySize": 4, "_reconnectGraceMs": 60000, "_cancelConfirmWaitMs": 3000 })";
    MatchmakingKitConfig kitConfig;
    SW_EXPECT_FALSE( entry.readSettings( kitConfig ) );
}
