// LeaderboardServiceTest — 순위표 로직(메모리 저장소 + 메모리 캐시): 갱신 방식, 읽기 범위, 동점 순서(상위 · 주변 · 친구 · 정산이 같다), 캐시를 잃은 뒤 다시 채우기,
// 기간, 통계와 연결된 보드, 접속한 계정의 통계 메모리, 업적 한 번, 시즌 정산(순위 · 보상 · 다시 돌기 · 도중 실패 · 페이지 · 예약 작업).
// 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardProtocol.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardService.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStore.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"
#include "GameFramework/Runtime/Online/Mail/Server/ServiceMail.h"
#include "GameFramework/Runtime/Online/Schedule/Server/ServiceScheduler.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr int64 kNowMs         = 1700000000000ll;
    constexpr int64 kDayMs         = 86400000ll;
    constexpr int64 kSeasonStartMs = 1000;
    constexpr int64 kSeasonEndMs   = 2000;
    constexpr int64 kInSeasonMs    = 1500;

    /** @brief 정산 완료를 받습니다. */
    struct SettlementSink
    {
        int32 _doneCount{ 0 };
        int32 _succeededCount{ 0 };

        void onDone( bool bSucceeded )
        {
            ++_doneCount;
            if ( bSucceeded )
                ++_succeededCount;
        }
    };

    /** @brief 서버 하나입니다. 캐시는 자기 것이고 영속 데이터는 빌립니다 — 같은 데이터로 둘째를 만들면 "캐시를 잃고 다시 뜬 서버" 입니다. */
    struct LeaderboardFixture
    {
        MemoryEphemeralDatabase _cacheData;
        MemoryEphemeralStore    _cache;
        EphemeralStoreRouter    _router;
        MemoryServiceStore      _store;
        LeaderboardService      _service;
        uint64                  _nextTag{ 1 };

        LeaderboardFixture( ManualClock& clock, MemoryServiceDatabase& database )
            : _cacheData{ clock }
            , _cache{ &_cacheData }
            , _router{}
            , _store{ &database }
            , _service{}
        {
            _router.initialize( &_cache );
            LeaderboardServiceDependencies dependencies;
            dependencies._pStore  = &_store;
            dependencies._pRouter = &_router;
            _service.initialize( dependencies );
        }

        ~LeaderboardFixture()
        {
            pump();
            _service.shutdown();
            _router.shutdown();
        }

        void pump()
        {
            while ( _store.pollCompletions() + _router.pump() > 0 )
            {
            }
        }

        /** @brief 태그 하나의 완료를 꺼냅니다. 없으면 실패이며 빈 완료를 돌려줍니다. */
        LeaderboardCompletion take( uint64 tag )
        {
            pump();
            vector<LeaderboardCompletion> listCompletion;
            _service.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            if ( listCompletion.empty() )
                return LeaderboardCompletion{};
            SW_EXPECT_EQUAL( tag, listCompletion[0]._requestTag );
            return std::move( listCompletion[0] );
        }

        LeaderboardCompletion submit( string_view boardID, AccountID accountID, int64 score, int64 nowMs = kNowMs, string_view name = "name" )
        {
            const uint64 tag = _nextTag++;
            _service.submitScore( boardID, accountID, name, score, UnixMilliseconds{ nowMs }, tag );
            return take( tag );
        }

        LeaderboardCompletion top( string_view boardID, int32 offset = 0, int32 count = 100, int64 nowMs = kNowMs )
        {
            const uint64 tag = _nextTag++;
            _service.readTop( boardID, offset, count, UnixMilliseconds{ nowMs }, tag );
            return take( tag );
        }

        LeaderboardCompletion around( string_view boardID, AccountID accountID, int32 radius )
        {
            const uint64 tag = _nextTag++;
            _service.readAround( boardID, accountID, radius, UnixMilliseconds{ kNowMs }, tag );
            return take( tag );
        }

        LeaderboardCompletion scoresOf( string_view boardID, const vector<AccountID>& listAccountID )
        {
            const uint64 tag = _nextTag++;
            _service.readScoresOf( boardID, listAccountID, UnixMilliseconds{ kNowMs }, tag );
            return take( tag );
        }

        LeaderboardCompletion stat( AccountID accountID, string_view statName, int64 value, LeaderboardUpdate update )
        {
            const uint64 tag = _nextTag++;
            _service.changeStat( accountID, "name", statName, value, update, UnixMilliseconds{ kNowMs }, tag );
            return take( tag );
        }

        LeaderboardCompletion seasonResult( string_view boardID, uint32 seasonID, AccountID accountID )
        {
            const uint64 tag = _nextTag++;
            _service.readSeasonResult( boardID, seasonID, accountID, tag );
            return take( tag );
        }

        SettlementSink settle( string_view boardID, uint32 seasonID )
        {
            SettlementSink sink;
            _service.settleSeason( boardID, seasonID, UnixMilliseconds{ kSeasonEndMs }, SettlementDelegate::create<&SettlementSink::onDone>( &sink ) );
            pump();
            return sink;
        }

        void addBoard( string_view boardID, LeaderboardOrder order = LeaderboardOrder::Descending, LeaderboardUpdate update = LeaderboardUpdate::Best, LeaderboardReset reset = LeaderboardReset::None )
        {
            LeaderboardDef board;
            board._boardID = string( boardID );
            board._order   = order;
            board._update  = update;
            board._reset   = reset;
            SW_REQUIRE_TRUE( _service.registerBoard( board ) );
        }

        /** @brief 시즌 하나짜리 보드입니다. 1 ~ @p rewardedRankTo 등은 젬 10, 1 등은 따로 100 입니다. */
        void addSeasonBoard( string_view boardID, int32 rewardedRankTo, LeaderboardOrder order = LeaderboardOrder::Descending )
        {
            LeaderboardRewardTier first;
            first._assetID      = "cur.gem";
            first._mailTitleKey = "lb.first";
            first._amount       = 100;
            first._rankFrom     = 1;
            first._rankTo       = 1;
            LeaderboardRewardTier rest;
            rest._assetID      = "cur.gem";
            rest._mailTitleKey = "lb.rest";
            rest._amount       = 10;
            rest._rankFrom     = 2;
            rest._rankTo       = rewardedRankTo;
            LeaderboardSeason season;
            season._seasonID = 1;
            season._startMs  = kSeasonStartMs;
            season._endMs    = kSeasonEndMs;
            season._listRewardTier.push_back( first );
            if ( rewardedRankTo >= 2 )
                season._listRewardTier.push_back( rest );
            LeaderboardDef board;
            board._boardID = string( boardID );
            board._order   = order;
            board._reset   = LeaderboardReset::Season;
            board._listSeason.push_back( season );
            SW_REQUIRE_TRUE( _service.registerBoard( board ) );
        }

        void addAchievement( string_view id, string_view statName, int64 threshold, bool bHidden = false, int64 rewardAmount = 5 )
        {
            OnlineAchievementDef achievement;
            achievement._achievementID = string( id );
            achievement._statName      = string( statName );
            achievement._threshold     = threshold;
            achievement._bHidden       = bHidden;
            if ( rewardAmount > 0 )
            {
                achievement._rewardAssetID = "cur.gem";
                achievement._rewardAmount  = rewardAmount;
                achievement._mailTitleKey  = "ach.title";
            }
            SW_REQUIRE_TRUE( _service.registerAchievement( achievement ) );
        }
    };

    /** @brief 시계와 영속 데이터를 들고 서버 하나를 띄운 묶음입니다. */
    struct LeaderboardWorld
    {
        ManualClock           _clock;
        MemoryServiceDatabase _database;
        LeaderboardFixture    _rig;

        LeaderboardWorld()
            : _clock{ MonotonicMilliseconds{ 1000 } }
            , _database{}
            , _rig{ _clock, _database }
        {
        }

        int32 countMail() const { return _database.countRecords( ServiceMail::getMailTable() ); }
    };
} // namespace

