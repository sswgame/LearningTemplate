#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameMode/GameSpeedControl.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    GameSpeedControl::GameSpeedControl()
        : _range{}
        , _timeScale{ 1.0f }
        , _bPaused{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool GameSpeedControl::initialize( const GameSpeedRange& range )
    {
        if ( range.isValid() == false )
        {
            SW_LOG_ERROR_ID( "GameSpeedControl.RangeInvalid", "[Speed] refused time scale range %#..%# step %# (needs 0 < min <= 1 <= max and step > 1)", range._minTimeScale, range._maxTimeScale,
                             range._stepFactor );
            return false;
        }
        _range = range;
        if ( ( _range._minTimeScale <= _timeScale && _timeScale <= _range._maxTimeScale ) == false )
            _timeScale = 1.0f;
        return true;
    }

    void GameSpeedControl::applyInput( bool bPauseTriggered, bool bFasterTriggered, bool bSlowerTriggered )
    {
        if ( bPauseTriggered )
        {
            _bPaused = _bPaused == SW_TRUE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO_ID( "GameSpeedControl.PauseToggled", "[Speed] %#", _bPaused == SW_TRUE ? "paused" : "running" );
        }
        if ( ( bFasterTriggered || bSlowerTriggered ) && _range.isValid() )
        {
            _timeScale = MathUtil::clamp( bFasterTriggered ? _timeScale * _range._stepFactor : _timeScale / _range._stepFactor, _range._minTimeScale, _range._maxTimeScale );
            SW_LOG_INFO_ID( "GameSpeedControl.SpeedX", "[Speed] speed x%#", _timeScale );
        }
    }

    bool GameSpeedControl::setTimeScale( float32 timeScale )
    {
        if ( ( _range._minTimeScale <= timeScale && timeScale <= _range._maxTimeScale ) == false )
        {
            SW_LOG_ERROR_ID( "GameSpeedControl.TimeScaleRefused", "[Speed] refused time scale %# (needs %#..%#)", timeScale, _range._minTimeScale, _range._maxTimeScale );
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
