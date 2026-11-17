#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Scene/SiblingOrderKey.h"

#include <limits>

// SiblingOrderKeyTest — 형제 순서 분수 색인 키: 사이 고르기 · 틈이 다 찬 경우 · 끝에 더하기 · 다시 펴기(설계 E-6).

/**
 * @brief [SiblingOrderKeyTest] 앞 · 뒤 키 사이의 키는 둘 사이 가운데다 — 앞에 더하기도 0 아래로 내려가지 않는다
 */
SW_TEST_CASE( SiblingOrderKeyTest, BetweenBisects )
{
    using Key                         = sw::editor::SiblingOrderKey;
    const sw::optional<uint32> middle = Key::between( 100u, 200u );
    SW_REQUIRE_TRUE( middle.has_value() );
    SW_EXPECT_EQUAL( *middle, 150u );
    const sw::optional<uint32> front = Key::between( sw::nullopt, 3u );
    SW_REQUIRE_TRUE( front.has_value() );
    SW_EXPECT_TRUE( *front < 3u );
    const sw::optional<uint32> farFront = Key::between( sw::nullopt, Key::kAppendStep * 3 );
    SW_REQUIRE_TRUE( farFront.has_value() );
    SW_EXPECT_EQUAL( *farFront, Key::kAppendStep * 2 );
}

/**
 * @brief [SiblingOrderKeyTest] 틈이 없으면(이웃 키 · 같은 키 · 0 앞 · `uint32` 끝) 없음이다 — 부르는 쪽이 그 부모의 형제만 다시 편다
 */
SW_TEST_CASE( SiblingOrderKeyTest, ExhaustedGapAsksRespace )
{
    using Key = sw::editor::SiblingOrderKey;
    SW_EXPECT_FALSE( Key::between( 7u, 8u ).has_value() );
    SW_EXPECT_FALSE( Key::between( 0u, 0u ).has_value() ); // 옛 씬의 같은 번호
    SW_EXPECT_FALSE( Key::between( 9u, 4u ).has_value() );
    SW_EXPECT_FALSE( Key::between( sw::nullopt, 0u ).has_value() );
    SW_EXPECT_FALSE( Key::between( std::numeric_limits<uint32>::max(), sw::nullopt ).has_value() );

    sw::vector<uint32> listKey;
    Key::spread( 5u, listKey );
    SW_REQUIRE_EQUAL( static_cast<uint32>( listKey.size() ), 5u );
    for ( size_t index = 1; index < listKey.size(); ++index )
        SW_EXPECT_TRUE( listKey[index] - listKey[index - 1] >= 2u ); // 다시 편 뒤에는 이웃 사이마다 틈이 있다
    SW_EXPECT_TRUE( listKey.front() > 0u );                          // 맨 앞에도 넣을 틈이 있다
}

/**
 * @brief [SiblingOrderKeyTest] 끝에 더하기는 `kAppendStep` 간격이다 — 첫 키도 같은 간격이고 `uint32` 끝 가까이에서는 남은 틈의 가운데로 줄인다
 */
SW_TEST_CASE( SiblingOrderKeyTest, AppendKeepsStep )
{
    using Key = sw::editor::SiblingOrderKey;
    SW_EXPECT_EQUAL( *Key::between( sw::nullopt, sw::nullopt ), Key::kAppendStep );
    SW_EXPECT_EQUAL( *Key::between( Key::kAppendStep, sw::nullopt ), Key::kAppendStep * 2 );
    constexpr uint32           kNearEnd = std::numeric_limits<uint32>::max() - 10u;
    const sw::optional<uint32> tail     = Key::between( kNearEnd, sw::nullopt );
    SW_REQUIRE_TRUE( tail.has_value() );
    SW_EXPECT_TRUE( *tail > kNearEnd );
}