/** @brief [LeaderboardServiceTest] 규칙에 맞지 않는 보드 · 업적은 등록하지 않는다 — 통계에 연결된 Sum 보드는 누적값을 또 더하므로 거절한다 */
SW_TEST_CASE( LeaderboardServiceTest, RegistrationRefusesBrokenDefinitions )
{
    LeaderboardWorld    world;
    LeaderboardService& service = world._rig._service;

    LeaderboardDef board;
    board._boardID = "Bad Id";
    SW_EXPECT_FALSE( service.registerBoard( board ) );

    board._boardID    = "linked";
    board._sourceStat = "kills";
    board._update     = LeaderboardUpdate::Sum;
    SW_EXPECT_FALSE( service.registerBoard( board ) );
    board._update = LeaderboardUpdate::Best;
    SW_EXPECT_TRUE( service.registerBoard( board ) );

    LeaderboardDef seasons;
    seasons._boardID = "seasons";
    LeaderboardSeason season;
    season._seasonID = 1;
    season._startMs  = 10;
    season._endMs    = 20;
    seasons._listSeason.push_back( season );
    SW_EXPECT_FALSE( service.registerBoard( seasons ) ); // Season 보드가 아닌데 시즌이 있다
    seasons._reset = LeaderboardReset::Season;
    SW_EXPECT_TRUE( service.registerBoard( seasons ) );
    seasons._listSeason.push_back( season );
    SW_EXPECT_FALSE( service.registerBoard( seasons ) ); // 시즌 id 가 겹친다
    seasons._listSeason.pop_back();
    seasons._listSeason[0]._endMs = 10;
    SW_EXPECT_FALSE( service.registerBoard( seasons ) ); // 시작이 끝보다 앞서야 한다

    OnlineAchievementDef achievement;
    achievement._achievementID = "ok";
    achievement._statName      = "kills";
    achievement._rewardAssetID = "cur.gem"; // 보상이 있으면 수량과 제목이 있어야 한다
    SW_EXPECT_FALSE( service.registerAchievement( achievement ) );
    achievement._rewardAmount = 1;
    achievement._mailTitleKey = "t";
    SW_EXPECT_TRUE( service.registerAchievement( achievement ) );
    achievement._statName = "Bad Stat";
    SW_EXPECT_FALSE( service.registerAchievement( achievement ) );
}

/** @brief [LeaderboardServiceTest] 갱신 방식: Best 는 정렬 방향으로 더 좋을 때만, Latest 는 덮어쓰기, Sum 은 더하되 상한에서 멈춘다 */
SW_TEST_CASE( LeaderboardServiceTest, UpdateModesApply )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addBoard( "best" );
    rig.addBoard( "fastest", LeaderboardOrder::Ascending );
    rig.addBoard( "latest", LeaderboardOrder::Descending, LeaderboardUpdate::Latest );
    rig.addBoard( "sum", LeaderboardOrder::Descending, LeaderboardUpdate::Sum );

    SW_EXPECT_EQUAL( int64( 10 ), rig.submit( "best", 1, 10 )._score );
    SW_EXPECT_EQUAL( int64( 10 ), rig.submit( "best", 1, 5 )._score );
    SW_EXPECT_EQUAL( int64( 20 ), rig.submit( "best", 1, 20 )._score );

    SW_EXPECT_EQUAL( int64( 10 ), rig.submit( "fastest", 1, 10 )._score );
    SW_EXPECT_EQUAL( int64( 10 ), rig.submit( "fastest", 1, 20 )._score );
    SW_EXPECT_EQUAL( int64( 5 ), rig.submit( "fastest", 1, 5 )._score );

    SW_EXPECT_EQUAL( int64( 10 ), rig.submit( "latest", 1, 10 )._score );
    SW_EXPECT_EQUAL( int64( 3 ), rig.submit( "latest", 1, 3 )._score );

    SW_EXPECT_EQUAL( int64( 10 ), rig.submit( "sum", 1, 10 )._score );
    SW_EXPECT_EQUAL( int64( 7 ), rig.submit( "sum", 1, -3 )._score );
    SW_EXPECT_EQUAL( LeaderboardLimit::kMaxAbsScore, rig.submit( "sum", 2, LeaderboardLimit::kMaxAbsScore )._score );
    SW_EXPECT_EQUAL( LeaderboardLimit::kMaxAbsScore, rig.submit( "sum", 2, 5 )._score );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );
}

