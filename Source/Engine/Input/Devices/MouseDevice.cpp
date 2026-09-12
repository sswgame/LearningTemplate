#include "pch.h"

#include "Engine/Input/Devices/MouseDevice.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    MouseDevice::MouseDevice()
        : _mouse{}
        , _prevMouse{}
        , _delta{}
        , _rawDelta{}
        , _smoothDelta{}
        , _smoothingFactor{ 0.0f }
        , _accelerationPower{ 1.0f }
        , _mouseWheelDelta{ 0.0f }
        , _mouseWheelHorizontalDelta{ 0.0f }
        , _clipSubRectLeft{ 0 }
        , _clipSubRectTop{ 0 }
        , _clipSubRectRight{ 0 }
        , _clipSubRectBottom{ 0 }
        , _lockMode{ MouseLockMode::None }
        , _buttonMask{ SW_FALSE }
        , _pressedMask{ 0 }
        , _releasedMask{ 0 }
        , _bCursorVisible{ SW_TRUE }
        , _bPointerInside{ SW_FALSE }
        , _bPointerEntered{ SW_FALSE }
        , _bPointerLeft{ SW_FALSE }
        , _bAnyButtonPressed{ SW_FALSE }
        , _bHasSubRect{ SW_FALSE }
        , _reserved{ 0 }
    {
        MouseDevice::resetState();
    }

    void MouseDevice::poll( [[maybe_unused]] float32 deltaTime )
    {
        float32 curDx = _rawDelta._x;
        float32 curDy = _rawDelta._y;
        if ( curDx == 0.0f && curDy == 0.0f )
        {
            curDx = static_cast<float32>( _delta._x );
            curDy = static_cast<float32>( _delta._y );
        }

        if ( _accelerationPower > 1.0f )
        {
            const float32 speed = MathUtil::sqrt( curDx * curDx + curDy * curDy );
            if ( speed > 1.0f )
            {
                const float32 factor = MathUtil::pow( speed, _accelerationPower - 1.0f );
                curDx *= factor;
                curDy *= factor;
            }
        }

        if ( _smoothingFactor > 0.0f )
        {
            const float32 alpha = 1.0f - _smoothingFactor;
            _smoothDelta._x     = _smoothDelta._x * _smoothingFactor + curDx * alpha;
            _smoothDelta._y     = _smoothDelta._y * _smoothingFactor + curDy * alpha;
        }
        else
        {
            _smoothDelta._x = curDx;
            _smoothDelta._y = curDy;
        }
    }

    void MouseDevice::onFrameBegin( [[maybe_unused]] float32 deltaTime )
    {
        _pressedMask       = 0;
        _releasedMask      = 0;
        _bAnyButtonPressed = SW_FALSE;

        _delta._x                  = _mouse._x - _prevMouse._x;
        _delta._y                  = _mouse._y - _prevMouse._y;
        _prevMouse._x              = _mouse._x;
        _prevMouse._y              = _mouse._y;
        _rawDelta._x               = 0.0f;
        _rawDelta._y               = 0.0f;
        _mouseWheelDelta           = 0.0f;
        _mouseWheelHorizontalDelta = 0.0f;

        _bPointerEntered = SW_FALSE;
        _bPointerLeft    = SW_FALSE;
    }

    void MouseDevice::onFrameEnd()
    {
        _prevMouse._x              = _mouse._x;
        _prevMouse._y              = _mouse._y;
        _pressedMask               = 0;
        _releasedMask              = 0;
        _bAnyButtonPressed         = SW_FALSE;
        _rawDelta._x               = 0.0f;
        _rawDelta._y               = 0.0f;
        _mouseWheelDelta           = 0.0f;
        _mouseWheelHorizontalDelta = 0.0f;
    }

    void MouseDevice::resetState()
    {
        _buttonMask                = SW_FALSE;
        _pressedMask               = 0;
        _releasedMask              = 0;
        _bAnyButtonPressed         = SW_FALSE;
        _delta._x                  = 0;
        _delta._y                  = 0;
        _rawDelta._x               = 0.0f;
        _rawDelta._y               = 0.0f;
        _smoothDelta._x            = 0.0f;
        _smoothDelta._y            = 0.0f;
        _mouseWheelDelta           = 0.0f;
        _mouseWheelHorizontalDelta = 0.0f;
    }

    bool MouseDevice::isControlDown( uint16 controlIndex ) const
    {
        if ( controlIndex >= kButtonCount )
            return false;
        return isButtonDown( static_cast<MouseButton>( controlIndex ) );
    }

    bool MouseDevice::wasControlPressed( uint16 controlIndex ) const
    {
        if ( controlIndex >= kButtonCount )
            return false;
        return wasButtonPressed( static_cast<MouseButton>( controlIndex ) );
    }

    bool MouseDevice::wasControlReleased( uint16 controlIndex ) const
    {
        if ( controlIndex >= kButtonCount )
            return false;
        return wasButtonReleased( static_cast<MouseButton>( controlIndex ) );
    }

    float32 MouseDevice::getControlValue( uint16 controlIndex ) const
    {
        if ( controlIndex == 100 ) // Wheel
            return _mouseWheelDelta;
        if ( controlIndex == 101 ) // Horizontal Wheel
            return _mouseWheelHorizontalDelta;
        if ( controlIndex == 102 ) // Smooth Delta X
            return _smoothDelta._x;
        if ( controlIndex == 103 ) // Smooth Delta Y
            return _smoothDelta._y;
        return isControlDown( controlIndex ) ? 1.0f : 0.0f;
    }

    bool MouseDevice::isButtonDown( MouseButton button ) const
    {
        const size_t index = static_cast<size_t>( button );
        return index < kButtonCount ? ( ( _buttonMask & ( 1u << index ) ) != 0 ) : false;
    }

    bool MouseDevice::wasButtonPressed( MouseButton button ) const
    {
        const size_t index = static_cast<size_t>( button );
        return index < kButtonCount ? ( ( _pressedMask & ( 1u << index ) ) != 0 ) : false;
    }

    bool MouseDevice::wasButtonReleased( MouseButton button ) const
    {
        const size_t index = static_cast<size_t>( button );
        return index < kButtonCount ? ( ( _releasedMask & ( 1u << index ) ) != 0 ) : false;
    }

    void MouseDevice::setButtonDown( MouseButton button, bool bDown )
    {
        const size_t index = static_cast<size_t>( button );
        if ( index >= kButtonCount )
            return;

        const uint8 bit      = static_cast<uint8>( 1u << index );
        const bool  bWasDown = ( _buttonMask & bit ) != 0;

        if ( bDown )
        {
            _buttonMask |= bit;
            if ( bWasDown == false )
            {
                _pressedMask |= bit;
                _bAnyButtonPressed = SW_TRUE;
            }
        }
        else
        {
            _buttonMask &= ~bit;
            if ( bWasDown )
                _releasedMask |= bit;
        }
    }

    void MouseDevice::updateSmoothDelta( float32 dx, float32 dy )
    {
        float32 curDx = dx;
        float32 curDy = dy;
        if ( _accelerationPower > 1.0f )
        {
            const float32 speed = MathUtil::sqrt( curDx * curDx + curDy * curDy );
            if ( speed > 1.0f )
            {
                const float32 factor = MathUtil::pow( speed, _accelerationPower - 1.0f );
                curDx *= factor;
                curDy *= factor;
            }
        }

        if ( _smoothingFactor > 0.0f )
        {
            const float32 alpha = 1.0f - _smoothingFactor;
            _smoothDelta._x     = _smoothDelta._x * _smoothingFactor + curDx * alpha;
            _smoothDelta._y     = _smoothDelta._y * _smoothingFactor + curDy * alpha;
        }
        else
        {
            _smoothDelta._x = curDx;
            _smoothDelta._y = curDy;
        }
    }

    void MouseDevice::setPosition( int32 x, int32 y )
    {
        _delta._x = x - _prevMouse._x;
        _delta._y = y - _prevMouse._y;
        _mouse._x = x;
        _mouse._y = y;
        updateSmoothDelta( static_cast<float32>( _delta._x ), static_cast<float32>( _delta._y ) );
    }

    void MouseDevice::addRawDelta( float32 dx, float32 dy )
    {
        _rawDelta._x += dx;
        _rawDelta._y += dy;
        updateSmoothDelta( dx, dy );
    }

    void MouseDevice::addWheelDelta( float32 delta )
    {
        _mouseWheelDelta += delta;
    }

    void MouseDevice::addHorizontalWheelDelta( float32 delta )
    {
        _mouseWheelHorizontalDelta += delta;
    }

    void MouseDevice::setPointerInsideState( bool bInside )
    {
        const bool bWasInside = _bPointerInside == SW_TRUE;
        _bPointerInside       = bInside ? SW_TRUE : SW_FALSE;

        if ( bInside && ( bWasInside == false ) )
            _bPointerEntered = SW_TRUE;
        else if ( ( bInside == false ) && bWasInside )
            _bPointerLeft = SW_TRUE;
    }
} // namespace sw
