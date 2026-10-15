#include "pch.h"

#include "Core/Time/FixedTimestep.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    FixedTimestep::FixedTimestep()
        : _timer{}
        , _params{}
        , _accumulator{ 0.0f }
    {
    }

    FixedTimestep::FixedTimestep( const FixedTimestepParams& params )
        : _timer{}
        , _params{}
        , _accumulator{ 0.0f }
    {
        configure( params );
    }

    void FixedTimestep::configure( const FixedTimestepParams& params )
    {
        // 설정 로드가 범위(Min)를 이미 본다. 다른 값으로 몰래 바꾸면 틀린 설정이 조용히 다른 정책이 된다 — 계약 위반은 단언한다.
        const bool bValid = params._maxFrameDeltaTime > 0.0f && params._fixedDeltaTime > 0.0f && params._maxFixedStepPerFrame > 0;
        SW_LOG_ASSERT( bValid, "FixedTimestep needs positive time policy values (EngineConfig range checks should have rejected these)" );
        if ( bValid )
            _params = params;
    }

    void FixedTimestep::start()
    {
        SW_LOG_ASSERT( isConfigured(), "FixedTimestep::start before configure - read EngineConfig first" );
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