/** @brief [LeaderboardServiceTest] 틀린 제출은 저장소에 닿지 않고 까닭을 돌려준다. 태그가 0 이면 완료가 없다 */
SW_TEST_CASE( LeaderboardServiceTest, InvalidSubmitsAreRefused )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addBoard( "best" );
    rig.addSeasonBoard( "season", 1 );
    const uint64 commitsBefore = world._database.getCommitCount();

    SW_EXPECT_TRUE( rig.submit( "nope", 1, 1 )._result == LeaderboardResult::UnknownBoard );
    SW_EXPECT_TRUE( rig.submit( "best", kInvalidAccountID, 1 )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.submit( "best", 1, LeaderboardLimit::kMaxAbsScore + 1 )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.submit( "best", 1, -LeaderboardLimit::kMaxAbsScore - 1 )._result.hasError( OnlineError::kInvalidRequest ) );
    const string longName( static_cast<size_t>( OnlineLimit::kMaxDisplayNameSize ) + 1, 'x' );
    SW_EXPECT_TRUE( rig.submit( "best", 1, 1, kNowMs, longName )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.submit( "season", 1, 1, kSeasonEndMs )._result == LeaderboardResult::NotRanked ); // 끝 시각은 시즌 밖이다
    SW_EXPECT_EQUAL( commitsBefore, world._database.getCommitCount() );

    rig._service.submitScore( "nope", 1, "", 1, UnixMilliseconds{ kNowMs }, 0 );
    rig.pump();
    vector<LeaderboardCompletion> listCompletion;
    rig._service.takeCompletions( listCompletion );
    SW_EXPECT_TRUE( listCompletion.empty() );
}

/** @brief [LeaderboardServiceTest] 상위 조회: 순위 · 점수 · 이름 · 쪽 나누기. 오름차순 보드는 작은 값이 1 등이고 점수를 원래 부호로 돌려준다 */
SW_TEST_CASE( LeaderboardServiceTest, TopReadsRanksScoresAndNames )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addBoard( "best" );
    rig.addBoard( "fastest", LeaderboardOrder::Ascending );
    (void)rig.submit( "best", 1, 10, kNowMs, "one" );
    (void)rig.submit( "best", 2, 30, kNowMs, "two" );
    (void)rig.submit( "best", 3, 20, kNowMs, "three" );
    (void)rig.submit( "fastest", 11, 10 );
    (void)rig.submit( "fastest", 12, 30 );

    const LeaderboardCompletion all = rig.top( "best" );
    SW_EXPECT_TRUE( all._result == LeaderboardResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 3 ), all._listEntry.size() );
    SW_EXPECT_EQUAL( uint64( 2 ), all._listEntry[0]._accountID );
    SW_EXPECT_EQUAL( int64( 30 ), all._listEntry[0]._score );
    SW_EXPECT_EQUAL( 1, all._listEntry[0]._rank );
    SW_EXPECT_EQUAL( string( "two" ), all._listEntry[0]._displayName );
    SW_EXPECT_EQUAL( uint64( 1 ), all._listEntry[2]._accountID );
    SW_EXPECT_EQUAL( 3, all._listEntry[2]._rank );

    const LeaderboardCompletion page = rig.top( "best", 1, 1 );
    SW_REQUIRE_EQUAL( size_t( 1 ), page._listEntry.size() );
    SW_EXPECT_EQUAL( uint64( 3 ), page._listEntry[0]._accountID );
    SW_EXPECT_EQUAL( 2, page._listEntry[0]._rank );
    SW_EXPECT_TRUE( rig.top( "best", 9, 5 )._listEntry.empty() );

    const LeaderboardCompletion fastest = rig.top( "fastest" );
    SW_REQUIRE_EQUAL( size_t( 2 ), fastest._listEntry.size() );
    SW_EXPECT_EQUAL( uint64( 11 ), fastest._listEntry[0]._accountID );
    SW_EXPECT_EQUAL( int64( 10 ), fastest._listEntry[0]._score );

    SW_EXPECT_TRUE( rig.top( "nope" )._result == LeaderboardResult::UnknownBoard );
    SW_EXPECT_TRUE( rig.top( "best", -1, 5 )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.top( "best", 0, 0 )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.top( "best", 0, LeaderboardLimit::kMaxPage + 1 )._result.hasError( OnlineError::kInvalidRequest ) );
}

/** @brief [LeaderboardServiceTest] 주변 조회: 나를 포함해 위아래 반경이고 맨 위에서는 위가 잘린다. 점수가 없으면 NotRanked */
SW_TEST_CASE( LeaderboardServiceTest, AroundReadsNeighbours )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addBoard( "best" );
    for ( uint64 accountID = 1; accountID <= 5; ++accountID )
    {
        (void)rig.submit( "best", accountID, static_cast<int64>( accountID ) * 10 ); // 5 번이 1 등
    }
    const LeaderboardCompletion middle = rig.around( "best", 3, 1 );
    SW_REQUIRE_EQUAL( size_t( 3 ), middle._listEntry.size() );
    SW_EXPECT_EQUAL( uint64( 4 ), middle._listEntry[0]._accountID );
    SW_EXPECT_EQUAL( 2, middle._listEntry[0]._rank );
    SW_EXPECT_EQUAL( uint64( 3 ), middle._listEntry[1]._accountID );
    SW_EXPECT_EQUAL( 3, middle._listEntry[1]._rank );

    const LeaderboardCompletion topEdge = rig.around( "best", 5, 2 );
    SW_REQUIRE_EQUAL( size_t( 3 ), topEdge._listEntry.size() );
    SW_EXPECT_EQUAL( 1, topEdge._listEntry[0]._rank );

    SW_EXPECT_TRUE( rig.around( "best", 99, 1 )._result == LeaderboardResult::NotRanked );
    SW_EXPECT_TRUE( rig.around( "best", 3, LeaderboardLimit::kMaxAround + 1 )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.around( "best", 3, -1 )._result.hasError( OnlineError::kInvalidRequest ) );
}

