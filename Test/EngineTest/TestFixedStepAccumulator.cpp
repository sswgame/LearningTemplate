#include "pch.h"

#include "Engine/Physics/FixedStepAccumulator.h"

#include "TestFramework/TestFramework.h"

// 가변 프레임 시간 → 고정 스텝 수 · 보간 비 · 상한(나선 방지).

/**
 * @brief [FixedStepAccumulatorTest] 쌓인 시간만큼 고정 스텝을 돌리고, 남은 시간이 보간 비가 된다
 */
SW_TEST_CASE( FixedStepAccumulatorTest, StepsAndAlpha )
{
    sw::FixedStepAccumulator accumulator;
    accumulator.configure( 0.01f, 8 );
    SW_EXPECT_EQUAL( 0u, accumulator.advance( 0.005f ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, accumulator.getAlpha(), 1e-4f );
    SW_EXPECT_EQUAL( 1u, accumulator.advance( 0.0075f ) ); // 0.0125 → 한 스텝, 0.0025 남음
    SW_EXPECT_NEAR_EQUAL( 0.25f, accumulator.getAlpha(), 1e-4f );
    SW_EXPECT_EQUAL( 3u, accumulator.advance( 0.03f ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, accumulator.getAlpha(), 1e-4f );
    // 60 Hz 프레임을 60 Hz 스텝으로 — 끝의 반올림 오차가 스텝 하나를 다음 프레임으로 밀지 않는다.
    sw::FixedStepAccumulator sixty;
    uint32                   totalSteps = 0;
    for ( uint32 frameIndex = 0; frameIndex < 600; ++frameIndex )
        totalSteps += sixty.advance( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 600u, totalSteps );
}

/**
 * @brief [FixedStepAccumulatorTest] 긴 프레임은 상한까지만 돌리고 나머지를 버린다 — 다음 프레임이 따라잡느라 길어지지 않는다
 */
SW_TEST_CASE( FixedStepAccumulatorTest, LongFrameIsClamped )
{
    sw::FixedStepAccumulator accumulator;
    accumulator.configure( 0.01f, 4 );
    SW_EXPECT_EQUAL( 4u, accumulator.advance( 1.0f ) );
    SW_EXPECT_TRUE( accumulator.getAlpha() < 1.0f );
    SW_EXPECT_TRUE( accumulator.getDroppedTime() > 0.9 );
    SW_EXPECT_EQUAL( 1u, accumulator.advance( 0.01f ) ); // 다음 프레임은 평소대로
    // 음수 · NaN 은 시간으로 치지 않는다.
    SW_EXPECT_EQUAL( 0u, accumulator.advance( -1.0f ) );
}
