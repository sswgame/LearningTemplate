#include "pch.h"

#include "Engine/Renderer/Frame/RenderView.h"
#include "Engine/Renderer/Frame/RenderViewScheduler.h"

#include "TestFramework/TestFramework.h"

// 추가 뷰 스케줄러 — 갱신 주기로 쉬기 · 보이지 않으면 쉬기 · 프레임 예산과 굶지 않기 · 사라진 뷰 잊기.

using namespace sw;

/**
 * @brief [RenderViewSchedulerTest] 10 Hz 뷰는 60 Hz 프레임에서 여섯 프레임마다 한 번 그리고, 0 Hz 뷰는 매 프레임 그린다
 */
SW_TEST_CASE( RenderViewSchedulerTest, UpdateRateSkipsFrames )
{
    RenderViewScheduler            scheduler;
    RenderViewScheduler::Candidate arrCandidate[2]{};
    arrCandidate[0]._viewId     = 1;
    arrCandidate[0]._updateRate = 10.0f;
    arrCandidate[1]._viewId     = 2;
    arrCandidate[1]._updateRate = 0.0f;

    uint32 arrRenderCount[2]{};
    uint32 firstRenders[8]{};
    uint32 firstRenderCount = 0;
    for ( uint32 frameIndex = 0; frameIndex < 60; ++frameIndex )
    {
        uint8 arrRender[2]{};
        (void)scheduler.schedule( static_cast<float64>( frameIndex ) / 60.0, arrCandidate, 2, 0, arrRender );
        for ( uint32 index = 0; index < 2; ++index )
        {
            arrRenderCount[index] += arrRender[index] == SW_TRUE ? 1u : 0u;
        }
        if ( arrRender[0] == SW_TRUE && firstRenderCount < 8 )
            firstRenders[firstRenderCount++] = frameIndex;
    }
    SW_EXPECT_EQUAL( 10u, arrRenderCount[0] );
    SW_EXPECT_EQUAL( 60u, arrRenderCount[1] );
    SW_EXPECT_EQUAL( 0u, firstRenders[0] );
    SW_EXPECT_EQUAL( 6u, firstRenders[1] );
    SW_EXPECT_EQUAL( 12u, firstRenders[2] );
}

/**
 * @brief [RenderViewSchedulerTest] 보이지 않는 뷰는 그리지 않고, 보이게 되면 그 프레임에 바로 그린다(쉬던 시간이 이미 지났다)
 */
SW_TEST_CASE( RenderViewSchedulerTest, InvisibleViewsWait )
{
    RenderViewScheduler            scheduler;
    RenderViewScheduler::Candidate candidate;
    candidate._viewId     = 7;
    candidate._updateRate = 2.0f;
    candidate._bVisible   = SW_FALSE;
    uint8 bRender         = SW_FALSE;
    for ( uint32 frameIndex = 0; frameIndex < 30; ++frameIndex )
    {
        (void)scheduler.schedule( static_cast<float64>( frameIndex ) / 30.0, &candidate, 1, 0, &bRender );
        SW_EXPECT_TRUE( bRender == SW_FALSE );
    }
    candidate._bVisible = SW_TRUE;
    (void)scheduler.schedule( 1.0, &candidate, 1, 0, &bRender );
    SW_EXPECT_TRUE( bRender == SW_TRUE );
    (void)scheduler.schedule( 1.1, &candidate, 1, 0, &bRender );
    SW_EXPECT_TRUE( bRender == SW_FALSE ); // 2 Hz — 0.5 초가 안 지났다
}

/**
 * @brief [RenderViewSchedulerTest] 예산보다 많은 뷰가 때가 되면 가장 오래 기다린 것부터 그리고, 밀린 뷰는 다음 프레임에 먼저 그린다(굶지 않는다)
 */
SW_TEST_CASE( RenderViewSchedulerTest, BudgetRotatesWithoutStarving )
{
    RenderViewScheduler            scheduler;
    RenderViewScheduler::Candidate arrCandidate[3]{};
    for ( uint32 index = 0; index < 3; ++index )
    {
        arrCandidate[index]._viewId = 100 + index;
    }

    uint32 arrRenderCount[3]{};
    for ( uint32 frameIndex = 0; frameIndex < 30; ++frameIndex )
    {
        uint8        arrRender[3]{};
        const uint32 chosen = scheduler.schedule( static_cast<float64>( frameIndex ) / 60.0, arrCandidate, 3, 2, arrRender );
        SW_EXPECT_EQUAL( 2u, chosen );
        for ( uint32 index = 0; index < 3; ++index )
        {
            arrRenderCount[index] += arrRender[index] == SW_TRUE ? 1u : 0u;
        }
    }
    // 예산 둘을 셋이 나눠 쓴다 — 셋 다 20 번(±1) 근처다. 늦은 쪽을 먼저 고르지 않으면 한 뷰가 0 번이다.
    for ( uint32 index = 0; index < 3; ++index )
    {
        SW_EXPECT_TRUE_MSG( 19u <= arrRenderCount[index] && arrRenderCount[index] <= 21u, ( "뷰가 굶었다: " + to_string( arrRenderCount[index] ) ).c_str() );
    }
}

/**
 * @brief [RenderViewSchedulerTest] 후보에서 빠진 뷰(카메라가 사라졌다)는 잊고, 다시 나타나면 처음 보는 뷰처럼 바로 그린다
 */
SW_TEST_CASE( RenderViewSchedulerTest, ForgetsViewsThatDisappear )
{
    RenderViewScheduler            scheduler;
    RenderViewScheduler::Candidate candidate;
    candidate._viewId     = 9;
    candidate._updateRate = 0.5f;
    uint8 bRender         = SW_FALSE;
    (void)scheduler.schedule( 0.0, &candidate, 1, 0, &bRender );
    SW_EXPECT_TRUE( bRender == SW_TRUE );
    SW_EXPECT_EQUAL( 1u, scheduler.getTrackedCount() );
    (void)scheduler.schedule( 0.1, &candidate, 0, 0, &bRender );
    SW_EXPECT_EQUAL( 0u, scheduler.getTrackedCount() );
    (void)scheduler.schedule( 0.2, &candidate, 1, 0, &bRender );
    SW_EXPECT_TRUE( bRender == SW_TRUE );
}

/**
 * @brief [RenderViewSchedulerTest] 출력 사각형이 전체인지의 판정 — 주 시점 Present 가 뷰포트를 걸지(사각형) 말지(전체)를 가른다
 */
SW_TEST_CASE( RenderViewSchedulerTest, FullRectIsDetected )
{
    RenderViewSettings settings;
    SW_EXPECT_TRUE( settings.isFullRect() );
    settings._screenRect = float4{ 0.5f, 0.0f, 0.5f, 1.0f };
    SW_EXPECT_FALSE( settings.isFullRect() );
}