/** @brief [LeaderboardServiceTest] 같은 점수의 순서는 상위 · 주변 · 친구 조회 · 정산 모두 계정 id 오름차순이다 — 순위표의 2 등이 정산에서 1 등 보상을 받으면 안 된다 */
SW_TEST_CASE( LeaderboardServiceTest, TiesRankTheSameEverywhere )
{
    for ( const LeaderboardOrder order : { LeaderboardOrder::Descending, LeaderboardOrder::Ascending } )
    {
        LeaderboardWorld    world;
        LeaderboardFixture& rig = world._rig;
        rig.addSeasonBoard( "season", 1, order );
        for ( const uint64 accountID : { uint64( 7 ), uint64( 3 ), uint64( 5 ) } )
        {
            (void)rig.submit( "season", accountID, 50, kInSeasonMs );
        }
        const LeaderboardCompletion top = rig.top( "season", 0, 100, kInSeasonMs );
        SW_REQUIRE_EQUAL( size_t( 3 ), top._listEntry.size() );
        SW_EXPECT_EQUAL( uint64( 3 ), top._listEntry[0]._accountID );
        SW_EXPECT_EQUAL( uint64( 5 ), top._listEntry[1]._accountID );
        SW_EXPECT_EQUAL( uint64( 7 ), top._listEntry[2]._accountID );

        const uint64 aroundTag = 900;
        rig._service.readAround( "season", 5, 0, UnixMilliseconds{ kInSeasonMs }, aroundTag );
        const LeaderboardCompletion around = rig.take( aroundTag );
        SW_REQUIRE_EQUAL( size_t( 1 ), around._listEntry.size() );
        SW_EXPECT_EQUAL( 2, around._listEntry[0]._rank );

        const uint64 friendsTag = 901;
        rig._service.readScoresOf( "season", vector<AccountID>{ 7, 3, 5 }, UnixMilliseconds{ kInSeasonMs }, friendsTag );
        const LeaderboardCompletion friends = rig.take( friendsTag );
        SW_REQUIRE_EQUAL( size_t( 3 ), friends._listEntry.size() );
        SW_EXPECT_EQUAL( uint64( 3 ), friends._listEntry[0]._accountID );

        SW_EXPECT_EQUAL( 1, rig.settle( "season", 1 )._succeededCount );
        const LeaderboardCompletion result = rig.seasonResult( "season", 1, 3 );
        SW_REQUIRE_EQUAL( size_t( 1 ), result._listEntry.size() );
        SW_EXPECT_EQUAL( 1, result._listEntry[0]._rank );
        SW_EXPECT_EQUAL( 1, world.countMail() );
    }
}

/** @brief [LeaderboardServiceTest] 캐시를 잃은 서버는 첫 읽기에서 영속 점수로 다시 채운다 — 순위 · 점수가 그대로이고, 그동안 온 읽기와 쓴 점수도 잃지 않는다 */
SW_TEST_CASE( LeaderboardServiceTest, RebuildsRanksAfterCacheLoss )
{
    LeaderboardWorld world;
    world._rig.addBoard( "fastest", LeaderboardOrder::Ascending );
    (void)world._rig.submit( "fastest", 1, 30 );
    (void)world._rig.submit( "fastest", 2, 10 );

    LeaderboardFixture restarted{ world._clock, world._database };
    restarted.addBoard( "fastest", LeaderboardOrder::Ascending );
    restarted._service.submitScore( "fastest", 3, "late", 20, UnixMilliseconds{ kNowMs }, 0 ); // 완료는 다시 채우는 동안 온다
    restarted._service.readTop( "fastest", 0, 10, UnixMilliseconds{ kNowMs }, 1 );
    restarted._router.pump();                                                     // 준비 표시가 없어 다시 채우기를 맡긴다
    restarted._service.readTop( "fastest", 0, 1, UnixMilliseconds{ kNowMs }, 2 ); // 그동안 온 읽기는 기다린다
    SW_EXPECT_EQUAL( 2, restarted._service.getPendingCount() );                   // 쓰기 하나 + 다시 채우기 하나
    restarted.pump();

    vector<LeaderboardCompletion> listCompletion;
    restarted._service.takeCompletions( listCompletion );
    SW_REQUIRE_EQUAL( size_t( 2 ), listCompletion.size() );
    const LeaderboardCompletion& full = listCompletion[0]._requestTag == 1 ? listCompletion[0] : listCompletion[1];
    SW_REQUIRE_EQUAL( size_t( 3 ), full._listEntry.size() );
    SW_EXPECT_EQUAL( uint64( 2 ), full._listEntry[0]._accountID );
    SW_EXPECT_EQUAL( int64( 10 ), full._listEntry[0]._score );
    SW_EXPECT_EQUAL( uint64( 3 ), full._listEntry[1]._accountID );
    SW_EXPECT_EQUAL( uint64( 1 ), full._listEntry[2]._accountID );
    SW_EXPECT_EQUAL( 0, restarted._service.getPendingCount() );
}

/** @brief [LeaderboardServiceTest] 영속 읽기가 실패하면 기다리던 읽기는 Unavailable 이고, 다음 읽기가 다시 채운다 */
SW_TEST_CASE( LeaderboardServiceTest, RebuildFailureReportsThenRecovers )
{
    LeaderboardWorld world;
    world._rig.addBoard( "best" );
    (void)world._rig.submit( "best", 1, 30 );

    LeaderboardFixture restarted{ world._clock, world._database };
    restarted.addBoard( "best" );
    world._database.armFault( ServiceStoreFault::RejectRead );
    SW_EXPECT_TRUE( restarted.top( "best" )._result.hasError( OnlineError::kUnavailable ) );
    const LeaderboardCompletion recovered = restarted.top( "best" );
    SW_EXPECT_TRUE( recovered._result == LeaderboardResult::Ok );
    SW_EXPECT_EQUAL( size_t( 1 ), recovered._listEntry.size() );
}

