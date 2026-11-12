#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetPinSet.h"

// AssetPinSetTest — 미리 읽은 에셋 고정 세트: 예산을 넘으면 가장 오래 안 쓴 것부터 놓고(쓰기 · 다시 넣기가 순서를 새로 한다), 경로 철자는 정규화해 찾는다.
// 혼자 예산보다 큰 것은 붙들지 않는다. 토큰은 가짜 값이다(nogpu).

namespace
{
    sw::shared_ptr<const void> makePin()
    {
        return sw::make_shared<int32>( 0 );
    }
} // namespace

/**
 * @brief [AssetPinSetTest] 예산을 넘으면 가장 오래 안 쓴 것부터 놓는다 — `touch` 와 다시 넣기가 순서를 새로 한다
 */
SW_TEST_CASE( AssetPinSetTest, EvictsLeastRecentlyUsed )
{
    sw::AssetPinSet pins{ 30 };
    SW_REQUIRE_TRUE( pins.add( "game/a.mesh", makePin(), 10 ) );
    SW_REQUIRE_TRUE( pins.add( "game/b.mesh", makePin(), 10 ) );
    SW_REQUIRE_TRUE( pins.add( "game/c.mesh", makePin(), 10 ) );
    pins.touch( "game/a.mesh" );                                 // 순서: b, c, a
    SW_REQUIRE_TRUE( pins.add( "game/c.mesh", makePin(), 10 ) ); // 이미 든 것 — 순서만: b, a, c
    SW_REQUIRE_TRUE( pins.add( "game/d.mesh", makePin(), 15 ) ); // 45 > 30 → b, a 를 놓는다

    SW_EXPECT_FALSE( pins.contains( "game/b.mesh" ) );
    SW_EXPECT_FALSE( pins.contains( "game/a.mesh" ) );
    SW_EXPECT_TRUE( pins.contains( "game/c.mesh" ) );
    SW_EXPECT_TRUE( pins.contains( "game/d.mesh" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), pins.getCount() );
    SW_EXPECT_EQUAL( uint64( 25 ), pins.getPinnedBytes() );
}

/**
 * @brief [AssetPinSetTest] 빈 토큰과 혼자 예산보다 큰 것은 붙들지 않고, 놓기 · 비우기가 합을 맞춘다. 경로 철자는 정규화해 찾는다
 */
SW_TEST_CASE( AssetPinSetTest, RejectsAndRemoves )
{
    sw::AssetPinSet pins{ 20 };
    SW_EXPECT_FALSE( pins.add( "game/empty.mesh", nullptr, 1 ) );
    SW_EXPECT_FALSE( pins.add( "game/huge.mesh", makePin(), 21 ) );
    SW_EXPECT_EQUAL( size_t( 0 ), pins.getCount() );

    SW_REQUIRE_TRUE( pins.add( "game/x/a.mesh", makePin(), 5 ) );
    SW_REQUIRE_TRUE( pins.add( "game/x/b.mesh", makePin(), 7 ) );
    SW_EXPECT_TRUE( pins.contains( "game\\x\\a.mesh" ) );
    SW_EXPECT_TRUE( pins.remove( "game\\x\\a.mesh" ) );
    SW_EXPECT_FALSE( pins.remove( "game/x/a.mesh" ) );
    SW_EXPECT_EQUAL( uint64( 7 ), pins.getPinnedBytes() );

    pins.clear();
    SW_EXPECT_EQUAL( size_t( 0 ), pins.getCount() );
    SW_EXPECT_EQUAL( uint64( 0 ), pins.getPinnedBytes() );
    SW_REQUIRE_TRUE( pins.add( "game/x/b.mesh", makePin(), 7 ) ); // 비운 뒤 다시 넣을 수 있다
}
