#include "pch.h"

#include "AppHost/FixedTimestep.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineConfig.h"

namespace sw
{
    FixedTimestep::FixedTimestep()
        : _timer{}
        , _accumulator{ 0.0f }
        , _maxFrameDeltaTime{ EngineConfig{}._maxFrameDeltaTime }
        , _fixedDeltaTime{ EngineConfig{}._fixedDeltaTime }
        , _maxFixedStepPerFrame{ EngineConfig{}._maxFixedStepPerFrame }
    {
    }

    void FixedTimestep::configure( float32 maxFrameDeltaTime, float32 fixedDeltaTime, uint32 maxFixedStepPerFrame )
    {
        // 설정 로드가 범위(Min)를 이미 본다. 코드가 직접 부르는 길에서 0 으로 나누지 않게 기본값(EngineConfig 의 초기값 — 출처 하나)으로 되돌린다.
        const EngineConfig defaults{};
        _maxFrameDeltaTime    = maxFrameDeltaTime > 0.0f ? maxFrameDeltaTime : defaults._maxFrameDeltaTime;
        _fixedDeltaTime       = fixedDeltaTime > 0.0f ? fixedDeltaTime : defaults._fixedDeltaTime;
        _maxFixedStepPerFrame = maxFixedStepPerFrame > 0 ? maxFixedStepPerFrame : defaults._maxFixedStepPerFrame;
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
        frameTime._unscaledDeltaTime = MathUtil::min( elapsedSeconds, _maxFrameDeltaTime );
        frameTime._deltaTime         = frameTime._unscaledDeltaTime * MathUtil::max( timeScale, 0.0f );
        frameTime._fixedDeltaTime    = _fixedDeltaTime;

        _accumulator += frameTime._deltaTime;

        uint32 stepCount = static_cast<uint32>( _accumulator / _fixedDeltaTime );
        if ( stepCount > _maxFixedStepPerFrame )
        {
            // 상한을 넘긴 남은 시간은 버린다. 남겨 두면 다음 프레임이 더 많은 스텝을 요구하고, 그래서 더 길어지는 악순환이 된다.
            // 시뮬레이션이 실시간보다 느려지는 쪽을 택한다.
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
