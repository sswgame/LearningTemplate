// RenderStabilityTest — 사진 앞 정착 판정(`RenderStabilityUtil`): 정지 프레임은 기록을 버리거나 움직이면 0 부터, 노출 오차 상한은 기록을 버리면 0 · 움직이면 범위 끝 · 정지면 지수로 준다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Frame/View/RenderStability.h"

using namespace sw;

/** @brief [RenderStabilityTest] 정지 프레임은 TAA 기록을 버린 프레임 · 카메라가 움직인 프레임 · 물체가 움직인 프레임에서 0 이 되고, 아니면 하나씩 는다 */
SW_TEST_CASE( RenderStabilityTest, StillFramesRestartOnResetOrMotion )
{
    uint32 count = 0;
    count        = RenderStabilityUtil::advanceStillFrames( count, true, true, false ); // 첫 프레임(기록 버림)
    SW_EXPECT_EQUAL( 0u, count );
    count = RenderStabilityUtil::advanceStillFrames( count, false, true, false );
    count = RenderStabilityUtil::advanceStillFrames( count, false, true, false );
    SW_EXPECT_EQUAL( 2u, count );
    SW_EXPECT_EQUAL( 0u, RenderStabilityUtil::advanceStillFrames( count, false, false, false ) ); // 카메라가 움직였다
    SW_EXPECT_EQUAL( 0u, RenderStabilityUtil::advanceStillFrames( count, false, true, true ) );   // 물체가 움직였다
}

/** @brief [RenderStabilityTest] 시간 누적 리셋은 뷰 기록을 버렸거나, 이번 프레임에 TAA 를 돌리는데 그 기록이 버려졌을 때뿐이다 — TAA 없는 뷰는 TAA 기록 버림을 보지 않는다 */
SW_TEST_CASE( RenderStabilityTest, AccumulationResetIgnoresTAAWhenTheViewHasNone )
{
    SW_EXPECT_FALSE( RenderStabilityUtil::isAccumulationReset( false, false, true ) ); // Forward 뷰 — TAA 기록은 늘 "버림"
    SW_EXPECT_TRUE( RenderStabilityUtil::isAccumulationReset( false, true, true ) );   // TAA 를 돌리는데 기록을 버렸다
    SW_EXPECT_TRUE( RenderStabilityUtil::isAccumulationReset( true, false, false ) );  // 컷 · 첫 프레임
    SW_EXPECT_FALSE( RenderStabilityUtil::isAccumulationReset( false, true, false ) );
}

/**
 * @brief [RenderStabilityTest] 노출 오차 상한 — 기록을 버린 프레임은 측정값을 그대로 써 0, 카메라가 움직인 프레임은 범위 끝까지, 정지 프레임마다 (1 − 다가간 비율) 을 곱한다.
 *        고정 노출(범위 0)은 늘 0 이다.
 */
SW_TEST_CASE( RenderStabilityTest, ExposureErrorDecaysOnlyWhileStill )
{
    constexpr float32 kRange = 4.0f;
    SW_EXPECT_NEAR( 0.0f, RenderStabilityUtil::advanceExposureError( 3.0f, true, false, kRange, 0.1f ), 0.0f );
    float32 error = RenderStabilityUtil::advanceExposureError( 0.0f, false, true, kRange, 0.5f );
    SW_EXPECT_NEAR( 2.0f, error, 1.0e-6f ); // 범위 끝(4)에서 이번 프레임에 절반 다가갔다
    error = RenderStabilityUtil::advanceExposureError( error, false, false, kRange, 0.5f );
    SW_EXPECT_NEAR( 1.0f, error, 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, RenderStabilityUtil::advanceExposureError( 2.0f, false, true, 0.0f, 0.5f ), 0.0f );
}

/** @brief [RenderStabilityTest] 정착은 정지 프레임 · 노출 오차 · 남은 에셋 일이 모두 문턱 안일 때만이다 */
SW_TEST_CASE( RenderStabilityTest, StableNeedsEveryCondition )
{
    const RenderStabilityThreshold threshold{ 16, 0.05f };
    RenderStabilitySample          sample;
    sample._stillFrameCount = 16;
    sample._exposureErrorEv = 0.04f;
    SW_EXPECT_TRUE( RenderStabilityUtil::isStable( sample, 0, threshold ) );
    SW_EXPECT_FALSE( RenderStabilityUtil::isStable( sample, 1, threshold ) ); // 에셋 일이 남았다
    sample._stillFrameCount = 15;
    SW_EXPECT_FALSE( RenderStabilityUtil::isStable( sample, 0, threshold ) );
    sample._stillFrameCount = 16;
    sample._exposureErrorEv = 0.06f;
    SW_EXPECT_FALSE( RenderStabilityUtil::isStable( sample, 0, threshold ) );
}

/**
 * @brief [RenderStabilityTest] 물체 움직임을 보지 않는 정착은 카메라 정지 프레임 · 되읽은 노출 적응 · 에셋 일만 본다 — 늘 움직이는 캐릭터 장면도 선다
 * @details 변이: 정지 프레임(물체 포함)을 보면 첫 단언이 진다. 에셋 일을 빼면 셋째가 진다.
 */
SW_TEST_CASE( RenderStabilityTest, StableIgnoringMotionLooksAtTheCameraOnly )
{
    const RenderStabilityThreshold threshold{ 16, 0.05f };
    RenderStabilitySample          sample{};
    sample._stillFrameCount       = 0; // 캐릭터가 숨 쉰다
    sample._exposureErrorEv       = 8.0f;
    sample._cameraStillFrameCount = 16;
    sample._exposureAdaptationEv  = 0.01f;
    SW_EXPECT_TRUE( RenderStabilityUtil::isStableIgnoringMotion( sample, 0, threshold ) );
    SW_EXPECT_FALSE( RenderStabilityUtil::isStable( sample, 0, threshold ) );
    SW_EXPECT_FALSE( RenderStabilityUtil::isStableIgnoringMotion( sample, 1, threshold ) );
    sample._exposureAdaptationEv = 0.5f;
    SW_EXPECT_FALSE( RenderStabilityUtil::isStableIgnoringMotion( sample, 0, threshold ) );
}
