#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameMode/GameSpeedControl.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    GameSpeedControl::GameSpeedControl()
        : _timeScale{ 1.0f }
        , _bPaused{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void GameSpeedControl::applyInput( bool bPauseTriggered, bool bFasterTriggered, bool bSlowerTriggered )
    {
        if ( bPauseTriggered )
        {
            _bPaused = _bPaused == SW_TRUE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO_ID( "GameSpeedControl.PauseToggled", "[Speed] %#", _bPaused == SW_TRUE ? "paused" : "running" );
        }
        if ( bFasterTriggered || bSlowerTriggered )
        {
            _timeScale = MathUtil::clamp( bFasterTriggered ? _timeScale * kStepFactor : _timeScale / kStepFactor, kMinTimeScale, kMaxTimeScale );
            SW_LOG_INFO_ID( "GameSpeedControl.SpeedX", "[Speed] speed x%#", _timeScale );
        }
    }

    bool GameSpeedControl::setTimeScale( float32 timeScale )
    {
        if ( ( kMinTimeScale <= timeScale && timeScale <= kMaxTimeScale ) == false )
        {
            SW_LOG_ERROR_ID( "GameSpeedControl.TimeScaleRefused", "[Speed] refused time scale %# (needs %#..%#)", timeScale, kMinTimeScale, kMaxTimeScale );
            return false;
        }
        _timeScale = timeScale;
        return true;
    }

    bool GameSpeedControl::restore( float32 timeScale, bool bPaused )
    {
        if ( setTimeScale( timeScale ) == false )
            return false;
        _bPaused = bPaused ? SW_TRUE : SW_FALSE;
        return true;
    }
} // namespace sw
