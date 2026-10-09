// 순위표 — Best · Latest · Sum, 오름차순 표, 상위 · 내 둘레(이름 포함 · 점수 없으면 NotRanked), 일간 표의 기간 경계, 캐시를 통째로 잃어도 영속에서 다시 채운
// 같은 순위(그동안 온 읽기는 줄을 서고 다음 읽기는 다시 채우지 않는다), 통계 → 연동 표 · 통계 조회, 규칙 밖 값 · 저장소 실패.
#include "pch.h"

#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Server/Leaderboard/LeaderboardService.h"

#include "TestFramework/TestFramework.h"

#include <algorithm>

using namespace sw;

namespace
{
    struct LeaderboardServiceTestInternal
    {
        static constexpr int64 kMonday20261005Ms = 1791158400000ll; // 2026-10-05 00:00 UTC(월)
        static constexpr int64 kDayMs            = 86400000;

        static LeaderboardDefinition makeBoard( string_view boardId, LeaderboardUpdate update, LeaderboardOrder order = LeaderboardOrder::Descending,
                                                LeaderboardReset reset = LeaderboardReset::None )
        {
            LeaderboardDefinition definition;
            definition._boardId = string( boardId );
            definition._update  = update;
            definition._order   = order;
            definition._reset   = reset;
            return definition;
        }
    };

    struct BoardNode
    {
        MemoryServiceStore            _store;
        MemoryEphemeralStore          _cache;
        EphemeralStoreRouter          _router;
        LeaderboardService            _service;
        vector<LeaderboardCompletion> _listCompletion;

        BoardNode( MemoryServiceDatabase* pDatabase, MemoryEphemeralDatabase* pCacheDatabase )
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

        const LeaderboardCompletion& getLast() const { return _listCompletion.back(); }
    };
} // namespace

SW_TEST_CASE( LeaderboardServiceTest, BestKeepsHighestAndTopListsWithNames )
{
    using Internal = LeaderboardServiceTestInternal;
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    BoardNode               node( &database, &cacheDatabase );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeBoard( "kills", LeaderboardUpdate::Best ) ) );
    SW_EXPECT_FALSE( node._service.registerBoard( Internal::makeBoard( "Kills", LeaderboardUpdate::Best ) ) ); // id 규칙
    node._service.submitScore( "kills", 1, "alice", 10, 0, 1 );
    node._service.submitScore( "kills", 2, "bob", 30, 0, 2 );
    node._service.submitScore( "kills", 3, "carol", 20, 0, 3 );
    node._service.submitScore( "kills", 2, "bob", 5, 0, 4 ); // 더 낮다 — 그대로 30
    node.step();
    SW_EXPECT_EQUAL( node.getLast()._score, int64( 30 ) );

    node._service.readTop( "kills", 0, 2, 0, 5 );
    node.step();
    SW_ASSERT_EQUAL( node.getLast()._listEntry.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( node.getLast()._listEntry[0]._accountId, AccountId( 2 ) );
    SW_EXPECT_STREQ( node.getLast()._listEntry[0]._displayName.c_str(), "bob" );
    SW_EXPECT_EQUAL( node.getLast()._listEntry[1]._rank, 2 );

    node._service.readAround( "kills", 1, 1, 0, 6 );
    node.step();
    SW_ASSERT_EQUAL( node.getLast()._listEntry.size(), size_t( 2 ) ); // 2 등 carol, 3 등 alice(아래가 없다)
    SW_EXPECT_EQUAL( node.getLast()._listEntry[1]._accountId, AccountId( 1 ) );
    SW_EXPECT_EQUAL( node.getLast()._listEntry[1]._rank, 3 );
    node._service.readAround( "kills", 9, 1, 0, 7 ); // 점수 없음
    node.step();
    SW_EXPECT_TRUE( node.getLast()._result == LeaderboardResult::NotRanked );
    node._service.readTop( "nope", 0, 2, 0, 8 );
    node._service.readTop( "kills", 0, LeaderboardLimit::kMaxPage + 1, 0, 9 );
    node._service.submitScore( "kills", 1, "alice", LeaderboardLimit::kMaxAbsScore + 1, 0, 10 );
    node.step();
    SW_ASSERT_TRUE( node._listCompletion.size() >= size_t( 3 ) );
    const size_t lastIndex = node._listCompletion.size() - 1;
    SW_EXPECT_TRUE( node._listCompletion[lastIndex - 2]._result == LeaderboardResult::UnknownBoard );
    SW_EXPECT_TRUE( node._listCompletion[lastIndex - 1]._result == LeaderboardResult::Invalid );
    SW_EXPECT_TRUE( node._listCompletion[lastIndex]._result == LeaderboardResult::Invalid );
}

