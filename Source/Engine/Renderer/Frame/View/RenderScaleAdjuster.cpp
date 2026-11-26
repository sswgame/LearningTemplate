#include "pch.h"

#include "Engine/Renderer/Frame/View/RenderScaleAdjuster.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    RenderScaleAdjuster::RenderScaleAdjuster()
        : _config{}
        , _scale{ 1.0f }
        , _overCount{ 0 }
        , _underCount{ 0 }
        , _lastSerial{ 0 }
        , _discardCount{ 0 }
        , _bConfigured{ SW_FALSE }
    {
    }

    bool RenderScaleAdjuster::configure( const RenderScaleAdjusterConfig& config )
    {
        if ( config.validate().has_value() == false )
            return false;
        _config      = config;
        _bConfigured = SW_TRUE;
        reset();
        return true;
    }

    void RenderScaleAdjuster::reset()
    {
        _scale        = 1.0f;
        _overCount    = 0;
        _underCount   = 0;
        _discardCount = 0;
    }

    bool RenderScaleAdjuster::acceptClockState( GPUFrameClockState state )
    {
        if ( state != GPUFrameClockState::Unsupported )
            return true;
        reset();
        return false;
    }

    float32 RenderScaleAdjuster::update( const GPUFrameSample& sample )
    {
        if ( _bConfigured == false )
            return 1.0f;
        // 표본은 몇 프레임 늦고 같은 것이 여러 프레임 이어진다 — 순번이 바뀐 것만 센다.
        if ( sample._serial == 0 || sample._serial == _lastSerial )
            return _scale;
        _lastSerial = sample._serial;
        // 단계를 바꾼 직후의 표본은 옛 배율로 그린 프레임이다.
        if ( _discardCount > 0 )
        {
            --_discardCount;
            return _scale;
        }
        const float32 frameMs = sample._gpuMs;
        const bool    bOver   = frameMs > _config._budgetMs * _config._overRatio;
        const bool    bUnder  = frameMs < _config._budgetMs * _config._headroomRatio;
        _overCount            = bOver ? _overCount + 1 : 0;
        _underCount           = bUnder ? _underCount + 1 : 0;
        if ( _overCount >= _config._downFrames )
        {
            _scale        = MathUtil::max( _config._minScale, _scale - _config._stepDown );
            _overCount    = 0;
            _underCount   = 0;
            _discardCount = _config._discardSamples;
        }
        else if ( _underCount >= _config._upFrames )
        {
            _scale        = MathUtil::min( 1.0f, _scale + _config._stepUp );
            _overCount    = 0;
            _underCount   = 0;
            _discardCount = _config._discardSamples;
        }
        return _scale;
    }
} // namespace sw
