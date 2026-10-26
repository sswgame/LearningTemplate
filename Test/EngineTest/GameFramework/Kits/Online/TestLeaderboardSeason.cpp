// 업적과 시즌 — 통계가 임계값을 넘으면 업적 하나 + 보상 우편 하나(두 번 넘어도 하나), 시즌 밖 제출은 NotRanked, 정산은 순위 · 보상 구간대로 결과 + 우편,
// 다시 돌려도 더 쓰지 않음, 정산 중 저장소 실패 뒤 다시 돌리면 남은 사람만, 예약 작업(ServiceScheduler 기간 창)이 정산을 돌리고 회차를 끝낸다.
#include "pch.h"

#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardService.h"

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
            for ( AccountID accountID = 1; accountID <= 30; ++accountID )
            {
                _service.submitScore( "arena", accountID, "p", static_cast<int64>( 1000 - accountID ), UnixMilliseconds{ 1500 }, accountID );
            }
            step();
        }
    };

    struct LeaderboardSeasonTestInternal
    {
        static LeaderboardDefinition makeSeasonBoard()
        {
            LeaderboardDefinition board;
            board._boardID = "arena";
            board._update  = LeaderboardUpdate::Best;
            board._reset   = LeaderboardReset::Season;
            LeaderboardSeason season;
            season._seasonID = 1;
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
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    SeasonNode              node( &database, &cacheDatabase );
    AchievementDefinition   achievement;
    achievement._achievementID = "ten_wins";
    achievement._statName      = "wins";
    achievement._threshold     = 10;
    achievement._rewardAssetID = "cur.gold";
    achievement._rewardAmount  = 500;
    achievement._mailTitleKey  = "mail.achievement.ten_wins";
    SW_ASSERT_TRUE( node._service.registerAchievement( achievement ) );
    AchievementDefinition noTitle = achievement;
    noTitle._mailTitleKey         = "";
    SW_EXPECT_FALSE( node._service.registerAchievement( noTitle ) ); // 보상 우편에는 제목이 있어야 한다

    node._service.changeStat( 7, "p", "wins", 9, LeaderboardUpdate::Latest, UnixMilliseconds{ 100 }, 1 );
    node.step();
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 0 );
    node._service.changeStat( 7, "p", "wins", 10, LeaderboardUpdate::Latest, UnixMilliseconds{ 200 }, 2 );
    node._service.changeStat( 7, "p", "wins", 11, LeaderboardUpdate::Latest, UnixMilliseconds{ 300 }, 3 ); // 다시 넘음 — 더 주지 않는다
    node.step();
    vector<AchievementUnlock> listUnlock;
    node._service.drainAchievementUnlocks( listUnlock );
    SW_ASSERT_EQUAL( listUnlock.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( listUnlock[0]._state._achievementID.c_str(), "ten_wins" );
    SW_EXPECT_EQUAL( listUnlock[0]._state._unlockedMs, int64( 200 ) );
    SW_EXPECT_EQUAL( listUnlock[0]._accountID, AccountID( 7 ) );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 1 );

    node._service.readAchievements( 7, 4 );
    node.step();
    SW_ASSERT_EQUAL( node._listCompletion.back()._listAchievement.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( node._listCompletion.back()._listAchievement[0]._achievementID.c_str(), "ten_wins" );
}

SW_TEST_CASE( LeaderboardSeasonTest, AchievementProgressShowsStatsAndHidesSecretOnesUntilUnlocked )
{
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    SeasonNode              node( &database, &cacheDatabase );
    AchievementDefinition   wins;
    wins._achievementID = "ten_wins";
    wins._statName      = "wins";
    wins._threshold     = 10;
    AchievementDefinition secret;
    secret._achievementID = "secret_room";
    secret._statName      = "rooms";
    secret._threshold     = 1;
    secret._bHidden       = true;
    SW_ASSERT_TRUE( node._service.registerAchievement( wins ) );
    SW_ASSERT_TRUE( node._service.registerAchievement( secret ) );

    node._service.changeStat( 7, "p", "wins", 4, LeaderboardUpdate::Latest, UnixMilliseconds{ 100 }, 1 );
    node.step();
    node._service.readAchievementProgress( 7, 2 );
    node.step();
    SW_ASSERT_TRUE( node._listCompletion.back()._result == LeaderboardResult::Ok );
    const vector<AchievementProgress>& listBefore = node._listCompletion.back()._listProgress;
    SW_ASSERT_EQUAL( listBefore.size(), size_t( 2 ) );
    SW_EXPECT_TRUE( listBefore[0]._bHidden == SW_TRUE ); // "secret_room" 이 id 순서로 먼저다
    SW_EXPECT_TRUE( listBefore[0]._achievementID.empty() );
    SW_EXPECT_EQUAL( listBefore[0]._threshold, int64( 0 ) );
    SW_EXPECT_STREQ( listBefore[1]._achievementID.c_str(), "ten_wins" );
    SW_EXPECT_EQUAL( listBefore[1]._progress, int64( 4 ) );
    SW_EXPECT_EQUAL( listBefore[1]._threshold, int64( 10 ) );
    SW_EXPECT_EQUAL( listBefore[1]._unlockedMs, int64( 0 ) );

    node._service.changeStat( 7, "p", "rooms", 1, LeaderboardUpdate::Latest, UnixMilliseconds{ 200 }, 3 );
    node.step();
    node._service.readAchievementProgress( 7, 4 );
    node.step();
    const vector<AchievementProgress>& listAfter = node._listCompletion.back()._listProgress;
    SW_ASSERT_EQUAL( listAfter.size(), size_t( 2 ) );
    SW_EXPECT_STREQ( listAfter[0]._achievementID.c_str(), "secret_room" ); // 달성하면 드러난다
    SW_EXPECT_EQUAL( listAfter[0]._unlockedMs, int64( 200 ) );
    SW_EXPECT_TRUE( listAfter[0]._bHidden == SW_TRUE );
}

SW_TEST_CASE( LeaderboardSeasonTest, SettlementRanksRewardsAndIsIdempotent )
{
    using Internal = LeaderboardSeasonTestInternal;
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    SeasonNode              node( &database, &cacheDatabase );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeSeasonBoard() ) );
    node._service.submitScore( "arena", 1, "a", 50, UnixMilliseconds{ 500 }, 1 ); // 시즌 전
    node.step();
    SW_EXPECT_TRUE( node._listCompletion.back()._result == LeaderboardResult::NotRanked );
    node.submitThirtyPlayers();
    SW_EXPECT_EQUAL( node._listCompletion.back()._periodID, uint64( 1 ) );

    node._service.readSeasonResult( "arena", 1, 1, 50 ); // 정산 전
    node.step();
    SW_EXPECT_TRUE( node._listCompletion.back()._result == LeaderboardResult::NotRanked );

    SettleRecorder recorder;
    node._service.settleSeason( "arena", 1, UnixMilliseconds{ 2100 }, recorder.makeDelegate() );
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
    SW_EXPECT_EQUAL( node._listCompletion.back()._periodID, uint64( 1 ) );

    const uint64 hashAfter = database.computeContentHash();
    node._service.settleSeason( "arena", 1, UnixMilliseconds{ 2200 }, recorder.makeDelegate() ); // 다른 서버가 다시 돌았다
    node.step();
    SW_EXPECT_TRUE( recorder._listSucceeded.back() == SW_TRUE );
    SW_EXPECT_EQUAL( database.computeContentHash(), hashAfter );
    node._service.settleSeason( "arena", 9, UnixMilliseconds{ 2200 }, recorder.makeDelegate() ); // 없는 시즌
    SW_EXPECT_TRUE( recorder._listSucceeded.back() == SW_FALSE );
}

