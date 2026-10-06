// 업적과 시즌 — 통계가 문턱을 넘으면 업적 하나 + 보상 우편 하나(두 번 넘어도 하나), 시즌 밖 제출은 NotRanked, 정산은 순위 · 보상 구간대로 결과 + 우편,
// 다시 돌려도 더 쓰지 않음, 정산 중 저장소 실패 뒤 다시 돌리면 남은 사람만, 예약 작업(ServiceScheduler 기간 창)이 정산을 돌리고 회차를 끝낸다.
#include "pch.h"

#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Online/Server/Leaderboard/LeaderboardService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct SettleRecorder
    {
        vector<uint8> _listSucceeded{};

        void onDone( bool bSucceeded ) { _listSucceeded.push_back( bSucceeded ? SW_TRUE : SW_FALSE ); }

        SettlementDelegate makeDelegate() { return SettlementDelegate::create<&SettleRecorder::onDone>( this ); }
    };

    struct SeasonNode
    {
        MemoryServiceStore            _store;
        MemoryEphemeralStore          _cache;
        EphemeralStoreRouter          _router;
        LeaderboardService            _service;
        vector<LeaderboardCompletion> _listCompletion;

        SeasonNode( MemoryServiceDatabase* pDatabase, MemoryEphemeralDatabase* pCacheDatabase )
            : _store{ pDatabase }
            , _cache{ pCacheDatabase }
            , _router{}
            , _service{}
            , _listCompletion{}
        {
            _router.initialize( &_cache );
            LeaderboardServiceDependencies dependencies;
            dependencies._pStore  = &_store;
            dependencies._pRouter = &_router;
            _service.initialize( dependencies );
        }

        void step()
        {
            for ( int32 round = 0; round < 8; ++round )
            {
                (void)_store.pollCompletions();
                (void)_router.pump();
            }
            _service.drainCompletions( _listCompletion );
        }

        /** @brief 30 명이 시즌 1 안에 점수를 낸다(1 등 = 계정 1). */
        void submitThirtyPlayers()
        {
            for ( AccountId accountId = 1; accountId <= 30; ++accountId )
                _service.submitScore( "arena", accountId, "p", static_cast<int64>( 1000 - accountId ), 1500, accountId );
            step();
        }
    };

    struct LeaderboardSeasonTestInternal
    {
        static LeaderboardDefinition makeSeasonBoard()
        {
            LeaderboardDefinition board;
            board._boardId = "arena";
            board._update  = LeaderboardUpdate::Best;
            board._reset   = LeaderboardReset::Season;
            LeaderboardSeason season;
            season._seasonId = 1;
            season._startMs  = 1000;
            season._endMs    = 2000;
            season._listRewardTier.push_back( LeaderboardRewardTier{ "cur.gem", "mail.season.top1", 100, 1, 1 } );
            season._listRewardTier.push_back( LeaderboardRewardTier{ "cur.gem", "mail.season.top10", 10, 2, 10 } );
            board._listSeason.push_back( season );
            return board;
        }

        static int32 countSeasonResults( const MemoryServiceDatabase& database ) { return database.countRecords( hashed_string( "lb_season_result" ) ); }
    };
} // namespace

SW_TEST_CASE( LeaderboardSeasonTest, AchievementUnlocksOnceWithOneRewardMail )
{
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    SeasonNode              node( &database, &cacheDatabase );
    AchievementDefinition   achievement;
    achievement._achievementId = "ten_wins";
    achievement._statName      = "wins";
    achievement._threshold     = 10;
    achievement._rewardAssetId = "cur.gold";
    achievement._rewardAmount  = 500;
    achievement._mailTitleKey  = "mail.achievement.ten_wins";
    SW_ASSERT_TRUE( node._service.registerAchievement( achievement ) );
    AchievementDefinition noTitle = achievement;
    noTitle._mailTitleKey         = "";
    SW_EXPECT_FALSE( node._service.registerAchievement( noTitle ) ); // 보상 우편에는 제목이 있어야 한다

    node._service.changeStat( 7, "p", "wins", 9, LeaderboardUpdate::Latest, 100, 1 );
    node.step();
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 0 );
    node._service.changeStat( 7, "p", "wins", 10, LeaderboardUpdate::Latest, 200, 2 );
    node._service.changeStat( 7, "p", "wins", 11, LeaderboardUpdate::Latest, 300, 3 ); // 다시 넘음 — 더 주지 않는다
    node.step();
    vector<AchievementUnlock> listUnlock;
    node._service.drainAchievementUnlocks( listUnlock );
    SW_ASSERT_EQUAL( listUnlock.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( listUnlock[0]._state._achievementId.c_str(), "ten_wins" );
    SW_EXPECT_EQUAL( listUnlock[0]._state._unlockedMs, int64( 200 ) );
    SW_EXPECT_EQUAL( listUnlock[0]._accountId, AccountId( 7 ) );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 1 );

    node._service.readAchievements( 7, 4 );
    node.step();
    SW_ASSERT_EQUAL( node._listCompletion.back()._listAchievement.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( node._listCompletion.back()._listAchievement[0]._achievementId.c_str(), "ten_wins" );
}

