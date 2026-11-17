#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Asset/Cache/SharedAssetTable.h"

// SharedAssetTableTest — 경로로 나눠 주는 에셋 표(스켈레톤 · 클립 · 리그 · 캐릭터 데이터가 쓴다): 같은 경로는 같은 객체, 마지막 사용자가 놓으면 사라짐,
// 다시 읽기는 새 판 넣기 하나(스냅숏은 옛 객체 그대로 — 다른 스레드가 읽는 중에도 내용이 바뀌지 않는다, A-7 — 따라가는 참조는 새 판을 본다), 읽기 실패는 옛 내용을 지킨다. 로드 함수는 가짜다(nogpu).

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
 * @brief [SharedAssetTableTest] 다시 읽기는 쥔 객체를 바꾸지 않고 새 객체를 새 판으로 넣는다 — 스냅숏(`shared_ptr`)은 옛 내용, 따라가는 참조와 다음 받기는 새 내용. 읽기 실패는 옛 내용을 지킨다
 */
SW_TEST_CASE( SharedAssetTableTest, ReloadLeavesSnapshotAndRefFollows )
{
    resetFakeLoader();
    sw::SharedAssetTable<FakeAsset> table;
    sw::shared_ptr<const FakeAsset> held;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/b.clip", &loadFake, held ) );
    const sw::SharedAssetRef<FakeAsset> ref{ held, table.findVersion( held ) };
    const uint32                        reloadsBefore = table.getReloadCount();

    s_bLoadFails = true;
    SW_EXPECT_FALSE( table.reloadShared( "game/x/b.clip", &loadFake ) );
    SW_EXPECT_TRUE( table.findLive( "game/x/b.clip" ).get() == held.get() ); // 읽지 못하면 표도 그대로
    SW_EXPECT_EQUAL( reloadsBefore, table.getReloadCount() );
    s_bLoadFails = false;

    SW_REQUIRE_TRUE( table.reloadShared( "game/x/b.clip", &loadFake ) );
    SW_EXPECT_EQUAL( 1, held->_generation ); // 스냅숏은 옛 객체 그대로
    SW_EXPECT_EQUAL( 2, ref->_generation );  // 따라가는 참조는 새 판
    SW_EXPECT_TRUE( table.findLive( "game/x/b.clip" ).get() == ref.get() );
    SW_EXPECT_TRUE( ref.getShared().get() == ref.get() );
    SW_EXPECT_EQUAL( reloadsBefore + 1, table.getReloadCount() );

    // 스냅숏을 놓아도 참조가 옛 판을 쥐어 새 판이 산다. 한 번 더 다시 읽으면 사슬 끝을 본다.
    held.reset();
    SW_REQUIRE_TRUE( table.reloadShared( "game/x/b.clip", &loadFake ) );
    SW_EXPECT_EQUAL( 3, ref->_generation );
    SW_EXPECT_EQUAL( size_t( 1 ), table.countLive() );

    SW_EXPECT_FALSE( table.reloadShared( "game/x/none.clip", &loadFake ) ); // 아무도 쥐지 않은 경로
}

/**
 * @brief [SharedAssetTableTest] 참조가 사슬 끝으로 옮기면 옛 판을 놓고, 모두 놓으면 표에서 사라진다
 */
SW_TEST_CASE( SharedAssetTableTest, RefFollowLatestReleasesOldVersions )
{
    resetFakeLoader();
    sw::SharedAssetTable<FakeAsset> table;
    sw::shared_ptr<const FakeAsset> first;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/d.clip", &loadFake, first ) );
    sw::SharedAssetRef<FakeAsset>       ref{ first, table.findVersion( first ) };
    const sw::weak_ptr<const FakeAsset> weakFirst = first;
    first.reset();
    SW_REQUIRE_TRUE( table.reloadShared( "game/x/d.clip", &loadFake ) );
    SW_EXPECT_FALSE( weakFirst.expired() ); // 참조가 머리를 쥔다
    ref.followLatest();
    SW_EXPECT_TRUE( weakFirst.expired() );
    SW_EXPECT_EQUAL( 2, ref->_generation );
    ref.reset();
    SW_EXPECT_EQUAL( size_t( 0 ), table.countLive() );
}

/**
 * @brief [SharedAssetTableTest] 워커가 쥔 스냅숏과 참조를 읽는 동안 게임 스레드가 다시 읽어도 스냅숏 내용은 바뀌지 않고 참조는 늘 온전한 판을 본다(TSan 이면 데이터 경쟁도 없다 — A-7)
 */
SW_TEST_CASE( SharedAssetTableTest, ReloadWhileWorkerReads )
{
    resetFakeLoader();
    sw::SharedAssetTable<FakeAsset> table;
    sw::shared_ptr<const FakeAsset> held;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/c.clip", &loadFake, held ) );
    const sw::SharedAssetRef<FakeAsset> ref{ held, table.findVersion( held ) };
    const int32                         seenGeneration = held->_generation;
    sw::atomic<bool>                    bStop{ false };
    sw::atomic<int32>                   changedCount{ 0 };
    sw::atomic<int32>                   backwardCount{ 0 };
    sw::thread                          reader{ sw::ThreadTestAccess::getKey(), [&]
    {
        int32 lastRefGeneration = 0;
        while ( bStop.load( std::memory_order_acquire ) == false )
        {
            if ( held->_generation != seenGeneration )
                changedCount.fetch_add( 1, std::memory_order_relaxed );
            const int32 refGeneration = ref->_generation;
            if ( refGeneration < lastRefGeneration )
                backwardCount.fetch_add( 1, std::memory_order_relaxed );
            lastRefGeneration = refGeneration;
        }
    } };
    for ( int32 i = 0; i < 64; ++i )
    {
        SW_REQUIRE_TRUE( table.reloadShared( "game/x/c.clip", &loadFake ) );
    }
    bStop.store( true, std::memory_order_release );
    reader.join();
    SW_EXPECT_EQUAL( 0, changedCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_EQUAL( 0, backwardCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_EQUAL( seenGeneration, held->_generation );
    SW_EXPECT_EQUAL( 65, ref->_generation );
}