SW_TEST_CASE( LeaderboardServiceTest, AscendingSumAndLatest )
{
    using Internal = LeaderboardServiceTestInternal;
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    BoardNode               node( &database, &cacheDatabase );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeBoard( "lap_time", LeaderboardUpdate::Best, LeaderboardOrder::Ascending ) ) );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeBoard( "gold_earned", LeaderboardUpdate::Sum ) ) );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeBoard( "last_level", LeaderboardUpdate::Latest ) ) );
    node._service.submitScore( "lap_time", 1, "a", 9000, 0, 1 );
    node._service.submitScore( "lap_time", 2, "b", 8500, 0, 2 );
    node._service.submitScore( "lap_time", 1, "a", 8000, 0, 3 ); // 더 빠르다 — 바뀐다
    node._service.submitScore( "gold_earned", 1, "a", 100, 0, 4 );
    node._service.submitScore( "gold_earned", 1, "a", 50, 0, 5 );
    node.step();
    SW_EXPECT_EQUAL( node.getLast()._score, int64( 150 ) );
    node._service.submitScore( "last_level", 1, "a", 7, 0, 6 );
    node._service.submitScore( "last_level", 1, "a", 3, 0, 7 ); // 늘 덮는다
    node.step();
    SW_EXPECT_EQUAL( node.getLast()._score, int64( 3 ) );
    node._service.readTop( "lap_time", 0, 2, 0, 8 );
    node.step();
    SW_ASSERT_EQUAL( node.getLast()._listEntry.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( node.getLast()._listEntry[0]._accountId, AccountId( 1 ) );
    SW_EXPECT_EQUAL( node.getLast()._listEntry[0]._score, int64( 8000 ) );
    SW_EXPECT_EQUAL( node.getLast()._listEntry[1]._score, int64( 8500 ) );
}

SW_TEST_CASE( LeaderboardServiceTest, DailyBoardStartsEmptyAfterReset )
{
    using Internal = LeaderboardServiceTestInternal;
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    BoardNode               node( &database, &cacheDatabase );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeBoard( "daily_runs", LeaderboardUpdate::Sum, LeaderboardOrder::Descending, LeaderboardReset::Daily ) ) );
    node._service.submitScore( "daily_runs", 1, "a", 3, Internal::kMonday20261005Ms + 1000, 1 );
    node.step();
    node._service.readTop( "daily_runs", 0, 10, Internal::kMonday20261005Ms + 2000, 2 );
    node.step();
    SW_EXPECT_EQUAL( node.getLast()._listEntry.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node.getLast()._periodId, uint64( Internal::kMonday20261005Ms ) );
    node._service.readTop( "daily_runs", 0, 10, Internal::kMonday20261005Ms + Internal::kDayMs, 3 ); // 다음 날
    node.step();
    SW_EXPECT_EQUAL( node.getLast()._listEntry.size(), size_t( 0 ) );
    SW_EXPECT_EQUAL( node.getLast()._periodId, uint64( Internal::kMonday20261005Ms + Internal::kDayMs ) );
}