SW_TEST_CASE( LeaderboardSeasonTest, InterruptedSettlementResumesWithoutDuplicates )
{
    using Internal = LeaderboardSeasonTestInternal;
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    SeasonNode              node( &database, &cacheDatabase );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeSeasonBoard() ) );
    node.submitThirtyPlayers();

    database.armFault( ServiceStoreFault::RejectCommit, 1 ); // 첫 묶음(12 명)은 쓰고 둘째 묶음에서 실패
    SettleRecorder recorder;
    node._service.settleSeason( "arena", 1, UnixMilliseconds{ 2100 }, recorder.makeDelegate() );
    node.step();
    SW_ASSERT_EQUAL( recorder._listSucceeded.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( recorder._listSucceeded.back() == SW_FALSE );
    SW_EXPECT_EQUAL( Internal::countSeasonResults( database ), 12 ); // 한 트랜잭션은 12 명이다(사람마다 쓰기 다섯)

    node._service.settleSeason( "arena", 1, UnixMilliseconds{ 2200 }, recorder.makeDelegate() );
    node.step();
    SW_EXPECT_TRUE( recorder._listSucceeded.back() == SW_TRUE );
    SW_EXPECT_EQUAL( Internal::countSeasonResults( database ), 30 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 10 );
}

SW_TEST_CASE( LeaderboardSeasonTest, ScheduledWindowRunsSettlementOnce )
{
    using Internal = LeaderboardSeasonTestInternal;
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    SeasonNode              node( &database, &cacheDatabase );
    ServiceScheduler        scheduler;
    scheduler.initialize( &node._store, 1 );
    node._service.setScheduler( &scheduler );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeSeasonBoard() ) );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeSeasonBoard() ) ); // 다시 올려도 작업은 하나
    node.submitThirtyPlayers();

    scheduler.update( UnixMilliseconds{ 1900 } ); // 창 전
    node.step();
    SW_EXPECT_EQUAL( Internal::countSeasonResults( database ), 0 );
    scheduler.update( UnixMilliseconds{ 2100 } ); // 창 안 — 차지 → 처리기 → 정산 → 회차 끝
    node.step();
    SW_EXPECT_EQUAL( Internal::countSeasonResults( database ), 30 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 10 );
    SW_EXPECT_EQUAL( scheduler.getPendingWorkCount(), 0 );
    SW_EXPECT_EQUAL( node._service.getPendingCount(), 0 );

    const uint64 hashAfter = database.computeContentHash();
    scheduler.update( UnixMilliseconds{ 2200 } ); // 같은 회차는 다시 돌지 않는다
    node.step();
    SW_EXPECT_EQUAL( database.computeContentHash(), hashAfter );
    node._service.shutdown();
    scheduler.shutdown();
}

