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
        , _smoothVelocity{}
        , _smoothingFactor{ 0.0f }
        , _accelerationPower{ 1.0f }
        , _mouseWheelDelta{ 0.0f }
        , _mouseWheelHorizontalDelta{ 0.0f }
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
        , _bHasSubRect{ SW_FALSE }
        , _reserved{ 0 }
    {
        MouseDevice::resetState();
    }

    void MouseDevice::poll( [[maybe_unused]] float32 deltaTime )
    {
        // 마우스는 폴링하지 않는다 — 이번 프레임 이동은 이벤트가 쌓고(`setPosition` · `addRawDelta`), 스무딩은 이벤트를 다 적용한 뒤
        // `onEventsDispatched` 가 한 번 건다.
    }

    void MouseDevice::onEventsDispatched( float32 deltaTime )
    {
        // 흐른 시간이 없는 프레임(0 이나 음수로 부른 시험 · 재동기화)은 기준 시간 한 칸으로 친다.
        const float32 frameSeconds = deltaTime > 0.0f ? deltaTime : kSmoothingReferenceSeconds;
        const float2  movement     = getMovementDelta();
        float32       velocityX    = movement._x / frameSeconds;
        float32       velocityY    = movement._y / frameSeconds;

        if ( _accelerationPower > 1.0f )
        {
            const float32 speedPerReference = MathUtil::sqrt( velocityX * velocityX + velocityY * velocityY ) * kSmoothingReferenceSeconds;
            if ( speedPerReference > 1.0f )
            {
                const float32 factor = MathUtil::pow( speedPerReference, _accelerationPower - 1.0f );
                velocityX *= factor;
                velocityY *= factor;
            }
        }

        if ( _smoothingFactor > 0.0f )
        {
            // 1 - exp( -dt / τ ) 를 설정값으로 바로 쓴다: exp( -dt / τ ) = factor^( dt / 기준 시간 ).
            const float32 alpha = 1.0f - MathUtil::pow( _smoothingFactor, frameSeconds / kSmoothingReferenceSeconds );
            _smoothVelocity._x += ( velocityX - _smoothVelocity._x ) * alpha;
            _smoothVelocity._y += ( velocityY - _smoothVelocity._y ) * alpha;
        }
        else
        {
            _smoothVelocity._x = velocityX;
            _smoothVelocity._y = velocityY;
        }

        _smoothDelta._x = _smoothVelocity._x * frameSeconds;
        _smoothDelta._y = _smoothVelocity._y * frameSeconds;
    }

    float2 MouseDevice::getMovementDelta() const
    {
        if ( _rawDelta._x != 0.0f || _rawDelta._y != 0.0f )
            return _rawDelta;
        return float2{ static_cast<float32>( _delta._x ), static_cast<float32>( _delta._y ) };
    }

    float32 MouseDevice::getSmoothingTimeConstant() const
    {
        if ( _smoothingFactor <= 0.0f )
            return 0.0f;
        return -kSmoothingReferenceSeconds / MathUtil::log( _smoothingFactor );
    }

    void MouseDevice::onFrameBegin( [[maybe_unused]] float32 deltaTime )
    {
        _pressedMask  = 0;
        _releasedMask = 0;

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
        _rawDelta._x               = 0.0f;
        _rawDelta._y               = 0.0f;
        _mouseWheelDelta           = 0.0f;
        _mouseWheelHorizontalDelta = 0.0f;
    }

    void MouseDevice::resetState()
    {
        _buttonMask                = 0;
        _pressedMask               = 0;
        _releasedMask              = 0;
        _delta._x                  = 0;
        _delta._y                  = 0;
        _rawDelta._x               = 0.0f;
        _rawDelta._y               = 0.0f;
        _smoothDelta._x            = 0.0f;
        _smoothDelta._y            = 0.0f;
        _smoothVelocity._x         = 0.0f;
        _smoothVelocity._y         = 0.0f;
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
        if ( controlIndex == 100 ) // 세로 휠
            return _mouseWheelDelta;
        if ( controlIndex == 101 ) // 가로 휠
            return _mouseWheelHorizontalDelta;
        if ( controlIndex == 102 ) // 스무딩 델타 X
            return _smoothDelta._x;
        if ( controlIndex == 103 ) // 스무딩 델타 Y
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
                _pressedMask |= bit;
        }
        else
        {
            _buttonMask &= ~bit;
            if ( bWasDown )
                _releasedMask |= bit;
        }
    }

    void MouseDevice::setPosition( int32 x, int32 y )
    {
        _delta._x = x - _prevMouse._x;
        _delta._y = y - _prevMouse._y;
        _mouse._x = x;
        _mouse._y = y;
    }

    void MouseDevice::setPositionWithoutDelta( int32 x, int32 y )
    {
        // 이번 프레임의 `_delta` 는 이미 계산돼 있다. 기준점(`_prevMouse`)도 함께 옮겨야 다음 이동이 되돌린 자리에서부터 잰다.
        _mouse._x     = x;
        _mouse._y     = y;
        _prevMouse._x = x;
        _prevMouse._y = y;
    }

    void MouseDevice::addRawDelta( float32 dx, float32 dy )
    {
        _rawDelta._x += dx;
        _rawDelta._y += dy;
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