SW_TEST_CASE( LeaderboardSeasonTest, SettlementRanksRewardsAndIsIdempotent )
{
    using Internal = LeaderboardSeasonTestInternal;
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    SeasonNode              node( &database, &cacheDatabase );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeSeasonBoard() ) );
    node._service.submitScore( "arena", 1, "a", 50, 500, 1 ); // 시즌 전
    node.step();
    SW_EXPECT_TRUE( node._listCompletion.back()._result == LeaderboardResult::NotRanked );
    node.submitThirtyPlayers();
    SW_EXPECT_EQUAL( node._listCompletion.back()._periodId, uint64( 1 ) );

    node._service.readSeasonResult( "arena", 1, 1, 50 ); // 정산 전
    node.step();
    SW_EXPECT_TRUE( node._listCompletion.back()._result == LeaderboardResult::NotRanked );

    SettleRecorder recorder;
    node._service.settleSeason( "arena", 1, 2100, recorder.makeDelegate() );
    node.step();
    SW_ASSERT_EQUAL( recorder._listSucceeded.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( recorder._listSucceeded[0] == SW_TRUE );
    SW_EXPECT_EQUAL( Internal::countSeasonResults( database ), 30 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 10 ); // 1 등 + 2..10 등

    node._service.readSeasonResult( "arena", 1, 1, 51 );
    node.step();
    SW_ASSERT_EQUAL( node._listCompletion.back()._listEntry.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._listCompletion.back()._listEntry[0]._rank, 1 );
    SW_EXPECT_EQUAL( node._listCompletion.back()._listEntry[0]._score, int64( 999 ) );
    SW_EXPECT_EQUAL( node._listCompletion.back()._periodId, uint64( 1 ) );

    const uint64 hashAfter = database.computeContentHash();
    node._service.settleSeason( "arena", 1, 2200, recorder.makeDelegate() ); // 다른 서버가 다시 돌았다
    node.step();
    SW_EXPECT_TRUE( recorder._listSucceeded.back() == SW_TRUE );
    SW_EXPECT_EQUAL( database.computeContentHash(), hashAfter );
    node._service.settleSeason( "arena", 9, 2200, recorder.makeDelegate() ); // 없는 시즌
    SW_EXPECT_TRUE( recorder._listSucceeded.back() == SW_FALSE );
}

SW_TEST_CASE( LeaderboardSeasonTest, InterruptedSettlementResumesWithoutDuplicates )
{
    using Internal = LeaderboardSeasonTestInternal;
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    SeasonNode              node( &database, &cacheDatabase );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeSeasonBoard() ) );
    node.submitThirtyPlayers();

    database.armFault( ServiceStoreFault::RejectCommit, 1 ); // 첫 묶음(15 명)은 쓰고 둘째 묶음에서 실패
    SettleRecorder recorder;
    node._service.settleSeason( "arena", 1, 2100, recorder.makeDelegate() );
    node.step();
    SW_ASSERT_EQUAL( recorder._listSucceeded.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( recorder._listSucceeded.back() == SW_FALSE );
    SW_EXPECT_EQUAL( Internal::countSeasonResults( database ), 15 );

    node._service.settleSeason( "arena", 1, 2200, recorder.makeDelegate() );
    node.step();
    SW_EXPECT_TRUE( recorder._listSucceeded.back() == SW_TRUE );
    SW_EXPECT_EQUAL( Internal::countSeasonResults( database ), 30 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 10 );
}

SW_TEST_CASE( LeaderboardSeasonTest, ScheduledWindowRunsSettlementOnce )
{
    using Internal = LeaderboardSeasonTestInternal;
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    SeasonNode              node( &database, &cacheDatabase );
    ServiceScheduler        scheduler;
    scheduler.initialize( &node._store, 1 );
    node._service.setScheduler( &scheduler );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeSeasonBoard() ) );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeSeasonBoard() ) ); // 다시 올려도 작업은 하나
    node.submitThirtyPlayers();

    scheduler.tick( 1900 ); // 창 전
    node.step();
    SW_EXPECT_EQUAL( Internal::countSeasonResults( database ), 0 );
    scheduler.tick( 2100 ); // 창 안 — 차지 → 처리기 → 정산 → 회차 끝
    node.step();
    SW_EXPECT_EQUAL( Internal::countSeasonResults( database ), 30 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 10 );
    SW_EXPECT_EQUAL( scheduler.getPendingWorkCount(), 0 );
    SW_EXPECT_EQUAL( node._service.getPendingCount(), 0 );

    const uint64 hashAfter = database.computeContentHash();
    scheduler.tick( 2200 ); // 같은 회차는 다시 돌지 않는다
    node.step();
    SW_EXPECT_EQUAL( database.computeContentHash(), hashAfter );
    node._service.shutdown();
    scheduler.shutdown();
}
