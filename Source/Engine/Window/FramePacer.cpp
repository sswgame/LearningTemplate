#include "pch.h"

#include "Engine/Window/FramePacer.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Window/IWindow.h"

namespace sw
{
    /**
     * @brief `-gv_backgroundFrameRate=<초당 프레임>`: 포커스 없는 창의 프레임 상한입니다(0 이하 = 상한 없음).
     * @details 배경 창이 전속으로 돌며 CPU · GPU 를 태우지 않게 한다. 자동화 · 리슨 호스트 · `FullSpeed` 는 이 값과 상관없이 전속이다(`FramePacingState`).
     */
    SW_GLOBAL_VARIABLE( int32, gv_backgroundFrameRate, 30, "포커스 없는 창의 초당 프레임 상한 (0=상한 없음)" );

    /**
     * @brief `-gv_framePacing=<Auto|FullSpeed|WindowState>`: 앱 루프 페이싱을 실행 인자로 고릅니다(`FramePacingRequest`).
     * @details 에디터 멀티플레이는 자식 프로세스에 `FullSpeed` 를 준다(포커스 없는 클라이언트가 배경 상한에 걸리지 않게). 시나리오는 `WindowState` 로
     *          자동화 중에도 배경 · 유휴 페이싱을 본다(`editor.idle`).
     */
    SW_GLOBAL_VARIABLE( FramePacingRequest, gv_framePacing, FramePacingRequest::Auto, "앱 루프 페이싱 요청 (Auto=실행 방식과 창 상태대로, FullSpeed=늘 전속, WindowState=자동화여도 창 상태대로)" );
} // namespace sw

namespace sw
{
    FramePacingState FramePacer::resolve( const FramePacingInputs& inputs, FramePacingRequest request, uint32 editorQuietFrames )
    {
        if ( request == FramePacingRequest::FullSpeed )
            return FramePacingState::FullSpeed;
        if ( inputs._bAutomationRun && request != FramePacingRequest::WindowState )
            return FramePacingState::Automation;
        if ( inputs._bListenHost )
            return FramePacingState::Server;
        if ( inputs._bMinimized )
            return FramePacingState::Minimized;
        if ( editorQuietFrames >= kEditorIdleSettleFrames )
            return FramePacingState::EditorIdle;
        return inputs._bFocused ? FramePacingState::Foreground : FramePacingState::Background;
    }

    int64 FramePacer::computeBackgroundDeadlineNanoseconds( int64 frameStartNanoseconds, int32 frameRateLimit )
    {
        if ( frameRateLimit <= 0 )
            return frameStartNanoseconds;
        return frameStartNanoseconds + constant::kNanosecondsPerSecond / frameRateLimit;
    }

    void FramePacer::markFrameStart()
    {
        _frameStartNanoseconds = MonotonicClock::nowNanoseconds();
    }

    void FramePacer::beginFrame( const FramePacingInputs& inputs )
    {
        _editorQuietFrames = inputs._bEditorQuiet ? MathUtil::min( _editorQuietFrames + 1, kEditorIdleSettleFrames ) : 0;
        _state             = resolve( inputs, gv_framePacing, _editorQuietFrames );
    }

    void FramePacer::endFrame( IWindow& window ) const
    {
        if ( _state == FramePacingState::Background )
            MonotonicClock::sleepUntilNanoseconds( computeBackgroundDeadlineNanoseconds( _frameStartNanoseconds, gv_backgroundFrameRate ) );
        else if ( _state == FramePacingState::Minimized )
            window.waitForMessages( kMinimizedWakeMilliseconds );
        else if ( _state == FramePacingState::EditorIdle )
            window.waitForMessages( kEditorIdleWakeMilliseconds );
    }
} // namespace sw
