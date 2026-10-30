#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cache/SharedAssetTable.h"

// SharedAssetTableTest — 경로로 나눠 주는 에셋 표(스켈레톤 · 클립 · 리그 · 캐릭터 데이터가 쓴다): 같은 경로는 같은 객체, 마지막 사용자가 놓으면 사라짐,
// 제자리 다시 읽기(쥔 쪽이 새 내용을 본다)와 떼어 내기(쥔 쪽은 옛 객체 그대로), 읽기 실패는 옛 내용을 지킨다. 로드 함수는 가짜다(nogpu).

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
 * @brief [SharedAssetTableTest] 제자리 다시 읽기는 쥔 쪽이 새 내용을 보고, 떼어 내기는 쥔 쪽의 옛 객체를 그대로 둔다. 읽기 실패는 옛 내용을 지킨다
 */
SW_TEST_CASE( SharedAssetTableTest, ReloadInPlaceOrDetach )
{
    resetFakeLoader();
    sw::SharedAssetTable<FakeAsset> table;
    sw::shared_ptr<const FakeAsset> held;
    SW_REQUIRE_TRUE( table.loadAndIntern( "game/x/b.clip", &loadFake, held ) );
    const uint32 reloadsBefore = table.getReloadCount();

    SW_REQUIRE_TRUE( table.reloadShared( "game/x/b.clip", &loadFake ) );
    SW_EXPECT_EQUAL( 2, held->_generation ); // 같은 객체가 새 내용이다
    SW_EXPECT_EQUAL( reloadsBefore + 1, table.getReloadCount() );

    s_bLoadFails = true;
    SW_EXPECT_FALSE( table.reloadShared( "game/x/b.clip", &loadFake ) );
    SW_EXPECT_EQUAL( 2, held->_generation );
    SW_EXPECT_EQUAL( reloadsBefore + 1, table.getReloadCount() );
    s_bLoadFails = false;

    SW_REQUIRE_TRUE( table.detachShared( "game/x/b.clip", &loadFake ) );
    SW_EXPECT_EQUAL( 2, held->_generation ); // 쥔 쪽은 옛 객체 그대로
    SW_EXPECT_TRUE( table.findLive( "game/x/b.clip" ) == nullptr );
    SW_EXPECT_EQUAL( reloadsBefore + 2, table.getReloadCount() );

    SW_EXPECT_FALSE( table.reloadShared( "game/x/none.clip", &loadFake ) ); // 아무도 쥐지 않은 경로
}
