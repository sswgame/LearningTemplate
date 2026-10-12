#include "pch.h"

#include "Core/Time/FixedTimestep.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    FixedTimestep::FixedTimestep( const FixedTimestepParams& defaults )
        : _timer{}
        , _defaults{ defaults }
        , _params{ defaults }
        , _accumulator{ 0.0f }
    {
        SW_ASSERT( defaults._maxFrameDeltaTime > 0.0f && defaults._fixedDeltaTime > 0.0f && defaults._maxFixedStepPerFrame > 0 );
    }

    void FixedTimestep::configure( const FixedTimestepParams& params )
    {
        // 설정 로드가 범위(Min)를 이미 본다. 코드가 직접 부르는 길에서 0 으로 나누지 않게 만들 때 받은 기본값(출처는 값을 가진 쪽 하나)으로 되돌린다.
        _params._maxFrameDeltaTime    = params._maxFrameDeltaTime > 0.0f ? params._maxFrameDeltaTime : _defaults._maxFrameDeltaTime;
        _params._fixedDeltaTime       = params._fixedDeltaTime > 0.0f ? params._fixedDeltaTime : _defaults._fixedDeltaTime;
        _params._maxFixedStepPerFrame = params._maxFixedStepPerFrame > 0 ? params._maxFixedStepPerFrame : _defaults._maxFixedStepPerFrame;
    }

    void FixedTimestep::start()
    {
        _accumulator = 0.0f;
        _timer.resetTimer();
        _timer.startTimer();
    }

    FrameTime FixedTimestep::advance( float32 timeScale, float32 overrideFrameSeconds )
    {
        _timer.updateTimer();
        const float32 elapsedSeconds = overrideFrameSeconds > 0.0f ? overrideFrameSeconds : _timer.getDeltaTime();

        FrameTime frameTime{};
        frameTime._unscaledDeltaTime = MathUtil::min( elapsedSeconds, _params._maxFrameDeltaTime );
        frameTime._deltaTime         = frameTime._unscaledDeltaTime * MathUtil::max( timeScale, 0.0f );
        frameTime._fixedDeltaTime    = _params._fixedDeltaTime;

        _accumulator += frameTime._deltaTime;

        uint32 stepCount = static_cast<uint32>( _accumulator / _params._fixedDeltaTime );
        if ( stepCount > _params._maxFixedStepPerFrame )
        {
            // 상한을 넘긴 남은 시간은 버린다. 남겨 두면 다음 프레임이 더 많은 스텝을 요구하고, 그래서 더 길어지는 악순환이 된다.
            // 시뮬레이션이 실시간보다 느려지는 쪽을 택한다.
            stepCount    = _params._maxFixedStepPerFrame;
            _accumulator = 0.0f;
        }
        else
        {
            _accumulator -= static_cast<float32>( stepCount ) * _params._fixedDeltaTime;
        }

        frameTime._fixedStepCount = stepCount;
        return frameTime;
    }
} // namespace sw
