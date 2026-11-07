#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Utility/DepletingValue.h"

using namespace sw;

/** @brief [DepletingValue] 값보다 적게 깎으면 그 양만 줄고 소진이 아니다. */
SW_TEST_CASE( DepletingValue, TakesLessThanValue )
{
    int32      hp     = 10;
    const auto result = DepletingValue::take( hp, 3 );
    SW_EXPECT_EQUAL( 7, hp );
    SW_EXPECT_EQUAL( 3, result._taken );
    SW_EXPECT_TRUE( result._bDepleted == SW_FALSE );
}

/** @brief [DepletingValue] 넘치면 남은 만큼만 깎고 0 에서 멈춘다 — 실제 깎은 양을 돌려준다. */
SW_TEST_CASE( DepletingValue, OverkillStopsAtZero )
{
    float32    health = 2.5f;
    const auto result = DepletingValue::take( health, 10.0f );
    SW_EXPECT_NEAR( 0.0f, health, 1.0e-6f );
    SW_EXPECT_NEAR( 2.5f, result._taken, 1.0e-6f );
    SW_EXPECT_TRUE( result._bDepleted == SW_TRUE );
}

/** @brief [DepletingValue] 음수 양은 회복이 아니다. */
SW_TEST_CASE( DepletingValue, NegativeAmountTakesNothing )
{
    int32      hp     = 5;
    const auto result = DepletingValue::take( hp, -4 );
    SW_EXPECT_EQUAL( 5, hp );
    SW_EXPECT_EQUAL( 0, result._taken );
    SW_EXPECT_TRUE( result._bDepleted == SW_FALSE );
}