SW_TEST_CASE( LeaderboardSeasonTest, LargeSettlementRunsInPagesWithContinuousRanks )
{
    // 정산은 순위 읽기 일 하나 + 600 명씩 페이지 일 여럿이다 — 시즌 하나가 저장소 워커 하나를 오래 잡지 않는다. 페이지 경계를 넘어도 순위가 이어진다.
    using Internal = LeaderboardSeasonTestInternal;
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    SeasonNode              node( &database, &cacheDatabase );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeSeasonBoard() ) );
    constexpr AccountID kPlayerCount = 1250; // 600 + 600 + 50
    for ( AccountID accountID = 1; accountID <= kPlayerCount; ++accountID )
    {
        node._service.submitScore( "arena", accountID, "p", static_cast<int64>( 100000 - accountID ), UnixMilliseconds{ 1500 }, 0 );
    }
    node.step();

    SettleRecorder recorder;
    node._service.settleSeason( "arena", 1, UnixMilliseconds{ 2100 }, recorder.makeDelegate() );
    SW_EXPECT_EQUAL( node._service.getPendingCount(), 1 ); // 정산 하나 — 페이지 수와 상관없이
    node.step();
    SW_ASSERT_EQUAL( recorder._listSucceeded.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( recorder._listSucceeded[0] == SW_TRUE );
    SW_EXPECT_EQUAL( Internal::countSeasonResults( database ), static_cast<int32>( kPlayerCount ) );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 10 );
    SW_EXPECT_EQUAL( node._service.getPendingCount(), 0 );

    node._service.readSeasonResult( "arena", 1, 601, 77 ); // 둘째 페이지의 첫 사람
    node.step();
    SW_ASSERT_EQUAL( node._listCompletion.back()._listEntry.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._listCompletion.back()._listEntry[0]._rank, 601 );
    node._service.readSeasonResult( "arena", 1, kPlayerCount, 78 ); // 마지막 페이지의 마지막 사람
    node.step();
    SW_ASSERT_EQUAL( node._listCompletion.back()._listEntry.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._listCompletion.back()._listEntry[0]._rank, static_cast<int32>( kPlayerCount ) );
}

SW_TEST_CASE( LeaderboardSeasonTest, PeriodRanksExpireFromTheCacheAfterTheirPeriod )
{
    // 주간 보드의 캐시 순위(정렬 집합)는 그 주가 끝나고 하루 뒤 캐시가 지운다 — 기간마다 쌓이지 않는다. 초기화 없는 보드는 지우지 않는다.
    MemoryServiceDatabase      database;
    ManualClock                cacheClock{ 1000000 };
    MemoryEphemeralDatabase    cacheDatabase{ cacheClock };
    constexpr UnixMilliseconds kNowMs{ 1767225600000 }; // 2026-01-01 00:00 UTC(목요일)
    constexpr int64            kWeekMs = 604800000;
    constexpr int64            kDayMs  = 86400000;
    cacheClock.setMilliseconds( kNowMs.count() );
    SeasonNode            node( &database, &cacheDatabase );
    LeaderboardDefinition weekly;
    weekly._boardID = "weekly";
    weekly._reset   = LeaderboardReset::Weekly;
    SW_ASSERT_TRUE( node._service.registerBoard( weekly ) );
    LeaderboardDefinition forever;
    forever._boardID = "forever";
    SW_ASSERT_TRUE( node._service.registerBoard( forever ) );
    node._service.submitScore( "weekly", 1, "a", 10, kNowMs, 0 );
    node._service.submitScore( "forever", 1, "a", 10, kNowMs, 0 );
    node.step();

    const uint64 weekStart  = node._service.computePeriodID( weekly, kNowMs );
    const string weeklyKey  = LeaderboardService::makeRankKey( "weekly", weekStart );
    const string foreverKey = LeaderboardService::makeRankKey( "forever", 0 );
    SW_EXPECT_EQUAL( cacheDatabase.execute( EphemeralRequest::makeScoreRange( weeklyKey, 0, 10 ) )._listMember.size(), size_t( 1 ) );

    cacheClock.setMilliseconds( static_cast<int64>( weekStart ) + kWeekMs + kDayMs - 1 ); // 끝나고 하루 안 — 아직 있다
    SW_EXPECT_EQUAL( cacheDatabase.execute( EphemeralRequest::makeScoreRange( weeklyKey, 0, 10 ) )._listMember.size(), size_t( 1 ) );
    cacheClock.setMilliseconds( static_cast<int64>( weekStart ) + kWeekMs + kDayMs + 1 );
    SW_EXPECT_TRUE( cacheDatabase.execute( EphemeralRequest::makeScoreRange( weeklyKey, 0, 10 ) )._listMember.empty() );
    SW_EXPECT_EQUAL( cacheDatabase.execute( EphemeralRequest::makeScoreRange( foreverKey, 0, 10 ) )._listMember.size(), size_t( 1 ) );
}
