#include "pch.h"

#include "Core/Time/FrameClock.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    FrameClock::FrameClock()
        : _pClock{ &SystemMonotonicClock::get() }
        , _prevNanoseconds{ 0 }
        , _params{}
        , _stepTimer{}
    {
    }

    FrameClock::FrameClock( const FrameClockParams& params )
        : FrameClock{ params, SystemMonotonicClock::get() }
    {
    }

    FrameClock::FrameClock( const FrameClockParams& params, const IClock& clock )
        : _pClock{ &clock }
        , _prevNanoseconds{ 0 }
        , _params{}
        , _stepTimer{}
    {
        configure( params );
    }

    void FrameClock::configure( const FrameClockParams& params )
    {
        // 설정 로드가 범위(Min)를 이미 본다. 다른 값으로 몰래 바꾸면 틀린 설정이 조용히 다른 정책이 된다 — 계약 위반은 단언한다.
        const bool bValid = params._maxFrameDeltaTime > 0.0f && params._fixedDeltaTime > 0.0f && params._maxFixedStepPerFrame > 0;
        // 시계 층(티어 2)은 로그 아래라 `SW_ENSURE` 대신 아래 티어 단언을 쓴다 — 단언과 빠져나가기가 같은 변수를 본다.
        SW_LOW_LEVEL_ASSERT( bValid, "FrameClock needs positive time policy values (EngineConfig range checks should have rejected these)" );
        if ( bValid == false )
            return;
        _params = params;
        // 한 프레임에 받는 시간을 스텝 상한만큼에서 자른다 — 넘친 시간을 남겨 다음 프레임이 더 많은 스텝을 요구하는 악순환(고정 스텝 스파이럴)을 막는다.
        _stepTimer = FixedStepTimer( params._fixedDeltaTime, params._fixedDeltaTime * static_cast<float32>( params._maxFixedStepPerFrame ) );
    }

    void FrameClock::start()
    {
        SW_LOW_LEVEL_ASSERT( isConfigured(), "FrameClock::start before configure - read EngineConfig first" );
        _stepTimer.reset();
        _prevNanoseconds = _pClock->nowNanoseconds();
    }

    FrameTime FrameClock::advance( float32 timeScale, float32 overrideFrameSeconds )
    {
        const int64   nowNanoseconds = _pClock->nowNanoseconds();
        const float32 realSeconds    = static_cast<float32>( MathUtil::max<int64>( nowNanoseconds - _prevNanoseconds, 0 ) ) / static_cast<float32>( constant::time::kNanosecondsPerSecond );
        _prevNanoseconds             = nowNanoseconds;
        const float32 elapsedSeconds = overrideFrameSeconds > 0.0f ? overrideFrameSeconds : realSeconds;

        FrameTime frameTime{};
        frameTime._unscaledDeltaTime = MathUtil::min( elapsedSeconds, _params._maxFrameDeltaTime );
        frameTime._deltaTime         = frameTime._unscaledDeltaTime * MathUtil::max( timeScale, 0.0f );
        frameTime._fixedDeltaTime    = _params._fixedDeltaTime;

        // 상한을 넘긴 시간은 누적기가 버린다 — 시뮬레이션이 실시간보다 느려지는 쪽을 택한다.
        frameTime._fixedStepCount = static_cast<uint32>( _stepTimer.consume( frameTime._deltaTime ) );
        return frameTime;
    }
} // namespace sw