/** @brief [LeaderboardServiceTest] 쓰기가 실패하면 Unavailable 이고 순위에 나타나지 않는다 */
SW_TEST_CASE( LeaderboardServiceTest, FailedWriteDoesNotReachRanks )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addBoard( "best" );
    world._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( rig.submit( "best", 1, 30 )._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_TRUE( rig.top( "best" )._listEntry.empty() );
}

/** @brief [LeaderboardServiceTest] 기간: 초기화 분이 지나면 빈 보드로 시작하고, 기간 id 는 기간 시작 시각이며, 순위 캐시는 기간 끝 + 하루까지만 남긴다 */
SW_TEST_CASE( LeaderboardServiceTest, DailyBoardsResetAtTheirMinute )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addBoard( "daily", LeaderboardOrder::Descending, LeaderboardUpdate::Best, LeaderboardReset::Daily );
    const LeaderboardDef* pBoard = rig._service.findBoard( "daily" );
    SW_REQUIRE_TRUE( pBoard != nullptr );

    const uint64 periodID = rig._service.computePeriodID( *pBoard, UnixMilliseconds{ kNowMs } );
    SW_EXPECT_TRUE( static_cast<int64>( periodID ) <= kNowMs && kNowMs < static_cast<int64>( periodID ) + kDayMs );
    SW_EXPECT_EQUAL( uint64( 0 ), periodID % static_cast<uint64>( kDayMs ) ); // 초기화 분이 0 이면 UTC 자정이다

    const int64 nextPeriodMs = static_cast<int64>( periodID ) + kDayMs;
    SW_EXPECT_EQUAL( periodID, rig.submit( "daily", 1, 10 )._periodID );
    SW_EXPECT_EQUAL( static_cast<uint64>( nextPeriodMs ), rig.submit( "daily", 1, 5, nextPeriodMs )._periodID );

    const LeaderboardCompletion today = rig.top( "daily" );
    SW_REQUIRE_EQUAL( size_t( 1 ), today._listEntry.size() );
    SW_EXPECT_EQUAL( int64( 10 ), today._listEntry[0]._score );
    const LeaderboardCompletion tomorrow = rig.top( "daily", 0, 10, nextPeriodMs );
    SW_REQUIRE_EQUAL( size_t( 1 ), tomorrow._listEntry.size() );
    SW_EXPECT_EQUAL( int64( 5 ), tomorrow._listEntry[0]._score );

    SW_EXPECT_EQUAL( nextPeriodMs + kDayMs - kNowMs, rig._service.computeRankRetentionMs( *pBoard, periodID, UnixMilliseconds{ kNowMs } ) );
    SW_EXPECT_EQUAL( int64( 1 ), rig._service.computeRankRetentionMs( *pBoard, periodID, UnixMilliseconds{ nextPeriodMs + 2 * kDayMs } ) );

    LeaderboardDef forever;
    forever._boardID = "forever";
    SW_EXPECT_EQUAL( int64( 0 ), rig._service.computeRankRetentionMs( forever, 0, UnixMilliseconds{ kNowMs } ) );
    SW_EXPECT_EQUAL( uint64( 0 ), rig._service.computePeriodID( forever, UnixMilliseconds{ kNowMs } ) );
}

/** @brief [LeaderboardServiceTest] 통계: 방식대로 바뀌고 연결된 보드에 누적값이 들어가며, 안 바뀌면 보드도 그대로다. 틀린 이름은 Invalid */
SW_TEST_CASE( LeaderboardServiceTest, StatsFeedLinkedBoards )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    LeaderboardDef      linked;
    linked._boardID    = "kills_board";
    linked._sourceStat = "kills";
    linked._update     = LeaderboardUpdate::Latest;
    SW_REQUIRE_TRUE( rig._service.registerBoard( linked ) );

    SW_EXPECT_EQUAL( int64( 3 ), rig.stat( 1, "kills", 3, LeaderboardUpdate::Sum )._score );
    SW_EXPECT_EQUAL( int64( 5 ), rig.stat( 1, "kills", 2, LeaderboardUpdate::Sum )._score );
    LeaderboardCompletion top = rig.top( "kills_board" );
    SW_REQUIRE_EQUAL( size_t( 1 ), top._listEntry.size() );
    SW_EXPECT_EQUAL( int64( 5 ), top._listEntry[0]._score ); // 3 + 5 로 부풀지 않는다

    SW_EXPECT_EQUAL( int64( 9 ), rig.stat( 1, "max_hit", 9, LeaderboardUpdate::Best )._score );
    SW_EXPECT_EQUAL( int64( 9 ), rig.stat( 1, "max_hit", 4, LeaderboardUpdate::Best )._score ); // 통계의 Best 는 큰 값이다

    SW_EXPECT_TRUE( rig.stat( 1, "Bad Name", 1, LeaderboardUpdate::Sum )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.stat( kInvalidAccountID, "kills", 1, LeaderboardUpdate::Sum )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.stat( 1, "kills", LeaderboardLimit::kMaxAbsScore + 1, LeaderboardUpdate::Sum )._result.hasError( OnlineError::kInvalidRequest ) );

    rig._service.readStats( 1, 77 );
    const LeaderboardCompletion stats = rig.take( 77 );
    SW_REQUIRE_EQUAL( size_t( 2 ), stats._listStat.size() );
    SW_EXPECT_EQUAL( string( "kills" ), stats._listStat[0]._name );
    SW_EXPECT_EQUAL( int64( 5 ), stats._listStat[0]._value );
}

