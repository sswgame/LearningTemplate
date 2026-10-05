#include "pch.h"

#include "Core/Network/Replication/NetClock.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    NetClock::NetClock()
        : _settings{}
        , _serverTick{ 0.0f }
        , _renderTick{ kNoRenderTick }
        , _newestTick{ 0 }
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
        _newestTick     = 0;
        _bHasServerTick = SW_FALSE;
    }

    float32 NetClock::getTickInterval() const { return MathUtil::max( 1.0e-4f, _settings._tickInterval ); }

    float32 NetClock::computeInterpolationDelay() const { return MathUtil::max( _settings._interpolationDelay, _settings._sampleInterval * kSampleIntervalsBehind ); }

    void NetClock::observeServerTick( uint32 serverTick )
    {
        const float32 tick = static_cast<float32>( serverTick );
        if ( _bHasServerTick == SW_FALSE )
        {
            _serverTick     = tick;
            _renderTick     = tick - computeInterpolationDelay() / getTickInterval();
            _newestTick     = serverTick;
            _bHasServerTick = SW_TRUE;
            return;
        }
        _newestTick = MathUtil::max( _newestTick, serverTick );
        _serverTick = MathUtil::max( _serverTick, tick );
    }

    void NetClock::advance( float32 deltaTime )
    {
        if ( _bHasServerTick == SW_FALSE || deltaTime <= 0.0f )
            return;
        // 추정은 흐르는 시간만큼 앞으로 가되 받은 가장 새 틱 + 지연을 넘지 않는다(렌더 틱이 받은 틱을 지나치지 않게). 렌더 틱은 뒤로 가지 않는다 — 지연이 커져도
        // (표본 간격이 늘었다) · 받은 틱이 끊겨도 멈춰 기다린다.
        const float32 tickInterval = getTickInterval();
        const float32 delayTicks   = computeInterpolationDelay() / tickInterval;
        const float32 ceiling      = static_cast<float32>( _newestTick ) + delayTicks;
        _serverTick                = MathUtil::min( _serverTick + deltaTime / tickInterval, ceiling );
        _renderTick                = MathUtil::max( _renderTick, _serverTick - delayTicks );
    }
} // namespace sw
