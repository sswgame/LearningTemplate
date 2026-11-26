// RenderStabilityTest — 사진 앞 정착(`RenderStabilityUtil`): 정지 프레임은 기록을 버리거나 움직이면 0 부터, 노출 오차 상한은 기록을 버리면 0 · 움직이면 범위 끝 · 정지면 지수로 준다. 정착 조건은 세 값이다.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

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

/**
 * @brief [RenderStabilityTest] TAA 조건은 카메라 정지 프레임만 본다 — 캐릭터가 늘 숨 쉬는 장면(물체 정지 프레임 0)도 서고, TAA 를 돌리지 않는 뷰는 해당 없음이다
 * @details 변이: 물체까지 본 정지 프레임(`_stillFrameCount`)을 보면 둘째 단언이 진다. TAA 를 안 돌리는 뷰를 진행 중으로 두면 첫 단언이 진다(Forward 뷰가 시한까지 기다렸다 — 실패 1).
 */
SW_TEST_CASE( RenderStabilityTest, TemporalAccumulationLooksAtTheCameraOnly )
{
    RenderStabilitySample sample{};
    sample._bTemporalAccumulation = SW_FALSE;
    SW_EXPECT_TRUE( sample.evaluateTemporalAccumulation( 16 ).isNotApplicable() );
    sample._bTemporalAccumulation   = SW_TRUE;
    sample._stillFrameCount         = 0; // 캐릭터가 숨 쉰다
    sample._cameraStillFrameCount   = 16;
    const AutomationReadiness ready = sample.evaluateTemporalAccumulation( 16 );
    SW_EXPECT_FALSE( ready.isWaiting() );
    SW_EXPECT_FALSE( ready.isNotApplicable() );
    sample._cameraStillFrameCount     = 5;
    const AutomationReadiness waiting = sample.evaluateTemporalAccumulation( 16 );
    SW_EXPECT_TRUE( waiting.isWaiting() );
    SW_EXPECT_EQUAL( 5ull, static_cast<unsigned long long>( waiting.getProgress() ) ); // 진척은 쌓인 정지 프레임 수 — 카메라가 계속 움직이면 0 에 머문다
}

/** @brief [RenderStabilityTest] 실시간 GI 조건은 실시간 볼륨이 없으면 해당 없음, 오차가 문턱 이하면 준비됨, 위면 진행 중이고 프레임이 쌓이면 진척이 바뀐다 */
SW_TEST_CASE( RenderStabilityTest, RealtimeGIWaitsForTheHistory )
{
    RenderStabilitySample sample{};
    sample._giError = 0.5f;
    SW_EXPECT_TRUE( sample.evaluateRealtimeGI( 0.0002f ).isNotApplicable() );
    sample._bRealtimeGI             = SW_TRUE;
    sample._giAccumulatedFrames     = 8;
    const AutomationReadiness early = sample.evaluateRealtimeGI( 0.0002f );
    sample._giAccumulatedFrames     = 9; // 갱신 주기 안에서는 오차가 그대로여도 프레임은 쌓인다 — 정지로 보지 않는다
    const AutomationReadiness later = sample.evaluateRealtimeGI( 0.0002f );
    SW_EXPECT_TRUE( early.isWaiting() );
    SW_EXPECT_TRUE( later.isWaiting() );
    SW_EXPECT_TRUE( early.getProgress() != later.getProgress() );
    sample._giError = 0.0002f;
    SW_EXPECT_FALSE( sample.evaluateRealtimeGI( 0.0002f ).isWaiting() );
}

/** @brief [RenderStabilityTest] 노출 조건은 재지 않는 뷰에서 해당 없음, 오차 상한이 문턱 이하면 준비됨, 위면 진행 중이고 상한이 줄면 진척이 바뀐다 */
SW_TEST_CASE( RenderStabilityTest, AutoExposureWaitsForTheErrorBound )
{
    RenderStabilitySample sample{};
    sample._exposureErrorEv = 8.0f;
    SW_EXPECT_TRUE( sample.evaluateAutoExposure( 0.05f ).isNotApplicable() );
    sample._bAutoExposure                  = SW_TRUE;
    const AutomationReadiness wideError    = sample.evaluateAutoExposure( 0.05f );
    sample._exposureErrorEv                = 4.0f;
    const AutomationReadiness smallerError = sample.evaluateAutoExposure( 0.05f );
    SW_EXPECT_TRUE( wideError.isWaiting() );
    SW_EXPECT_TRUE( smallerError.isWaiting() );
    SW_EXPECT_TRUE( wideError.getProgress() != smallerError.getProgress() );
    sample._exposureErrorEv = 0.04f;
    SW_EXPECT_FALSE( sample.evaluateAutoExposure( 0.05f ).isWaiting() );
}
