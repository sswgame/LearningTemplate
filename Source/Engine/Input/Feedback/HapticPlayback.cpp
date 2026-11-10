#include "pch.h"

#include "Engine/Input/Feedback/HapticPlayback.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Input/Feedback/HapticEffect.h"
#include "Engine/Input/Feedback/IInputFeedbackDevice.h"

namespace sw
{
    HapticPlayback::HapticPlayback()
        : _pEffect{}
        , _time{ 0.0f }
        , _scale{ 1.0f }
        , _sentValue{ 0.0f, 0.0f }
    {
    }

    bool HapticPlayback::start( shared_ptr<const HapticEffectDef> pEffect, float32 scale, IInputFeedbackDevice& output )
    {
        if ( pEffect == nullptr )
        {
            SW_LOG_ERROR_ID( "HapticPlayback.NoEffect", "A haptic effect was played without an effect (it failed to load) - nothing plays" );
            return false;
        }
        _pEffect           = std::move( pEffect );
        _time              = 0.0f;
        _scale             = MathUtil::clamp( scale, 0.0f, 1.0f );
        const float2 value = _pEffect->evaluate( 0.0f ) * _scale;
        if ( output.setVibration( value._x, value._y ) )
        {
            _sentValue = value;
            return true;
        }
        clear();
        return false;
    }

    void HapticPlayback::advance( float32 deltaSeconds, IInputFeedbackDevice& output )
    {
        if ( _pEffect == nullptr )
            return;
        _time += deltaSeconds;
        if ( _pEffect->_bLoop == false && _time >= _pEffect->getDuration() )
        {
            output.stopVibration();
            clear();
            return;
        }
        const float2 value = _pEffect->evaluate( _time ) * _scale;
        if ( value == _sentValue )
            return; // 고정 진동은 처음 한 번만 낸다(장치 호출을 프레임마다 되풀이하지 않는다)
        if ( output.setVibration( value._x, value._y ) )
            _sentValue = value; // 거절하는 장치(연결 끊김)는 다음 프레임에 다시 낸다
    }

    void HapticPlayback::clear()
    {
        _pEffect.reset();
        _time = 0.0f;
    }
} // namespace sw