/** @brief [LeaderboardServiceTest] 통계 메모리: 읽기 전 NotLoaded, 읽은 뒤 Found · Missing, 바뀌면 같이 고친다. 잡은 쪽이 모두 놓아야 지운다. 읽기 실패는 경고하고 NotLoaded 로 남으며 다음 잡기가 다시 읽는다 */
SW_TEST_CASE( LeaderboardServiceTest, AccountStatCacheFollowsConnection )
{
    SW_EXPECT_LOG( "LeaderboardService.LeaderboardStatsAccountCould" );
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    (void)rig.stat( 1, "rating", 1500, LeaderboardUpdate::Latest );

    int64 value = 0;
    SW_EXPECT_TRUE( rig._service.findStat( 1, "rating", value ) == AccountStatLookup::NotLoaded );
    rig._service.retainStats( 1 );
    SW_EXPECT_TRUE( rig._service.findStat( 1, "rating", value ) == AccountStatLookup::NotLoaded ); // 완료 전
    rig.pump();
    SW_EXPECT_TRUE( rig._service.findStat( 1, "rating", value ) == AccountStatLookup::Found );
    SW_EXPECT_EQUAL( int64( 1500 ), value );
    SW_EXPECT_TRUE( rig._service.findStat( 1, "other", value ) == AccountStatLookup::Missing );

    (void)rig.stat( 1, "rating", 1600, LeaderboardUpdate::Latest );
    SW_EXPECT_TRUE( rig._service.findStat( 1, "rating", value ) == AccountStatLookup::Found );
    SW_EXPECT_EQUAL( int64( 1600 ), value );

    rig._service.retainStats( 1 ); // 줄 서는 쪽이 같이 잡는다 — 접속이 끊겨도 남는다
    rig._service.releaseStats( 1 );
    SW_EXPECT_TRUE( rig._service.findStat( 1, "rating", value ) == AccountStatLookup::Found );
    rig._service.releaseStats( 1 );
    SW_EXPECT_TRUE( rig._service.findStat( 1, "rating", value ) == AccountStatLookup::NotLoaded );

    world._database.armFault( ServiceStoreFault::RejectRead );
    rig._service.retainStats( 1 );
    rig.pump();
    SW_EXPECT_TRUE( rig._service.findStat( 1, "rating", value ) == AccountStatLookup::NotLoaded );
    rig._service.retainStats( 1 ); // 실패한 읽기를 다시 한다
    rig.pump();
    SW_EXPECT_TRUE( rig._service.findStat( 1, "rating", value ) == AccountStatLookup::Found );
    rig._service.releaseStats( 1 );
    rig._service.releaseStats( 1 );
}

/** @brief [LeaderboardServiceTest] 업적: 임계값을 넘으면 알림과 보상 우편이 한 번만 나가고, 다시 넘어도 그대로다 */
SW_TEST_CASE( LeaderboardServiceTest, AchievementUnlocksOnce )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addAchievement( "ten_kills", "kills", 10 );

    (void)rig.stat( 1, "kills", 9, LeaderboardUpdate::Latest );
    vector<AchievementUnlock> listUnlock;
    rig._service.pollAchievementUnlocks( listUnlock );
    SW_EXPECT_TRUE( listUnlock.empty() );
    SW_EXPECT_EQUAL( 0, world.countMail() );

    (void)rig.stat( 1, "kills", 10, LeaderboardUpdate::Latest );
    rig._service.pollAchievementUnlocks( listUnlock );
    SW_REQUIRE_EQUAL( size_t( 1 ), listUnlock.size() );
    SW_EXPECT_EQUAL( uint64( 1 ), listUnlock[0]._accountID );
    SW_EXPECT_EQUAL( string( "ten_kills" ), listUnlock[0]._state._achievementID );
    SW_EXPECT_EQUAL( 1, world.countMail() );

    listUnlock.clear();
    (void)rig.stat( 1, "kills", 20, LeaderboardUpdate::Latest );
    rig._service.pollAchievementUnlocks( listUnlock );
    SW_EXPECT_TRUE( listUnlock.empty() );
    SW_EXPECT_EQUAL( 1, world.countMail() );

    rig._service.readAchievements( 1, 77 );
    const LeaderboardCompletion achievements = rig.take( 77 );
    SW_REQUIRE_EQUAL( size_t( 1 ), achievements._listAchievement.size() );
    SW_EXPECT_EQUAL( kNowMs, achievements._listAchievement[0]._unlockedMs );
}

/** @brief [LeaderboardServiceTest] 업적 저장이 실패하면 달성도 우편도 없고, 다음 통계 변화에서 다시 시도한다 */
SW_TEST_CASE( LeaderboardServiceTest, AchievementRetriesAfterFailedCommit )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addAchievement( "ten_kills", "kills", 10 );
    world._database.armFault( ServiceStoreFault::RejectCommit, 1 ); // 통계 쓰기는 지나가고 업적 쓰기가 진다
    (void)rig.stat( 1, "kills", 10, LeaderboardUpdate::Latest );
    SW_EXPECT_EQUAL( 0, world.countMail() );

    (void)rig.stat( 1, "kills", 11, LeaderboardUpdate::Latest );
    vector<AchievementUnlock> listUnlock;
    rig._service.pollAchievementUnlocks( listUnlock );
    SW_EXPECT_EQUAL( size_t( 1 ), listUnlock.size() );
    SW_EXPECT_EQUAL( 1, world.countMail() );
}

