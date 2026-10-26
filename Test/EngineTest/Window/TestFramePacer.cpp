#include "pch.h"

#include "Core/Common/Defines.h"

#include "Engine/Window/FramePacer.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    FramePacingInputs makeInputs( bool bAutomationRun, bool bListenHost, bool bMinimized, bool bFocused )
    {
        FramePacingInputs inputs;
        inputs._bAutomationRun = bAutomationRun;
        inputs._bListenHost    = bListenHost;
        inputs._bMinimized     = bMinimized;
        inputs._bFocused       = bFocused;
        return inputs;
    }
} // namespace

SW_TEST_CASE( FramePacerTest, WindowStateDecidesInteractiveRuns )
{
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, false, false, true ) ) == FramePacingState::Foreground );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, false, false, false ) ) == FramePacingState::Background );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, false, true, false ) ) == FramePacingState::Minimized );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, false, true, true ) ) == FramePacingState::Minimized );
}

// 시나리오는 대개 포커스 없는 창으로 돈다(여러 작업자 · CI). 그때도 배경 상한 · 최소화 대기에 걸리면 안 된다.
SW_TEST_CASE( FramePacerTest, AutomationRunIsFullSpeedWithoutFocus )
{
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( true, false, false, false ) ) == FramePacingState::Automation );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( true, false, true, false ) ) == FramePacingState::Automation );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( true, true, true, false ) ) == FramePacingState::Automation );
}

SW_TEST_CASE( FramePacerTest, ListenHostIsFullSpeedWithoutFocus )
{
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, true, false, false ) ) == FramePacingState::Server );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, true, true, false ) ) == FramePacingState::Server );
}

SW_TEST_CASE( FramePacerTest, BackgroundDeadlineFollowsFrameRateLimit )
{
    constexpr int64 kStart = 5 * constant::kNanosecondsPerSecond;
    SW_EXPECT_EQUAL( kStart + constant::kNanosecondsPerSecond / 30, FramePacer::computeBackgroundDeadlineNanoseconds( kStart, 30 ) );
    SW_EXPECT_EQUAL( kStart, FramePacer::computeBackgroundDeadlineNanoseconds( kStart, 0 ) );
    SW_EXPECT_EQUAL( kStart, FramePacer::computeBackgroundDeadlineNanoseconds( kStart, -1 ) );
}
