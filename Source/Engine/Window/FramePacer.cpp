#include "pch.h"

#include "Engine/Window/FramePacer.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Window/IWindow.h"

namespace sw
{
    /**
     * @brief `-gv_backgroundFrameRate=<초당 프레임>`: 포커스 없는 창의 프레임 상한입니다(0 이하 = 상한 없음).
     * @details 배경 창이 전속으로 돌며 CPU · GPU 를 태우지 않게 한다. 자동화 · 리슨 호스트는 이 값과 상관없이 전속이다(`FramePacingState`).
     */
    SW_GLOBAL_VARIABLE( int32, gv_backgroundFrameRate, 30, "포커스 없는 창의 초당 프레임 상한 (0=상한 없음)" );
} // namespace sw

namespace sw
{
    FramePacingState FramePacer::resolve( const FramePacingInputs& inputs )
    {
        if ( inputs._bAutomationRun )
            return FramePacingState::Automation;
        if ( inputs._bListenHost )
            return FramePacingState::Server;
        if ( inputs._bMinimized )
            return FramePacingState::Minimized;
        return inputs._bFocused ? FramePacingState::Foreground : FramePacingState::Background;
    }

    int64 FramePacer::computeBackgroundDeadlineNanoseconds( int64 frameStartNanoseconds, int32 frameRateLimit )
    {
        if ( frameRateLimit <= 0 )
            return frameStartNanoseconds;
        return frameStartNanoseconds + constant::kNanosecondsPerSecond / frameRateLimit;
    }

    void FramePacer::beginFrame( const FramePacingInputs& inputs )
    {
        _state                 = resolve( inputs );
        _frameStartNanoseconds = MonotonicClock::nowNanoseconds();
    }

    void FramePacer::endFrame( IWindow& window ) const
    {
        if ( _state == FramePacingState::Background )
            MonotonicClock::sleepUntilNanoseconds( computeBackgroundDeadlineNanoseconds( _frameStartNanoseconds, gv_backgroundFrameRate ) );
        else if ( _state == FramePacingState::Minimized )
            window.waitForMessages( kMinimizedWakeMilliseconds );
    }
} // namespace sw
