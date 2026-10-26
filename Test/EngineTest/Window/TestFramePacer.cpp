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

    FramePacingState resolveAuto( const FramePacingInputs& inputs )
    {
        return FramePacer::resolve( inputs, FramePacingRequest::Auto, 0 );
    }
} // namespace

SW_TEST_CASE( FramePacerTest, WindowStateDecidesInteractiveRuns )
{
    SW_EXPECT_TRUE( resolveAuto( makeInputs( false, false, false, true ) ) == FramePacingState::Foreground );
    SW_EXPECT_TRUE( resolveAuto( makeInputs( false, false, false, false ) ) == FramePacingState::Background );
    SW_EXPECT_TRUE( resolveAuto( makeInputs( false, false, true, false ) ) == FramePacingState::Minimized );
    SW_EXPECT_TRUE( resolveAuto( makeInputs( false, false, true, true ) ) == FramePacingState::Minimized );
}

// 시나리오는 대개 포커스 없는 창으로 돈다(여러 작업자 · CI). 그때도 배경 상한 · 최소화 대기 · 유휴 대기에 걸리면 안 된다.
SW_TEST_CASE( FramePacerTest, AutomationRunIsFullSpeedWithoutFocus )
{
    SW_EXPECT_TRUE( resolveAuto( makeInputs( true, false, false, false ) ) == FramePacingState::Automation );
    SW_EXPECT_TRUE( resolveAuto( makeInputs( true, false, true, false ) ) == FramePacingState::Automation );
    SW_EXPECT_TRUE( resolveAuto( makeInputs( true, true, true, false ) ) == FramePacingState::Automation );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( true, false, false, false ), FramePacingRequest::Auto, FramePacer::kEditorIdleSettleFrames ) == FramePacingState::Automation );
}

SW_TEST_CASE( FramePacerTest, ListenHostIsFullSpeedWithoutFocus )
{
    SW_EXPECT_TRUE( resolveAuto( makeInputs( false, true, false, false ) ) == FramePacingState::Server );
    SW_EXPECT_TRUE( resolveAuto( makeInputs( false, true, true, false ) ) == FramePacingState::Server );
}

// 에디터 멀티플레이의 자식 프로세스는 실행 인자(`-gv_framePacing=FullSpeed`)로 전속을 고른다 — 포커스 · 최소화 · 유휴와 상관없다.
SW_TEST_CASE( FramePacerTest, FullSpeedRequestPinsTheState )
{
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, false, false, false ), FramePacingRequest::FullSpeed, 0 ) == FramePacingState::FullSpeed );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, false, true, false ), FramePacingRequest::FullSpeed, FramePacer::kEditorIdleSettleFrames ) == FramePacingState::FullSpeed );
}

// 조용한 프레임이 이어져야 잔다 — 마지막 변화가 화면에 닿을 몫을 남긴다. 시나리오는 `WindowState` 로 자동화 중에도 이 상태를 본다.
SW_TEST_CASE( FramePacerTest, EditorIdleNeedsSettledQuietFrames )
{
    const FramePacingInputs focused = makeInputs( false, false, false, true );
    SW_EXPECT_TRUE( FramePacer::resolve( focused, FramePacingRequest::Auto, FramePacer::kEditorIdleSettleFrames - 1 ) == FramePacingState::Foreground );
    SW_EXPECT_TRUE( FramePacer::resolve( focused, FramePacingRequest::Auto, FramePacer::kEditorIdleSettleFrames ) == FramePacingState::EditorIdle );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, false, true, true ), FramePacingRequest::Auto, FramePacer::kEditorIdleSettleFrames ) == FramePacingState::Minimized );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( true, false, false, false ), FramePacingRequest::WindowState, FramePacer::kEditorIdleSettleFrames ) == FramePacingState::EditorIdle );
}

SW_TEST_CASE( FramePacerTest, BackgroundDeadlineFollowsFrameRateLimit )
{
    constexpr int64 kStart = 5 * constant::kNanosecondsPerSecond;
    SW_EXPECT_EQUAL( kStart + constant::kNanosecondsPerSecond / 30, FramePacer::computeBackgroundDeadlineNanoseconds( kStart, 30 ) );
    SW_EXPECT_EQUAL( kStart, FramePacer::computeBackgroundDeadlineNanoseconds( kStart, 0 ) );
    SW_EXPECT_EQUAL( kStart, FramePacer::computeBackgroundDeadlineNanoseconds( kStart, -1 ) );
}
