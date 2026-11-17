// AutomationReadinessTest — 붙잡는 조건의 세 값(준비됨 · 해당 없음 · 진행 중)과 정지 시계(진척이 그대로인 횟수 — 정지 시한 `_stallFrames` 와 견준다).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Scenario/AutomationReadiness.h"

using namespace sw;

/** @brief [AutomationReadinessTest] 해당 없음은 진행 중이 아니다 — 막지 않는다(TAA 없는 Forward 뷰가 시한까지 기다리던 실패 1) */
SW_TEST_CASE( AutomationReadinessTest, NotApplicableDoesNotWait )
{
    SW_EXPECT_FALSE( AutomationReadiness::notApplicable().isWaiting() );
    SW_EXPECT_TRUE( AutomationReadiness::notApplicable().isNotApplicable() );
    SW_EXPECT_FALSE( AutomationReadiness::ready().isWaiting() );
    SW_EXPECT_FALSE( AutomationReadiness::ready().isNotApplicable() );
    const AutomationReadiness waiting = AutomationReadiness::waiting( 7, "the camera moves" );
    SW_EXPECT_TRUE( waiting.isWaiting() );
    SW_EXPECT_EQUAL( 7u, static_cast<uint32>( waiting.getProgress() ) );
    SW_EXPECT_EQUAL( string( "the camera moves" ), string( waiting.getWhat() ) );
}

/**
 * @brief [AutomationReadinessTest] 진척 없는 진행 중은 정지 시계가 매번 하나씩 늘고, 진척 · 조건이 바뀌거나 기다릴 것이 없어지면 0 부터다
 * @details 실행기는 이 수가 `_stallFrames` 에 닿으면 그 단계를 `WaitStalled` 로 끝낸다. 변이: 진척을 보지 않으면 둘째 단언이, 조건 이름을 보지 않으면 셋째 단언이 진다.
 */
SW_TEST_CASE( AutomationReadinessTest, StallClockCountsUnchangedProgress )
{
    static constexpr const utf8* kTemporal = "RenderTemporalAccumulation";
    static constexpr const utf8* kExposure = "RenderAutoExposure";
    AutomationStallClock         clock;
    SW_EXPECT_EQUAL( 0u, clock.advance( kTemporal, 0 ) );
    SW_EXPECT_EQUAL( 1u, clock.advance( kTemporal, 0 ) );
    SW_EXPECT_EQUAL( 2u, clock.advance( kTemporal, 0 ) ); // 카메라가 계속 움직인다 — 정지 프레임이 0 에 머문다
    SW_EXPECT_EQUAL( 0u, clock.advance( kTemporal, 1 ) ); // 나아갔다
    SW_EXPECT_EQUAL( 0u, clock.advance( kExposure, 1 ) ); // 다른 조건을 기다린다
    SW_EXPECT_EQUAL( 1u, clock.advance( kExposure, 1 ) );
    SW_EXPECT_EQUAL( 0u, clock.advance( nullptr, 0 ) ); // 기다릴 것이 없다
    SW_EXPECT_EQUAL( 0u, clock.getUnchangedCount() );
}
