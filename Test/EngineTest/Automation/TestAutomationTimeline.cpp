#include "pch.h"

#include "Engine/Scenario/AutomationTimeline.h"

#include "TestFramework/TestFramework.h"

// 시나리오 벽시계 타임라인 — 순수 값 모으기라 시계를 읽지 않고 초를 직접 넣어 시험한다. 실행기가 프레임 종류를 고르는 규칙은 `AutomationRunnerWatchTest` 가 보고로 본다.

/**
 * @brief [AutomationTimelineTest] 프레임은 종류별로 개수와 초가 따로 쌓인다
 */
SW_TEST_CASE( AutomationTimelineTest, SlicesAccumulatePerKind )
{
    sw::AutomationTimeline timeline;
    timeline.recordFrame( sw::AutomationFrameKind::ScheduledWait, 0.25 );
    timeline.recordFrame( sw::AutomationFrameKind::ScheduledWait, 0.5 );
    timeline.recordFrame( sw::AutomationFrameKind::StepRan, 1.0 );
    SW_EXPECT_EQUAL( 2u, timeline.getSlice( sw::AutomationFrameKind::ScheduledWait )._frameCount );
    SW_EXPECT_NEAR_EQUAL( 0.75, timeline.getSlice( sw::AutomationFrameKind::ScheduledWait )._seconds, 1.0e-9 );
    SW_EXPECT_EQUAL( 1u, timeline.getSlice( sw::AutomationFrameKind::StepRan )._frameCount );
    SW_EXPECT_EQUAL( 0u, timeline.getSlice( sw::AutomationFrameKind::ConditionHold )._frameCount );
}

/**
 * @brief [AutomationTimelineTest] 단계 앞 대기는 고정 대기와 조건 대기를 합쳐 그 단계가 돌 때 닫히고, 단계가 돈 프레임은 대기에 들지 않는다
 */
SW_TEST_CASE( AutomationTimelineTest, WaitClosesOnTheNextStepAndExcludesStepFrames )
{
    sw::AutomationTimeline timeline;
    timeline.recordFrame( sw::AutomationFrameKind::ScheduledWait, 0.5 );
    timeline.recordFrame( sw::AutomationFrameKind::ConditionHold, 1.5 );
    timeline.recordFrame( sw::AutomationFrameKind::StepRan, 9.0 ); // 단계가 돈 프레임 — 대기가 아니다
    timeline.closeWait( "Expect", 7, 120 );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( timeline.getSlowestWaits().size() ) );
    const sw::AutomationSlowWait& wait = timeline.getSlowestWaits()[0];
    SW_EXPECT_STREQ( "Expect", wait._stepKind.c_str() );
    SW_EXPECT_EQUAL( 7u, wait._stepNumber );
    SW_EXPECT_EQUAL( 120u, wait._frameIndex );
    SW_EXPECT_EQUAL( 2u, wait._waitedFrameCount );
    SW_EXPECT_NEAR_EQUAL( 2.0, wait._seconds, 1.0e-9 );

    timeline.closeWait( "Pass", 8, 121 ); // 쌓인 대기가 없다 — 목록은 그대로
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( timeline.getSlowestWaits().size() ) );
}

/**
 * @brief [AutomationTimelineTest] 가장 긴 대기만 긴 순으로 `kSlowestWaitKeep` 개 남는다
 */
SW_TEST_CASE( AutomationTimelineTest, KeepsOnlyTheLongestWaitsInOrder )
{
    sw::AutomationTimeline timeline;
    for ( uint32 index = 1; index <= sw::AutomationTimeline::kSlowestWaitKeep + 3; ++index )
    {
        timeline.recordFrame( sw::AutomationFrameKind::ScheduledWait, static_cast<float64>( index ) );
        timeline.closeWait( "Expect", index, index );
    }
    const sw::vector<sw::AutomationSlowWait>& listWait = timeline.getSlowestWaits();
    SW_ASSERT_EQUAL( sw::AutomationTimeline::kSlowestWaitKeep, static_cast<uint32>( listWait.size() ) );
    SW_EXPECT_EQUAL( sw::AutomationTimeline::kSlowestWaitKeep + 3, listWait[0]._stepNumber );
    SW_EXPECT_EQUAL( 4u, listWait.back()._stepNumber );
    for ( size_t index = 1; index < listWait.size(); ++index )
    {
        SW_EXPECT_TRUE( listWait[index]._seconds <= listWait[index - 1]._seconds );
    }
}
