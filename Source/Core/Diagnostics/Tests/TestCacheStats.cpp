// CacheStats — 캐시 계측 레지스트리: 센 값(적중 · 실패 · 비우기 · 항목 수), 이름 중복 거절, 측정 창 시작 뒤의 값, 여러 스레드의 더하기.
// 칸은 프로세스 끝까지 남으므로 테스트마다 이름이 달라야 하고, 같은 프로세스에서 다시 돌려도(반복 실행) 깨지지 않게 이름 올리기는 `getOrCreate` 로 시작한다.
#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/Container/formatString.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 이름의 스냅샷입니다. 없으면 nullptr 입니다. */
    const sw::CacheStatsSnapshot* findSnapshot( const sw::vector<sw::CacheStatsSnapshot>& listSnapshot, sw::string_view name )
    {
        for ( const sw::CacheStatsSnapshot& snapshot : listSnapshot )
        {
            if ( sw::string_view{ snapshot._name } == name )
                return &snapshot;
        }
        return nullptr;
    }
} // namespace

/**
 * @brief [CacheStatsTest] 적중 · 실패 · 비우기 · 항목 수가 올린 이름의 칸에 그대로 셈해진다
 */
SW_TEST_CASE( CacheStatsTest, CountsHitMissClearAndItemsPerName )
{
    const sw::CacheStatsHandle handle = sw::CacheStats::getOrCreate( "TestCacheStats.Counts" );
    SW_REQUIRE_TRUE( handle.isValid() );
    sw::vector<sw::CacheStatsSnapshot> listBefore;
    sw::CacheStats::collect( listBefore );
    const sw::CacheStatsSnapshot* pBefore = findSnapshot( listBefore, "TestCacheStats.Counts" );
    SW_REQUIRE_NOT_NULL( pBefore );

    handle.recordHit();
    handle.recordHit();
    handle.recordHit();
    handle.recordMiss();
    handle.recordClear();
    handle.addItemCount( 5 );
    handle.addItemCount( -2 );

    sw::vector<sw::CacheStatsSnapshot> listAfter;
    sw::CacheStats::collect( listAfter );
    const sw::CacheStatsSnapshot* pAfter = findSnapshot( listAfter, "TestCacheStats.Counts" );
    SW_REQUIRE_NOT_NULL( pAfter );
    SW_EXPECT_EQUAL( pBefore->_hitCount + 3, pAfter->_hitCount );
    SW_EXPECT_EQUAL( pBefore->_missCount + 1, pAfter->_missCount );
    SW_EXPECT_EQUAL( pBefore->_clearCount + 1, pAfter->_clearCount );
    SW_EXPECT_EQUAL( pBefore->_itemCount + 3, pAfter->_itemCount );
    SW_EXPECT_TRUE( pAfter->_bItemCountKnown );
}

/**
 * @brief [CacheStatsTest] 같은 이름을 두 번 올리면 두 번째를 거절하고(첫 칸을 둔다) 빈 핸들의 기록은 아무 일도 하지 않으며, `getOrCreate` 는 같은 칸을 돌려준다
 */
SW_TEST_CASE( CacheStatsTest, DuplicateNameIsRejectedAndGetOrCreateReturnsTheSameCounters )
{
    // 레지스트리는 프로세스에 하나라 반복 실행마다 새 이름이 필요하다 — 칸 수는 등록마다 늘고 줄지 않아 접미사로 쓴다.
    utf8 arrName[sw::CacheStatsCounters::kMaxNameLength + 1]{};
    sw::formatstring( arrName, static_cast<uint32>( sizeof( arrName ) ), "TestCacheStats.Strict.%#", sw::CacheStats::getCount() );

    const uint32               beforeCount = sw::CacheStats::getCount();
    const sw::CacheStatsHandle first       = sw::CacheStats::registerCache( arrName );
    SW_REQUIRE_TRUE( first.isValid() );
    SW_EXPECT_EQUAL( beforeCount + 1, sw::CacheStats::getCount() );

    {
        SW_EXPECT_LOG( "CacheStats.DuplicateName" );
        const sw::CacheStatsHandle second = sw::CacheStats::registerCache( arrName );
        SW_EXPECT_FALSE( second.isValid() );
        SW_EXPECT_EQUAL( beforeCount + 1, sw::CacheStats::getCount() ); // 거절은 칸을 늘리지 않는다
        second.recordHit();                                             // 빈 핸들의 기록은 아무 일도 하지 않는다(첫 칸을 건드리지 않는다)
    }
    SW_EXPECT_EQUAL( uint64{ 0 }, first.getCounters()->getHitCount() );

    const sw::CacheStatsHandle again = sw::CacheStats::getOrCreate( arrName );
    SW_EXPECT_TRUE( again.getCounters() == first.getCounters() );
    again.recordHit();
    SW_EXPECT_EQUAL( uint64{ 1 }, first.getCounters()->getHitCount() ); // 같은 칸이다
    SW_EXPECT_EQUAL( beforeCount + 1, sw::CacheStats::getCount() );
}

