#include "pch.h"

#include "Core/Network/Connection/NetSendThrottle.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    expected<void> NetSendThrottle::validate( const NetSendThrottleConfig& config, int32 maxBytesPerSecond )
    {
        if ( maxBytesPerSecond < kNetMaxPacketSize )
            return unexpected( Error{ ErrorKind::InvalidArgument, "must be at least one packet (1200 bytes) per second", "_maxBytesPerSecond" } );
        if ( config._minBytesPerSecond < kNetMaxPacketSize || config._minBytesPerSecond > maxBytesPerSecond )
            return unexpected( Error{ ErrorKind::InvalidArgument, "must be between one packet (1200 bytes) and _maxBytesPerSecond", "_minBytesPerSecond" } );
        if ( config._increaseBytesPerSecond <= 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "must be greater than 0", "_increaseBytesPerSecond" } );
        if ( ( config._decreaseFactor > 0.0 && config._decreaseFactor < 1.0 ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "must be in (0, 1)", "_decreaseFactor" } );
        if ( ( config._lossThreshold > 0.0 && config._lossThreshold < 1.0 ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "must be in (0, 1)", "_lossThreshold" } );
        if ( ( config._maxQueueDelaySeconds > 0.0 ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "must be greater than 0", "_maxQueueDelaySeconds" } );
        if ( ( config._adjustIntervalSeconds > 0.0 ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "must be greater than 0", "_adjustIntervalSeconds" } );
        return {};
    }

    void NetSendThrottle::update( float64 nowSeconds, const NetSendThrottleConfig& config, int32 maxBytesPerSecond, const NetConnectionStats& stats )
    {
        const float64 maxRate = static_cast<float64>( maxBytesPerSecond );
        const float64 rtt     = static_cast<float64>( stats._rtt );
        if ( _lastAdjustTime < 0.0 )
            _windowStartTime = nowSeconds;
        if ( nowSeconds - _windowStartTime >= kMinRttWindowSeconds )
        {
            _previousMinRtt  = _minRtt;
            _minRtt          = 0.0;
            _windowStartTime = nowSeconds;
        }
        if ( rtt > 0.0 && ( _minRtt <= 0.0 || rtt < _minRtt ) )
            _minRtt = rtt;
        if ( _lastAdjustTime < 0.0 )
        {
            _bytesPerSecond = maxRate;
            _lastAdjustTime = nowSeconds;
            return;
        }
        if ( nowSeconds - _lastAdjustTime < config._adjustIntervalSeconds )
            return;
        _lastAdjustTime             = nowSeconds;
        const bool    bLossy        = static_cast<float64>( stats._packetLoss ) > config._lossThreshold;
        const float64 baseRtt       = _previousMinRtt > 0.0 ? MathUtil::min( _minRtt, _previousMinRtt ) : _minRtt;
        const bool    bQueueGrowing = _minRtt > 0.0 && rtt - baseRtt > config._maxQueueDelaySeconds;
        if ( bLossy || bQueueGrowing )
            _bytesPerSecond = MathUtil::max( static_cast<float64>( config._minBytesPerSecond ), _bytesPerSecond * config._decreaseFactor );
        else
            _bytesPerSecond = MathUtil::min( maxRate, _bytesPerSecond + static_cast<float64>( config._increaseBytesPerSecond ) );
    }
} // namespace sw
