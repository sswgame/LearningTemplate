#include "pch.h"

#include "Engine/Renderer/Frame/View/RenderScaleAdjuster.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    string RenderScaleAdjuster::validate( const RenderScaleAdjusterConfig& config )
    {
        string reason;
        if ( config._budgetMs <= 0.0f )
            reason = "budgetMs must be > 0";
        else if ( config._minScale <= 0.0f || config._minScale > 1.0f )
            reason = "minScale must be in (0, 1]";
        else if ( config._stepDown <= 0.0f || config._stepUp <= 0.0f )
            reason = "stepDown and stepUp must be > 0";
        else if ( config._stepUp >= config._stepDown )
            reason = "stepUp must be smaller than stepDown (otherwise the scale oscillates)";
        else if ( config._overRatio < 1.0f )
            reason = "overRatio must be >= 1";
        else if ( config._headroomRatio <= 0.0f || config._headroomRatio >= 1.0f )
            reason = "headroomRatio must be in (0, 1)";
        else if ( config._downFrames == 0 || config._upFrames == 0 )
            reason = "downFrames and upFrames must be >= 1";
        return reason;
    }

    bool RenderScaleAdjuster::configure( const RenderScaleAdjusterConfig& config )
    {
        if ( validate( config ).empty() == false )
            return false;
        _config      = config;
        _bConfigured = true;
        reset();
        return true;
    }

    void RenderScaleAdjuster::reset()
    {
        _scale      = 1.0f;
        _overCount  = 0;
        _underCount = 0;
    }

    float32 RenderScaleAdjuster::update( float32 frameMs )
    {
        if ( _bConfigured == false )
            return 1.0f;
        const bool bOver  = frameMs > _config._budgetMs * _config._overRatio;
        const bool bUnder = frameMs < _config._budgetMs * _config._headroomRatio;
        _overCount        = bOver ? _overCount + 1 : 0;
        _underCount       = bUnder ? _underCount + 1 : 0;
        if ( _overCount >= _config._downFrames )
        {
            _scale      = MathUtil::max( _config._minScale, _scale - _config._stepDown );
            _overCount  = 0;
            _underCount = 0;
        }
        else if ( _underCount >= _config._upFrames )
        {
            _scale      = MathUtil::min( 1.0f, _scale + _config._stepUp );
            _overCount  = 0;
            _underCount = 0;
        }
        return _scale;
    }
} // namespace sw
