#include "pch.h"

#include "Core/Network/Replication/NetClock.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    NetClock::NetClock()
        : _settings{}
        , _serverTick{ 0.0f }
        , _renderTick{ kNoRenderTick }
        , _bHasServerTick{ SW_FALSE }
    {
    }

    void NetClock::initialize( const NetClockSettings& settings )
    {
        _settings = settings;
        reset();
    }

    void NetClock::reset()
    {
        _serverTick     = 0.0f;
        _renderTick     = kNoRenderTick;
        _bHasServerTick = SW_FALSE;
    }

    float32 NetClock::getTickInterval() const { return MathUtil::max( 1.0e-4f, _settings._tickInterval ); }

    float32 NetClock::computeInterpolationDelay() const { return MathUtil::max( _settings._interpolationDelay, _settings._sampleInterval * kSampleIntervalsBehind ); }

    void NetClock::observeServerTick( uint32 serverTick )
    {
        const float32 tick   = static_cast<float32>( serverTick );
        const bool    bFirst = _bHasServerTick == SW_FALSE;
        if ( bFirst || tick > _serverTick )
            _serverTick = tick;
        _bHasServerTick = SW_TRUE;
        // Smooth 는 첫 틱에서 바로 목표에 선다(그 뒤로는 흐르며 맞춘다). Monotonic 은 다음 advance 가 둔다.
        if ( bFirst && _settings._mode == NetClockMode::Smooth )
            _renderTick = tick - computeInterpolationDelay() / getTickInterval();
    }

    void NetClock::advance( float32 deltaTime )
    {
        if ( _bHasServerTick == SW_FALSE )
            return;
        if ( _settings._mode == NetClockMode::Smooth )
            advanceSmooth( deltaTime );
        else
            advanceMonotonic( deltaTime );
    }

    void NetClock::advanceSmooth( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        // 목표 = 가장 새로 받은 틱 − 지연. 벗어난 만큼 조금 빠르게 · 느리게 흘려 맞춘다(튀지 않게). 크게 벗어나면 바로 맞춘다.
        const float32 tickInterval = getTickInterval();
        const float32 delay        = computeInterpolationDelay();
        const float32 delayTicks   = delay / tickInterval;
        const float32 stepTicks    = deltaTime / tickInterval;
        const float32 target       = _serverTick - delayTicks;
        const float32 error        = target - ( _renderTick + stepTicks );
        if ( MathUtil::abs( error ) > delayTicks * kSnapDelayMultiple )
        {
            _renderTick = target;
            return;
        }
        const float32 errorRatio = error / ( MathUtil::max( 1.0e-3f, delay ) / tickInterval );
        const float32 scale      = 1.0f + MathUtil::clamp( errorRatio, -1.0f, 1.0f ) * _settings._clockCorrection;
        _renderTick += stepTicks * scale;
    }

    void NetClock::advanceMonotonic( float32 deltaTime )
    {
        // 추정은 흐르는 시간만큼 앞으로 간다(받은 틱은 observeServerTick 이 끌어올린다). 렌더 틱은 뒤로 가지 않는다 — 지연이 커져도(표본 간격이 늘었다) 멈춰 기다린다.
        const float32 tickInterval = getTickInterval();
        _serverTick += deltaTime / tickInterval;
        _renderTick = MathUtil::max( _renderTick, _serverTick - computeInterpolationDelay() / tickInterval );
    }
} // namespace sw