SW_TEST_CASE( LeaderboardServiceTest, LostCacheIsRebuiltFromStoreWithTheSameOrder )
{
    using Internal = LeaderboardServiceTestInternal;
    MemoryServiceDatabase database;
    int64                 bestScore = 0;
    {
        MemoryEphemeralDatabase cacheDatabase;
        BoardNode               node( &database, &cacheDatabase );
        SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeBoard( "kills", LeaderboardUpdate::Best ) ) );
        for ( AccountId accountId = 1; accountId <= 300; ++accountId ) // 다시 채우기 묶음(256)보다 많이 — 점수는 서로 다르다(307 은 소수)
        {
            const int64 score = static_cast<int64>( accountId * 7 % 307 );
            bestScore         = std::max( bestScore, score );
            node._service.submitScore( "kills", accountId, "p", score, 0, accountId );
        }
        node.step();
    }
    MemoryEphemeralDatabase emptyCache; // 캐시 서버가 비었다
    BoardNode               node( &database, &emptyCache );
    SW_ASSERT_TRUE( node._service.registerBoard( Internal::makeBoard( "kills", LeaderboardUpdate::Best ) ) );
    node._service.readTop( "kills", 0, 3, 0, 1 );
    node._service.readTop( "kills", 0, 1, 0, 2 ); // 다시 채우는 동안 줄을 선다
    node.step();
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 2 ) );
    SW_ASSERT_EQUAL( node._listCompletion[0]._listEntry.size(), size_t( 3 ) );
    SW_EXPECT_EQUAL( node._listCompletion[0]._listEntry[0]._score, bestScore );
    SW_EXPECT_EQUAL( node._listCompletion[1]._listEntry[0]._accountId, node._listCompletion[0]._listEntry[0]._accountId );

    const uint64 commitBefore = database.getCommitCount();
    node._service.readTop( "kills", 0, 1, 0, 3 ); // 준비 표시가 있다 — 다시 채우지 않는다
    node._service.submitScore( "kills", 301, "late", 1000, 0, 4 );
    node.step();
    SW_EXPECT_EQUAL( node._service.getPendingCount(), 0 );
    SW_EXPECT_EQUAL( database.getCommitCount(), commitBefore + 1 );
    node._service.readTop( "kills", 0, 1, 0, 5 );
    node.step();
    SW_EXPECT_EQUAL( node.getLast()._listEntry[0]._accountId, AccountId( 301 ) );
}

SW_TEST_CASE( LeaderboardServiceTest, StatFeedsLinkedBoardAndStoreFailure )
{
    using Internal = LeaderboardServiceTestInternal;
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    BoardNode               node( &database, &cacheDatabase );
    LeaderboardDefinition   linked = Internal::makeBoard( "total_wins", LeaderboardUpdate::Latest );
    linked._sourceStat             = "wins";
    SW_ASSERT_TRUE( node._service.registerBoard( linked ) );
    node._service.changeStat( 1, "alice", "wins", 1, LeaderboardUpdate::Sum, 0, 1 );
    node._service.changeStat( 1, "alice", "wins", 1, LeaderboardUpdate::Sum, 0, 2 );
    node.step();
    SW_EXPECT_EQUAL( node.getLast()._score, int64( 2 ) );
    node._service.readTop( "total_wins", 0, 1, 0, 3 );
    node.step();
    SW_ASSERT_EQUAL( node.getLast()._listEntry.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node.getLast()._listEntry[0]._score, int64( 2 ) );
    SW_EXPECT_STREQ( node.getLast()._listEntry[0]._displayName.c_str(), "alice" );
    node._service.readStats( 1, 4 );
    node.step();
    SW_ASSERT_EQUAL( node.getLast()._listStat.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( node.getLast()._listStat[0]._name.c_str(), "wins" );
    SW_EXPECT_EQUAL( node.getLast()._listStat[0]._value, int64( 2 ) );
    node._service.changeStat( 1, "alice", "Bad Name", 1, LeaderboardUpdate::Sum, 0, 5 );
    node.step();
    SW_EXPECT_TRUE( node.getLast()._result == LeaderboardResult::Invalid );

    const uint64 hashBefore = database.computeContentHash();
    database.armFault( ServiceStoreFault::RejectCommit );
    node._service.submitScore( "total_wins", 2, "bob", 9, 0, 6 );
    node.step();
    SW_EXPECT_TRUE( node.getLast()._result == LeaderboardResult::Unavailable );
    SW_EXPECT_EQUAL( database.computeContentHash(), hashBefore );
}
