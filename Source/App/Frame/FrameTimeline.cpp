#include "pch.h"

#include "App/Frame/FrameTimeline.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    FrameTimeline::FrameTimeline()
        : _timer{}
        , _accumulator{ 0.0f }
        , _maxFrameDeltaTime{ kDefaultMaxFrameDeltaTime }
        , _fixedDeltaTime{ kDefaultFixedDeltaTime }
        , _maxFixedStepPerFrame{ kDefaultMaxFixedStepPerFrame }
    {
    }

    void FrameTimeline::configure( float32 maxFrameDeltaTime, float32 fixedDeltaTime, uint32 maxFixedStepPerFrame )
    {
        _maxFrameDeltaTime    = maxFrameDeltaTime > 0.0f ? maxFrameDeltaTime : kDefaultMaxFrameDeltaTime;
        _fixedDeltaTime       = fixedDeltaTime > 0.0f ? fixedDeltaTime : kDefaultFixedDeltaTime;
        _maxFixedStepPerFrame = maxFixedStepPerFrame > 0 ? maxFixedStepPerFrame : kDefaultMaxFixedStepPerFrame;
    }

    void FrameTimeline::start()
    {
        _accumulator = 0.0f;
        _timer.resetTimer();
        _timer.startTimer();
    }

    FrameTime FrameTimeline::advance()
    {
        _timer.updateTimer();

        FrameTime frameTime{};
        frameTime._deltaTime      = MathUtil::min( _timer.getDeltaTime(), _maxFrameDeltaTime );
        frameTime._fixedDeltaTime = _fixedDeltaTime;

        _accumulator += frameTime._deltaTime;

        uint32 stepCount = static_cast<uint32>( _accumulator / _fixedDeltaTime );
        if ( stepCount > _maxFixedStepPerFrame )
        {
            // 상한을 넘긴 잔액은 버린다. 남겨 두면 다음 프레임이 더 많은 스텝을 요구하고
            // 그래서 더 길어지는 되먹임이 된다 — 시뮬레이션이 실시간보다 느려지는 쪽을 택한다.
            stepCount    = _maxFixedStepPerFrame;
            _accumulator = 0.0f;
        }
        else
        {
            _accumulator -= static_cast<float32>( stepCount ) * _fixedDeltaTime;
        }

        frameTime._fixedStepCount = stepCount;
        return frameTime;
    }
} // namespace sw
