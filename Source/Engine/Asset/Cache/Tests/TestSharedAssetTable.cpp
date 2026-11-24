#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
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

    struct FakeListAsset
    {
        sw::vector<uint8>                   _listByte;
        sw::shared_ptr<const FakeListAsset> _child;
    };

    constexpr size_t kFakeListBytes = 64u * 1024u;

    bool loadFakeList( const sw::AssetID&, FakeListAsset& outAsset )
    {
        const sw::vector<uint8> temporary( kFakeListBytes * 4u ); // 읽고 놓는 임시 버퍼는 세지 않는다
        outAsset._listByte.resize( kFakeListBytes );
        return temporary.empty() == false;
    }

    /** @brief 안에서 다른 에셋(같은 표의 "child")을 읽는 로더입니다. */
    sw::SharedAssetTable<FakeListAsset>* s_pListTable = nullptr;

    bool loadFakeParent( const sw::AssetID& assetID, FakeListAsset& outAsset )
    {
        return s_pListTable->loadAndIntern( "game/x/child.skeleton.xml", &loadFakeList, outAsset._child ) && loadFakeList( assetID, outAsset );
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
 * @brief [SharedAssetTableTest] 에셋마다 읽으며 남긴 바이트를 든다 — 안쪽 컨테이너는 세고, 놓은 임시 버퍼와 안에서 읽은 다른 에셋은 세지 않는다
 * @details 변이: 재는 구간이 안쪽 구간을 빼지 않으면 부모의 값이 두 벌이 되어 진다.
 */
SW_TEST_CASE( SharedAssetTableTest, LoadedBytesCountOwnContainersOnce )
{
    sw::SharedAssetTable<FakeListAsset> table;
    s_pListTable = &table;
    sw::shared_ptr<const FakeListAsset> parent;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/parent.skeleton.xml", &loadFakeParent, parent ) );
    s_pListTable = nullptr;
    if ( sw::kMemoryTagScopesEnabled == false )
        return;
    for ( const utf8* pKey : { "game/x/parent.skeleton.xml", "game/x/child.skeleton.xml" } )
    {
        const uint64 bytes = table.findLoadedBytes( pKey );
        SW_EXPECT_TRUE( bytes >= kFakeListBytes );
        SW_EXPECT_TRUE( bytes < kFakeListBytes * 2u );
    }
    SW_EXPECT_EQUAL( uint64{ 0 }, table.findLoadedBytes( "game/x/unknown.skeleton.xml" ) );
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
    const sw::SharedAssetRef<FakeAsset> ref{ held, table.findVersion( held.get() ) };
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
    SW_EXPECT_TRUE( ref.snapshot().get() == ref.get() );
    SW_EXPECT_EQUAL( reloadsBefore + 1, table.getReloadCount() );

    // 스냅숏을 놓아도 참조가 옛 판을 쥐어 새 판이 산다. 한 번 더 다시 읽으면 사슬 끝을 본다.
    held.reset();
    SW_REQUIRE_TRUE( table.reloadShared( "game/x/b.clip", &loadFake ) );
    SW_EXPECT_EQUAL( 3, ref->_generation );
    SW_EXPECT_EQUAL( size_t( 1 ), table.countLive() );

    SW_EXPECT_FALSE( table.reloadShared( "game/x/none.clip", &loadFake ) ); // 아무도 쥐지 않은 경로
}

/**
 * @brief [SharedAssetTableTest] 여러 번 다시 읽어도 사슬 정리(`followRefs`)가 참조를 최신 판으로 옮기고, 프레임 경계 두 번(`collectRetired`) 뒤 살아 있는 판은 하나로 줄어든다
 */
SW_TEST_CASE( SharedAssetTableTest, ReloadsCollapseToOneVersion )
{
    resetFakeLoader();
    sw::SharedAssetTable<FakeAsset> table;
    sw::shared_ptr<const FakeAsset> first;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/d.clip", &loadFake, first ) );
    const sw::SharedAssetRef<FakeAsset> ref{ first, table.findVersion( first.get() ) };
    const sw::SharedAssetRef<FakeAsset> copy      = ref; // 복사도 목록에 오른다
    const sw::weak_ptr<const FakeAsset> weakFirst = first;
    first.reset();
    for ( int32 i = 0; i < 5; ++i )
    {
        SW_REQUIRE_TRUE( table.reloadShared( "game/x/d.clip", &loadFake ) );
        SW_EXPECT_EQUAL( uint32( 2 ), table.followRefs() ); // 다시 읽은 직후 — 참조 둘을 옮긴다
        SW_EXPECT_EQUAL( 2 + i, ref->_generation );
    }
    SW_EXPECT_FALSE( weakFirst.expired() ); // 옮기며 놓은 옛 판은 프레임 경계 두 번까지 남는다
    SW_EXPECT_TRUE( table.countVersions() > size_t( 1 ) );
    (void)table.collectRetired();
    (void)table.collectRetired();
    SW_EXPECT_TRUE( weakFirst.expired() );
    SW_EXPECT_EQUAL( size_t( 1 ), table.countVersions() );
    SW_EXPECT_EQUAL( 6, ref->_generation );
    SW_EXPECT_EQUAL( 6, copy->_generation );
    SW_EXPECT_EQUAL( uint32( 0 ), table.followRefs() ); // 이미 최신이다
}

/**
 * @brief [SharedAssetTableTest] 참조를 모두 놓으면 표에서 사라지고, 빌린 참조(`borrow`)는 따라가지 않는다
 */
SW_TEST_CASE( SharedAssetTableTest, RefReleaseAndBorrow )
{
    resetFakeLoader();
    sw::SharedAssetTable<FakeAsset> table;
    sw::shared_ptr<const FakeAsset> first;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/e.clip", &loadFake, first ) );
    sw::SharedAssetRef<FakeAsset> ref{ first, table.findVersion( first.get() ) };
    first.reset();
    SW_REQUIRE_TRUE( table.reloadShared( "game/x/e.clip", &loadFake ) );
    ref.reset();
    (void)table.collectRetired();
    (void)table.collectRetired();
    SW_EXPECT_EQUAL( size_t( 0 ), table.countLive() );
    SW_EXPECT_EQUAL( size_t( 0 ), table.countVersions() );

    const FakeAsset                     local{ 42 };
    const sw::SharedAssetRef<FakeAsset> borrowed = sw::SharedAssetRef<FakeAsset>::borrow( &local );
    SW_EXPECT_TRUE( borrowed.get() == &local );
    SW_EXPECT_TRUE( borrowed.snapshot().get() == &local );  // 스냅숏을 쥐는 쪽(워커 일감)도 빌린 값을 읽는다
    SW_EXPECT_EQUAL( 0L, borrowed.snapshot().use_count() ); // 소유하지 않는다 — 수명은 빌려준 쪽이 지킨다
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
    const sw::SharedAssetRef<FakeAsset> ref{ held, table.findVersion( held.get() ) };
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
        // 다시 읽기와 사슬 정리는 참조를 쓴다(원자) — 워커가 그 참조를 읽는 중이어도 된다. 옛 판은 프레임 경계 전에는 놓지 않는다.
        SW_REQUIRE_TRUE( table.reloadShared( "game/x/c.clip", &loadFake ) );
        (void)table.followRefs();
    }
    bStop.store( true, std::memory_order_release );
    reader.join();
    SW_EXPECT_EQUAL( 0, changedCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_EQUAL( 0, backwardCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_EQUAL( seenGeneration, held->_generation );
    SW_EXPECT_EQUAL( 65, ref->_generation );
}
