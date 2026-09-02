#include "pch.h"

#include "Engine/Input/Devices/MouseDevice.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    MouseDevice::MouseDevice()
        : _mouseX{ 0 }
        , _mouseY{ 0 }
        , _prevMouseX{ 0 }
        , _prevMouseY{ 0 }
        , _deltaX{ 0 }
        , _deltaY{ 0 }
        , _rawDeltaX{ 0.0f }
        , _rawDeltaY{ 0.0f }
        , _smoothDeltaX{ 0.0f }
        , _smoothDeltaY{ 0.0f }
        , _smoothingFactor{ 0.0f }
        , _accelerationPower{ 1.0f }
        , _accumulatedRawDx{ 0.0f }
        , _accumulatedRawDy{ 0.0f }
        , _mouseWheelDelta{ 0.0f }
        , _mouseWheelAccum{ 0.0f }
        , _mouseWheelHorizontalDelta{ 0.0f }
        , _mouseWheelHorizontalAccum{ 0.0f }
        , _clipSubRectLeft{ 0 }
        , _clipSubRectTop{ 0 }
        , _clipSubRectRight{ 0 }
        , _clipSubRectBottom{ 0 }
        , _lockMode{ MouseLockMode::None }
        , _buttonMask{ 0 }
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
        resetState();
    }

    void MouseDevice::poll( [[maybe_unused]] float32 deltaTime )
    {
        float32 curDx = _rawDeltaX;
        float32 curDy = _rawDeltaY;
        if ( curDx == 0.0f && curDy == 0.0f )
        {
            curDx = static_cast<float32>( _deltaX );
            curDy = static_cast<float32>( _deltaY );
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
            _smoothDeltaX       = _smoothDeltaX * _smoothingFactor + curDx * alpha;
            _smoothDeltaY       = _smoothDeltaY * _smoothingFactor + curDy * alpha;
        }
        else
        {
            _smoothDeltaX = curDx;
            _smoothDeltaY = curDy;
        }
    }

    void MouseDevice::onFrameBegin( [[maybe_unused]] float32 deltaTime )
    {
        _pressedMask       = 0;
        _releasedMask      = 0;
        _bAnyButtonPressed = SW_FALSE;

        _deltaX                    = _mouseX - _prevMouseX;
        _deltaY                    = _mouseY - _prevMouseY;
        _prevMouseX                = _mouseX;
        _prevMouseY                = _mouseY;
        _rawDeltaX                 = 0.0f;
        _rawDeltaY                 = 0.0f;
        _mouseWheelDelta           = 0.0f;
        _mouseWheelAccum           = 0.0f;
        _mouseWheelHorizontalDelta = 0.0f;
        _mouseWheelHorizontalAccum = 0.0f;

        _bPointerEntered = SW_FALSE;
        _bPointerLeft    = SW_FALSE;
    }

    void MouseDevice::onFrameEnd()
    {
        _prevMouseX                = _mouseX;
        _prevMouseY                = _mouseY;
        _pressedMask               = 0;
        _releasedMask              = 0;
        _bAnyButtonPressed         = SW_FALSE;
        _rawDeltaX                 = 0.0f;
        _rawDeltaY                 = 0.0f;
        _mouseWheelDelta           = 0.0f;
        _mouseWheelHorizontalDelta = 0.0f;
    }

    void MouseDevice::resetState()
    {
        _buttonMask                = 0;
        _pressedMask               = 0;
        _releasedMask              = 0;
        _bAnyButtonPressed         = SW_FALSE;
        _deltaX                    = 0;
        _deltaY                    = 0;
        _rawDeltaX                 = 0.0f;
        _rawDeltaY                 = 0.0f;
        _smoothDeltaX              = 0.0f;
        _smoothDeltaY              = 0.0f;
        _accumulatedRawDx          = 0.0f;
        _accumulatedRawDy          = 0.0f;
        _mouseWheelDelta           = 0.0f;
        _mouseWheelAccum           = 0.0f;
        _mouseWheelHorizontalDelta = 0.0f;
        _mouseWheelHorizontalAccum = 0.0f;
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
            return _smoothDeltaX;
        if ( controlIndex == 103 ) // Smooth Delta Y
            return _smoothDeltaY;
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
            {
                _releasedMask |= bit;
            }
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
            _smoothDeltaX       = _smoothDeltaX * _smoothingFactor + curDx * alpha;
            _smoothDeltaY       = _smoothDeltaY * _smoothingFactor + curDy * alpha;
        }
        else
        {
            _smoothDeltaX = curDx;
            _smoothDeltaY = curDy;
        }
    }

    void MouseDevice::setPosition( int32 x, int32 y )
    {
        _deltaX = x - _prevMouseX;
        _deltaY = y - _prevMouseY;
        _mouseX = x;
        _mouseY = y;
        updateSmoothDelta( static_cast<float32>( _deltaX ), static_cast<float32>( _deltaY ) );
    }

    void MouseDevice::addRawDelta( float32 dx, float32 dy )
    {
        _rawDeltaX += dx;
        _rawDeltaY += dy;
        updateSmoothDelta( dx, dy );
    }

    void MouseDevice::addWheelDelta( float32 delta )
    {
        _mouseWheelDelta += delta;
        _mouseWheelAccum += delta;
    }

    void MouseDevice::addHorizontalWheelDelta( float32 delta )
    {
        _mouseWheelHorizontalDelta += delta;
        _mouseWheelHorizontalAccum += delta;
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
