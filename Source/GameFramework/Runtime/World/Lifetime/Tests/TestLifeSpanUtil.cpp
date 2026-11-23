// LifeSpanUtilTest — 수명 계산의 경계(수명 이상에서 다함 · 수명 없음 · 흐림 1 → 0)를 본다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/World/Lifetime/LifeSpanUtil.h"

using namespace sw;

/** @brief [LifeSpanUtilTest] 흐른 시간이 수명에 닿는 걸음부터 다한 것이고 그 뒤로도 그렇다 */
SW_TEST_CASE( LifeSpanUtilTest, ExpiresAtLifeTimeAndStaysExpired )
{
    float32 elapsed = 0.0f;
    SW_EXPECT_FALSE( LifeSpanUtil::advance( elapsed, 1.0f, 0.5f ) );
    SW_EXPECT_NEAR( 0.5f, elapsed, 1.0e-6f );
    SW_EXPECT_TRUE( LifeSpanUtil::advance( elapsed, 1.0f, 0.5f ) );
    SW_EXPECT_TRUE( LifeSpanUtil::advance( elapsed, 1.0f, 0.0f ) );
}

/** @brief [LifeSpanUtilTest] 수명이 0 이하이면 시간은 흐르지만 끝나지 않고 흐려지지 않는다 */
SW_TEST_CASE( LifeSpanUtilTest, NoLifeTimeNeverExpires )
{
    float32 elapsed = 0.0f;
    SW_EXPECT_FALSE( LifeSpanUtil::advance( elapsed, 0.0f, 100.0f ) );
    SW_EXPECT_NEAR( 100.0f, elapsed, 1.0e-6f );
    SW_EXPECT_FALSE( LifeSpanUtil::hasExpired( elapsed, -1.0f ) );
    SW_EXPECT_NEAR( 1.0f, LifeSpanUtil::computeFade( elapsed, 0.0f ), 1.0e-6f );
}

/** @brief [LifeSpanUtilTest] 흐림은 1 에서 0 으로 고르게 줄고 다한 뒤는 0, 음수 시간은 1 이다 */
SW_TEST_CASE( LifeSpanUtilTest, FadeIsClamped )
{
    SW_EXPECT_NEAR( 1.0f, LifeSpanUtil::computeFade( 0.0f, 2.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.25f, LifeSpanUtil::computeFade( 1.5f, 2.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, LifeSpanUtil::computeFade( 5.0f, 2.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, LifeSpanUtil::computeFade( -1.0f, 2.0f ), 1.0e-6f );
}