/** @brief [LeaderboardServiceTest] 업적 진행률: id 순서이고, 숨긴 업적은 달성 전에 id · 진행 · 임계값을 가리며, 달성 뒤에는 보인다 */
SW_TEST_CASE( LeaderboardServiceTest, ProgressMasksHiddenAchievements )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addAchievement( "b_open", "kills", 10 );
    rig.addAchievement( "a_hidden", "kills", 20, true, 0 );
    (void)rig.stat( 1, "kills", 5, LeaderboardUpdate::Latest );

    rig._service.readAchievementProgress( 1, 77 );
    const LeaderboardCompletion before = rig.take( 77 );
    SW_REQUIRE_EQUAL( size_t( 2 ), before._listProgress.size() );
    SW_EXPECT_TRUE( before._listProgress[0]._bHidden == SW_TRUE );
    SW_EXPECT_TRUE( before._listProgress[0]._achievementID.empty() );
    SW_EXPECT_EQUAL( int64( 0 ), before._listProgress[0]._progress );
    SW_EXPECT_EQUAL( int64( 0 ), before._listProgress[0]._threshold );
    SW_EXPECT_EQUAL( string( "b_open" ), before._listProgress[1]._achievementID );
    SW_EXPECT_EQUAL( int64( 5 ), before._listProgress[1]._progress );
    SW_EXPECT_EQUAL( int64( 10 ), before._listProgress[1]._threshold );
    SW_EXPECT_EQUAL( int64( 0 ), before._listProgress[1]._unlockedMs );

    (void)rig.stat( 1, "kills", 25, LeaderboardUpdate::Latest );
    rig._service.readAchievementProgress( 1, 78 );
    const LeaderboardCompletion after = rig.take( 78 );
    SW_REQUIRE_EQUAL( size_t( 2 ), after._listProgress.size() );
    SW_EXPECT_EQUAL( string( "a_hidden" ), after._listProgress[0]._achievementID );
    SW_EXPECT_EQUAL( int64( 25 ), after._listProgress[0]._progress );
    SW_EXPECT_EQUAL( kNowMs, after._listProgress[0]._unlockedMs );
}

/** @brief [LeaderboardServiceTest] 친구 순위: 점수 없는 계정은 빠지고 같은 계정은 한 번이며, 영속 값이라 캐시를 잃어도 같다. 비었거나 상한을 넘으면 Invalid */
SW_TEST_CASE( LeaderboardServiceTest, ScoresOfRanksWithinTheList )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addBoard( "best" );
    (void)rig.submit( "best", 1, 10, kNowMs, "one" );
    (void)rig.submit( "best", 2, 30, kNowMs, "two" );
    (void)rig.submit( "best", 3, 20, kNowMs, "three" );

    const LeaderboardCompletion friends = rig.scoresOf( "best", vector<AccountID>{ 1, 3, 3, 99 } );
    SW_REQUIRE_EQUAL( size_t( 2 ), friends._listEntry.size() );
    SW_EXPECT_EQUAL( uint64( 3 ), friends._listEntry[0]._accountID );
    SW_EXPECT_EQUAL( 1, friends._listEntry[0]._rank ); // 전체 2 등이어도 목록 안 1 등
    SW_EXPECT_EQUAL( string( "three" ), friends._listEntry[0]._displayName );
    SW_EXPECT_EQUAL( 2, friends._listEntry[1]._rank );

    LeaderboardFixture restarted{ world._clock, world._database };
    restarted.addBoard( "best" );
    SW_EXPECT_EQUAL( size_t( 2 ), restarted.scoresOf( "best", vector<AccountID>{ 1, 3 } )._listEntry.size() );

    SW_EXPECT_TRUE( rig.scoresOf( "nope", vector<AccountID>{ 1 } )._result == LeaderboardResult::UnknownBoard );
    SW_EXPECT_TRUE( rig.scoresOf( "best", vector<AccountID>{} )._result.hasError( OnlineError::kInvalidRequest ) );
    const vector<AccountID> listTooMany( static_cast<size_t>( LeaderboardLimit::kMaxScoresOfCount ) + 1, 1 );
    SW_EXPECT_TRUE( rig.scoresOf( "best", listTooMany )._result.hasError( OnlineError::kInvalidRequest ) );
}

/** @brief [LeaderboardServiceTest] 정산: 순위와 점수를 남기고 구간대로 보상하며, 구간 밖은 결과만 남는다. 시즌 결과 조회의 오류 길도 본다 */
SW_TEST_CASE( LeaderboardServiceTest, SettlementRecordsRanksAndRewards )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addBoard( "plain" );
    rig.addSeasonBoard( "season", 2 );
    (void)rig.submit( "season", 1, 10, kInSeasonMs );
    (void)rig.submit( "season", 2, 30, kInSeasonMs );
    (void)rig.submit( "season", 3, 20, kInSeasonMs );

    SW_EXPECT_TRUE( rig.seasonResult( "season", 1, 2 )._result == LeaderboardResult::NotRanked ); // 정산 전
    const SettlementSink settled = rig.settle( "season", 1 );
    SW_EXPECT_EQUAL( 1, settled._doneCount );
    SW_EXPECT_EQUAL( 1, settled._succeededCount );
    SW_EXPECT_EQUAL( 2, world.countMail() ); // 1 · 2 등만
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );

    const LeaderboardCompletion third = rig.seasonResult( "season", 1, 1 );
    SW_EXPECT_TRUE( third._result == LeaderboardResult::Ok );
    SW_EXPECT_EQUAL( uint64( 1 ), third._periodID );
    SW_REQUIRE_EQUAL( size_t( 1 ), third._listEntry.size() );
    SW_EXPECT_EQUAL( 3, third._listEntry[0]._rank );
    SW_EXPECT_EQUAL( int64( 10 ), third._listEntry[0]._score );
    SW_EXPECT_EQUAL( uint64( 1 ), third._listEntry[0]._accountID );

    SW_EXPECT_TRUE( rig.seasonResult( "season", 1, 99 )._result == LeaderboardResult::NotRanked );
    SW_EXPECT_TRUE( rig.seasonResult( "nope", 1, 1 )._result == LeaderboardResult::UnknownBoard );
    SW_EXPECT_TRUE( rig.seasonResult( "plain", 1, 1 )._result.hasError( OnlineError::kInvalidRequest ) );

    const SettlementSink unknownSeason = rig.settle( "season", 9 );
    SW_EXPECT_EQUAL( 1, unknownSeason._doneCount );
    SW_EXPECT_EQUAL( 0, unknownSeason._succeededCount );
}

