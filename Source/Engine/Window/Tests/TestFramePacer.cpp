#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Window/FramePacer.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/Tests/FakeWindow.h"

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

    /** @brief 페이서가 무엇을 기다리라고 했는지 적는 창입니다. 잠들지 않는다 — 기다리는 방법은 창의 몫이다(브라우저는 메인 스레드를 재울 수 없다). */
    class PacingRecordWindow final : public FakeWindow
    {
    public:
        uint32 _frameWaitCount{ 0 };
        uint32 _messageWaitCount{ 0 };
        int64  _frameStartNanoseconds{ 0 };
        int64  _deadlineNanoseconds{ 0 };

        PacingRecordWindow()
            : FakeWindow{ NativeSurfaceKind::WebCanvas }
        {
        }

        void waitForMessages( uint32 /*timeoutMilliseconds*/ ) override { ++_messageWaitCount; }
        void waitForNextFrame( int64 frameStartNanoseconds, int64 deadlineNanoseconds ) override
        {
            ++_frameWaitCount;
            _frameStartNanoseconds = frameStartNanoseconds;
            _deadlineNanoseconds   = deadlineNanoseconds;
        }
    };
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
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( true, false, false, false ), FramePacingRequest::Auto, FramePacer::kEditorIdleStabilityFrames ) == FramePacingState::Automation );
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
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, false, true, false ), FramePacingRequest::FullSpeed, FramePacer::kEditorIdleStabilityFrames ) == FramePacingState::FullSpeed );
}

// 조용한 프레임이 이어져야 잔다 — 마지막 변화가 화면에 닿을 몫을 남긴다. 시나리오는 `WindowState` 로 자동화 중에도 이 상태를 본다.
SW_TEST_CASE( FramePacerTest, EditorIdleNeedsStableQuietFrames )
{
    const FramePacingInputs focused = makeInputs( false, false, false, true );
    SW_EXPECT_TRUE( FramePacer::resolve( focused, FramePacingRequest::Auto, FramePacer::kEditorIdleStabilityFrames - 1 ) == FramePacingState::Foreground );
    SW_EXPECT_TRUE( FramePacer::resolve( focused, FramePacingRequest::Auto, FramePacer::kEditorIdleStabilityFrames ) == FramePacingState::EditorIdle );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( false, false, true, true ), FramePacingRequest::Auto, FramePacer::kEditorIdleStabilityFrames ) == FramePacingState::Minimized );
    SW_EXPECT_TRUE( FramePacer::resolve( makeInputs( true, false, false, false ), FramePacingRequest::WindowState, FramePacer::kEditorIdleStabilityFrames ) == FramePacingState::EditorIdle );
}

SW_TEST_CASE( FramePacerTest, BackgroundDeadlineFollowsFrameRateLimit )
{
    constexpr int64 kStart = 5 * constant::time::kNanosecondsPerSecond;
    SW_EXPECT_EQUAL( kStart + constant::time::kNanosecondsPerSecond / 30, FramePacer::computeBackgroundDeadlineNanoseconds( kStart, 30 ) );
    SW_EXPECT_EQUAL( kStart, FramePacer::computeBackgroundDeadlineNanoseconds( kStart, 0 ) );
    SW_EXPECT_EQUAL( kStart, FramePacer::computeBackgroundDeadlineNanoseconds( kStart, -1 ) );
}

/**
 * @brief [FramePacerTest] 배경 상한 대기는 페이서가 스스로 자지 않고 창(`IWindow::waitForNextFrame`)에 맡긴다 — 웹은 메인 스레드를 재우면 브라우저가 멈춘다(감사 P-5).
 *        전속 상태도 창에 "기다릴 것 없음"(마감 = 시작)을 알려, 배경에서 미뤄 둔 다음 프레임 콜백을 되돌릴 수 있게 한다. 최소화는 창 메시지를 기다린다.
 */
SW_TEST_CASE( FramePacerTest, FrameWaitGoesThroughTheWindow )
{
    PacingRecordWindow window;
    FramePacer         pacer;

    pacer.markFrameStart();
    pacer.beginFrame( makeInputs( false, false, false, false ) );
    SW_REQUIRE_TRUE( pacer.getState() == FramePacingState::Background );
    pacer.endFrame( window );
    SW_EXPECT_EQUAL( 1u, window._frameWaitCount );
    SW_EXPECT_TRUE( window._deadlineNanoseconds >= window._frameStartNanoseconds ); // 상한(gv_backgroundFrameRate)이 0 이면 같다

    pacer.markFrameStart();
    pacer.beginFrame( makeInputs( false, false, false, true ) );
    pacer.endFrame( window );
    SW_EXPECT_EQUAL( 2u, window._frameWaitCount );
    SW_EXPECT_EQUAL( window._frameStartNanoseconds, window._deadlineNanoseconds );

    pacer.beginFrame( makeInputs( false, false, true, false ) );
    pacer.endFrame( window );
    SW_EXPECT_EQUAL( 2u, window._frameWaitCount );
    SW_EXPECT_EQUAL( 1u, window._messageWaitCount );
}
