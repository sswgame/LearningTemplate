#include "pch.h"

#include "Core/Network/Replication/NetClock.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        constexpr float64 kNetClockRounding = 1.0e-3; ///< 프레임 길이를 더한 실수가 정수 틱 바로 아래에 걸린 것을 그 틱으로 본다
    } // namespace

    NetClock::NetClock()
        : _serverTick{ 0.0 }
        , _newestTick{ 0 }
        , _side{ NetClockSide::Replica }
        , _bObserved{ SW_FALSE }
    {
    }

    optional<uint32> NetClock::computeStride( float32 intervalSeconds )
    {
        constexpr float32 kStrideTolerance = 1.0e-2f;
        const float32     stride           = intervalSeconds / kTickSeconds;
        const float32     rounded          = MathUtil::round( stride );
        if ( rounded < 1.0f || MathUtil::abs( stride - rounded ) > kStrideTolerance )
            return nullopt;
        return static_cast<uint32>( rounded );
    }

    void NetClock::reset( NetClockSide side )
    {
        _serverTick = 0.0;
        _newestTick = 0;
        _side       = side;
        _bObserved  = SW_FALSE;
    }

    void NetClock::advance( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || hasServerTick() == false )
            return;
        const float64 elapsed = static_cast<float64>( deltaTime ) / static_cast<float64>( kTickSeconds );
        if ( _side == NetClockSide::Authority )
        {
            _serverTick += MathUtil::min( elapsed, static_cast<float64>( kMaxTicksPerAdvance ) );
            return;
        }
        const float64 ceiling = static_cast<float64>( _newestTick ) + static_cast<float64>( kMaxLeadSeconds / kTickSeconds );
        _serverTick           = MathUtil::max( _serverTick, MathUtil::min( _serverTick + elapsed, ceiling ) );
    }

    void NetClock::observe( uint32 serverTick )
    {
        if ( _side == NetClockSide::Authority )
            return;
        _newestTick = _bObserved == SW_TRUE ? MathUtil::max( _newestTick, serverTick ) : serverTick;
        _serverTick = MathUtil::max( _serverTick, static_cast<float64>( serverTick ) );
        _bObserved  = SW_TRUE;
    }

    uint32 NetClock::getWholeTick() const
    {
        return static_cast<uint32>( _serverTick + kNetClockRounding );
    }
} // namespace sw

namespace sw
{
    NetRenderClock::NetRenderClock()
        : _settings{}
        , _renderTick{ kNoRenderTick }
        , _newestTick{ 0 }
        , _bHasSample{ SW_FALSE }
        , _bRendering{ SW_FALSE }
    {
    }

    void NetRenderClock::initialize( const NetRenderClockConfig& settings )
    {
        SW_ASSERT( settings._stride > 0 );
        _settings = settings;
        reset();
    }

    void NetRenderClock::reset()
    {
        _renderTick = kNoRenderTick;
        _newestTick = 0;
        _bHasSample = SW_FALSE;
        _bRendering = SW_FALSE;
    }

    void NetRenderClock::observeSample( uint32 tick )
    {
        _newestTick = _bHasSample == SW_TRUE ? MathUtil::max( _newestTick, tick ) : tick;
        _bHasSample = SW_TRUE;
    }

    float32 NetRenderClock::computeInterpolationDelay() const
    {
        return MathUtil::max( _settings._interpolationDelay, _settings._sampleInterval * kSampleIntervalsBehind );
    }

    void NetRenderClock::follow( const NetClock& clock )
    {
        if ( _bHasSample == SW_FALSE || clock.hasServerTick() == false )
            return;
        const float64 delayTicks = static_cast<float64>( computeInterpolationDelay() / NetClock::kTickSeconds );
        const float64 behind     = ( clock.getServerTick() - delayTicks ) / static_cast<float64>( _settings._stride );
        const float64 target     = MathUtil::min( behind, static_cast<float64>( _newestTick ) ); // 받은 표본을 지나치지 않는다(내다보지 않는다)
        _renderTick              = _bRendering == SW_TRUE ? MathUtil::max( _renderTick, target ) : target;
        _bRendering              = SW_TRUE;
    }
} // namespace sw
