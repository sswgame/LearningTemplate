#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"

#include "Engine/Asset/Cache/SharedAssetTable.h"

// SharedAssetTableTest — 경로로 나눠 주는 에셋 표(스켈레톤 · 클립 · 리그 · 캐릭터 데이터가 쓴다): 같은 경로는 같은 객체, 마지막 사용자가 놓으면 사라짐,
// 다시 읽기는 떼어 내기 하나(쥔 쪽은 옛 객체 그대로, 다른 스레드가 읽는 중에도 내용이 바뀌지 않는다 — A-7), 읽기 실패는 옛 내용을 지킨다. 로드 함수는 가짜다(nogpu).

namespace
{
    struct FakeAsset
    {
        int32 _generation{ 0 };
    };

    int32 s_loadCount  = 0;
    bool  s_bLoadFails = false;

    bool loadFake( const sw::AssetID&, FakeAsset& outAsset )
    {
        if ( s_bLoadFails )
            return false;
        outAsset._generation = ++s_loadCount;
        return true;
    }

    void resetFakeLoader()
    {
        s_loadCount  = 0;
        s_bLoadFails = false;
    }
} // namespace

/**
 * @brief [SharedAssetTableTest] 같은 경로는 같은 객체를 나눠 주고, 마지막 사용자가 놓으면 표에서 사라져 다음 요청이 다시 읽는다
 */
SW_TEST_CASE( SharedAssetTableTest, SharesWhileHeld )
{
    resetFakeLoader();
    sw::SharedAssetTable<FakeAsset> table;
    sw::shared_ptr<const FakeAsset> first;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/a.skeleton.xml", &loadFake, first ) );
    SW_EXPECT_TRUE( table.findLive( "game/x/a.skeleton.xml" ).get() == first.get() );
    SW_EXPECT_EQUAL( size_t( 1 ), table.countLive() );
    SW_EXPECT_TRUE( table.findLive( "game\\x\\a.skeleton.xml" ).get() == first.get() ); // 구분자는 맞춘 키로 찾는다

    first.reset();
    SW_EXPECT_EQUAL( size_t( 0 ), table.countLive() );
    SW_EXPECT_TRUE( table.findLive( "game/x/a.skeleton.xml" ) == nullptr );
    sw::shared_ptr<const FakeAsset> again;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/a.skeleton.xml", &loadFake, again ) );
    SW_EXPECT_EQUAL( 2, again->_generation );
}

/**
 * @brief [SharedAssetTableTest] 다시 읽기는 표에서 떼어 낼 뿐 쥔 객체를 바꾸지 않고, 다시 받으면 새 객체다. 읽기 실패는 옛 내용을 지킨다
 */
SW_TEST_CASE( SharedAssetTableTest, ReloadDetachesAndLeavesHeldObjectUntouched )
{
    resetFakeLoader();
    sw::SharedAssetTable<FakeAsset> table;
    sw::shared_ptr<const FakeAsset> held;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/b.clip", &loadFake, held ) );
    const uint32 reloadsBefore = table.getReloadCount();

    s_bLoadFails = true;
    SW_EXPECT_FALSE( table.detachShared( "game/x/b.clip", &loadFake ) );
    SW_EXPECT_TRUE( table.findLive( "game/x/b.clip" ).get() == held.get() ); // 읽지 못하면 표도 그대로
    SW_EXPECT_EQUAL( reloadsBefore, table.getReloadCount() );
    s_bLoadFails = false;

    SW_REQUIRE_TRUE( table.detachShared( "game/x/b.clip", &loadFake ) );
    SW_EXPECT_EQUAL( 1, held->_generation ); // 쥔 쪽은 옛 객체 그대로
    SW_EXPECT_TRUE( table.findLive( "game/x/b.clip" ) == nullptr );
    SW_EXPECT_EQUAL( reloadsBefore + 1, table.getReloadCount() );

    sw::shared_ptr<const FakeAsset> fresh;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/b.clip", &loadFake, fresh ) );
    SW_EXPECT_TRUE( fresh.get() != held.get() );
    SW_EXPECT_EQUAL( 3, fresh->_generation ); // 확인용 읽기(2) 뒤의 새 읽기

    SW_EXPECT_FALSE( table.detachShared( "game/x/none.clip", &loadFake ) ); // 아무도 쥐지 않은 경로
}

/**
 * @brief [SharedAssetTableTest] 워커가 쥔 객체를 읽는 동안 게임 스레드가 다시 읽어도 워커가 보는 내용은 한 번도 바뀌지 않는다(TSan 이면 데이터 경쟁도 없다 — A-7)
 */
SW_TEST_CASE( SharedAssetTableTest, ReloadWhileWorkerReads )
{
    resetFakeLoader();
    sw::SharedAssetTable<FakeAsset> table;
    sw::shared_ptr<const FakeAsset> held;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/c.clip", &loadFake, held ) );
    const int32       seenGeneration = held->_generation;
    sw::atomic<bool>  bStop{ false };
    sw::atomic<int32> changedCount{ 0 };
    sw::thread        reader{ sw::ThreadTestAccess::getKey(), [&]
    {
        while ( bStop.load( std::memory_order_acquire ) == false )
        {
            if ( held->_generation != seenGeneration )
                changedCount.fetch_add( 1, std::memory_order_relaxed );
        }
    } };
    for ( int32 i = 0; i < 64; ++i )
    {
        // 다시 읽은 뒤 다시 받아 표에 다시 올린다 — 다음 다시 읽기가 쥔 경로를 본다.
        sw::shared_ptr<const FakeAsset> again;
        SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/c.clip", &loadFake, again ) );
        SW_REQUIRE_TRUE( table.detachShared( "game/x/c.clip", &loadFake ) );
    }
    bStop.store( true, std::memory_order_release );
    reader.join();
    SW_EXPECT_EQUAL( 0, changedCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_EQUAL( seenGeneration, held->_generation );
}
