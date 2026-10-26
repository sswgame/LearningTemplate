// FixedStepTimer — 쌓인 시간만큼 고정 스텝을 내고 남은 시간이 보간 비가 되는지, 긴 프레임을 상한에서 자르는지, 스텝을 나눠도 반올림으로 밀리지 않는지.
#include "pch.h"

#include "Core/Time/FixedStepTimer.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( FixedStepTimerTest, StepsAndAlpha )
{
    FixedStepTimer timer( 0.01f, 0.08f );
    SW_EXPECT_EQUAL( 0, timer.consume( 0.005f ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, timer.getAlpha(), 1e-4f );
    SW_EXPECT_EQUAL( 1, timer.consume( 0.0075f ) ); // 0.0125 → 한 스텝, 0.0025 남음
    SW_EXPECT_NEAR_EQUAL( 0.25f, timer.getAlpha(), 1e-3f );
    SW_EXPECT_EQUAL( 3, timer.consume( 0.03f ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, timer.getAlpha(), 1e-3f );

    // 60 Hz 프레임을 60 Hz 스텝으로, 1/60 초를 1/180 초 스텝 셋으로 — 반올림 오차가 스텝 하나를 다음 프레임으로 밀지 않는다.
    FixedStepTimer sixty( 1.0f / 60.0f, 0.25f );
    FixedStepTimer subStep( 1.0f / 60.0f / 3.0f, 0.25f );
    int32          sixtySteps = 0;
    for ( int32 frameIndex = 0; frameIndex < 600; ++frameIndex )
    {
        sixtySteps += sixty.consume( 1.0f / 60.0f );
        SW_EXPECT_EQUAL( 3, subStep.consume( 1.0f / 60.0f ) );
    }
    SW_EXPECT_EQUAL( 600, sixtySteps );
}

SW_TEST_CASE( FixedStepTimerTest, LongFrameIsClampedAndBadDeltaIgnored )
{
    FixedStepTimer timer( 0.01f, 0.04f ); // 프레임당 4 스텝
    SW_EXPECT_EQUAL( 4, timer.consume( 1.0f ) );
    SW_EXPECT_TRUE( timer.getAlpha() < 1.0f );
    SW_EXPECT_EQUAL( 1, timer.consume( 0.01f ) ); // 다음 프레임은 평소대로 — 따라잡지 않는다

    SW_EXPECT_EQUAL( 0, timer.consume( -1.0f ) );
    const float32 notANumber = std::numeric_limits<float32>::quiet_NaN();
    SW_EXPECT_EQUAL( 0, timer.consume( notANumber ) );
    SW_EXPECT_TRUE( timer.getAlpha() < 1.0f ); // NaN 이 누적기에 들어가지 않았다
}