/**
 * @brief [CacheStatsTest] 비었거나 이름 길이 상한을 넘는 이름은 올리지 않는다
 */
SW_TEST_CASE( CacheStatsTest, EmptyAndOverlongNamesAreRejected )
{
    SW_EXPECT_LOG( "CacheStats.BadName" );
    const uint32 beforeCount = sw::CacheStats::getCount();
    SW_EXPECT_FALSE( sw::CacheStats::registerCache( "" ).isValid() );
    const sw::string overlong( sw::CacheStatsCounters::kMaxNameLength + 1, 'x' );
    SW_EXPECT_FALSE( sw::CacheStats::getOrCreate( overlong ).isValid() );
    SW_EXPECT_EQUAL( beforeCount, sw::CacheStats::getCount() );
}

/**
 * @brief [CacheStatsTest] 측정 창을 시작하면 그 뒤에 센 적중 · 실패 · 비우기만 보이고 항목 수는 그대로다
 */
SW_TEST_CASE( CacheStatsTest, WindowStartHidesEarlierCountsButKeepsItemCount )
{
    const sw::CacheStatsHandle handle = sw::CacheStats::getOrCreate( "TestCacheStats.Window" );
    SW_REQUIRE_TRUE( handle.isValid() );
    handle.recordHit();
    handle.recordMiss();
    handle.recordClear();
    handle.addItemCount( 7 );

    sw::CacheStats::markWindowStart();
    sw::vector<sw::CacheStatsSnapshot> listStart;
    sw::CacheStats::collect( listStart );
    const sw::CacheStatsSnapshot* pStart = findSnapshot( listStart, "TestCacheStats.Window" );
    SW_REQUIRE_NOT_NULL( pStart );
    SW_EXPECT_EQUAL( uint64{ 0 }, pStart->_hitCount );
    SW_EXPECT_EQUAL( uint64{ 0 }, pStart->_missCount );
    SW_EXPECT_EQUAL( uint64{ 0 }, pStart->_clearCount );
    SW_EXPECT_TRUE( pStart->_bItemCountKnown );

    handle.recordHit();
    handle.recordHit();
    sw::vector<sw::CacheStatsSnapshot> listLater;
    sw::CacheStats::collect( listLater );
    const sw::CacheStatsSnapshot* pLater = findSnapshot( listLater, "TestCacheStats.Window" );
    SW_REQUIRE_NOT_NULL( pLater );
    SW_EXPECT_EQUAL( uint64{ 2 }, pLater->_hitCount );
    SW_EXPECT_EQUAL( pStart->_itemCount, pLater->_itemCount );
}

/**
 * @brief [CacheStatsTest] 여러 스레드가 같은 칸에 더한 적중이 하나도 빠지지 않는다(락 없는 원자 증가)
 */
SW_TEST_CASE( CacheStatsTest, ConcurrentRecordsAreAllCounted )
{
    constexpr uint32 kThreadCount = 8;
    constexpr uint32 kRecordCount = 10000;

    const sw::CacheStatsHandle handle = sw::CacheStats::getOrCreate( "TestCacheStats.Threads" );
    SW_REQUIRE_TRUE( handle.isValid() );
    const uint64 hitBefore = handle.getCounters()->getHitCount();

    sw::vector<sw::thread> listThread;
    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [handle]()
        {
            for ( uint32 index = 0; index < kRecordCount; ++index )
            {
                handle.recordHit();
            }
        } );
    }
    for ( sw::thread& worker : listThread )
    {
        worker.join();
    }
    SW_EXPECT_EQUAL( hitBefore + uint64{ kThreadCount } * kRecordCount, handle.getCounters()->getHitCount() );
}