/** @brief [LeaderboardServiceTest] 정산이 도중에 실패하면 실패로 끝나고, 다시 돌면 남은 사람만 처리한다 — 앞에서 보상받은 사람에게 두 번 주지 않는다. 다 끝난 뒤 또 돌아도 그대로다 */
SW_TEST_CASE( LeaderboardServiceTest, SettlementResumesWithoutDoublePaying )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addSeasonBoard( "season", 30 ); // 커밋 하나에 12 명이라 30 명이면 커밋 셋이다
    for ( uint64 accountID = 1; accountID <= 30; ++accountID )
    {
        (void)rig.submit( "season", accountID, static_cast<int64>( accountID ), kInSeasonMs );
    }
    world._database.armFault( ServiceStoreFault::RejectCommit, 1 ); // 둘째 커밋이 진다
    const SettlementSink failed = rig.settle( "season", 1 );
    SW_EXPECT_EQUAL( 1, failed._doneCount );
    SW_EXPECT_EQUAL( 0, failed._succeededCount );
    SW_EXPECT_EQUAL( 12, world.countMail() );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );

    SW_EXPECT_EQUAL( 1, rig.settle( "season", 1 )._succeededCount );
    SW_EXPECT_EQUAL( 30, world.countMail() );

    const uint64 hashBefore = world._database.computeContentHash();
    SW_EXPECT_EQUAL( 1, rig.settle( "season", 1 )._succeededCount );
    SW_EXPECT_EQUAL( hashBefore, world._database.computeContentHash() );
}

/** @brief [LeaderboardServiceTest] 쪽(600 명)을 넘는 정산도 순위가 이어진다 — 둘째 쪽 첫 사람은 601 등이다. 참가자가 없는 시즌은 할 일 없이 성공한다 */
SW_TEST_CASE( LeaderboardServiceTest, SettlementPagesKeepRanksContinuous )
{
    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    rig.addSeasonBoard( "empty", 1 );
    SW_EXPECT_EQUAL( 1, rig.settle( "empty", 1 )._succeededCount );
    SW_EXPECT_EQUAL( 0, world.countMail() );

    rig.addSeasonBoard( "season", 1 );
    constexpr uint64 kPlayers = 601;
    for ( uint64 accountID = 1; accountID <= kPlayers; ++accountID )
    {
        (void)rig.submit( "season", accountID, static_cast<int64>( kPlayers + 1 - accountID ), kInSeasonMs ); // 1 번이 1 등
    }
    SW_EXPECT_EQUAL( 1, rig.settle( "season", 1 )._succeededCount );
    SW_EXPECT_EQUAL( 1, world.countMail() );
    const LeaderboardCompletion last = rig.seasonResult( "season", 1, kPlayers );
    SW_REQUIRE_EQUAL( size_t( 1 ), last._listEntry.size() );
    SW_EXPECT_EQUAL( static_cast<int32>( kPlayers ), last._listEntry[0]._rank );
}

/** @brief [LeaderboardServiceTest] 예약 작업: 시즌이 끝나면 예약표가 정산을 부르고 다음 갱신에서 또 돌지 않는다. 작업 id 형식과 틀린 작업 id 도 본다 */
SW_TEST_CASE( LeaderboardServiceTest, SchedulerSettlesEndedSeasonOnce )
{
    SW_EXPECT_EQUAL( string( "lb.settle.season.00000001" ), LeaderboardService::makeSettlementJobID( "season", 1 ) );

    LeaderboardWorld    world;
    LeaderboardFixture& rig = world._rig;
    ServiceScheduler    scheduler;
    scheduler.initialize( &rig._store, 1 );
    rig._service.setScheduler( &scheduler );
    rig.addSeasonBoard( "season", 1 );
    rig.addSeasonBoard( "season", 1 ); // 다시 등록해도 작업은 하나다
    (void)rig.submit( "season", 1, 10, kInSeasonMs );

    scheduler.update( UnixMilliseconds{ kInSeasonMs } ); // 시즌 안 — 돌지 않는다
    rig.pump();
    SW_EXPECT_TRUE( rig.seasonResult( "season", 1, 1 )._result == LeaderboardResult::NotRanked );

    scheduler.update( UnixMilliseconds{ kSeasonEndMs + 1 } );
    rig.pump();
    SW_EXPECT_TRUE( rig.seasonResult( "season", 1, 1 )._result == LeaderboardResult::Ok );
    SW_EXPECT_EQUAL( 1, world.countMail() );

    const uint64 hashBefore = world._database.computeContentHash();
    scheduler.update( UnixMilliseconds{ kSeasonEndMs + 2 } );
    rig.pump();
    SW_EXPECT_EQUAL( hashBefore, world._database.computeContentHash() );

    ScheduledRun brokenRun;
    brokenRun._jobID = "lb.settle.nope.00000001"; // 없는 보드는 정산하지 않고 회차를 실패로 돌려준다
    rig._service.onScheduledRun( brokenRun );
    rig.pump();
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );

    rig._service.setScheduler( nullptr );
    scheduler.shutdown();
}

/** @brief [LeaderboardServiceTest] 다시 시도할 공통 범주(일시 장애 · 경합)는 요청 층이 기억하지 않아 같은 멱등 키의 점수 제출 재시도가 서비스에 다시 닿고, 끝난 결과는 기억한다 */
SW_TEST_CASE( LeaderboardServiceTest, RetryableReplyReachesTheServiceAgain )
{
    test::ReplyRetentionFixture<LeaderboardProtocol, LeaderboardSubmitReply> rig;
    LeaderboardSubmitReply                                                   finished;
    finished._result = LeaderboardResult::Ok;
    rig.expectFailureReachesService( LeaderboardMethod::SubmitScore::kID, OnlineError::kUnavailable, finished );
    rig.expectFailureReachesService( LeaderboardMethod::SubmitScore::kID, OnlineError::kBusy, finished );
}
