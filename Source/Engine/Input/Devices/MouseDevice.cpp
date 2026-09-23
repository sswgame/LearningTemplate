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
        // **여기서 원시 델타는 항상 0 이다.** `InputManager::beginFrame` 이 같은 루프에서
        // `onFrameBegin()` 을 부른 **바로 다음에** 이것을 부르는데, `onFrameBegin` 이 `_rawDelta` 를
        // 비우기 때문이다. 예전에는 "원시 델타가 있으면 그것을, 없으면 위치 차이를" 이라고 적혀
        // 있었지만 앞 갈래는 **한 번도 실행되지 않았다.** 읽는 사람만 원시 입력이 여기서 반영된다고
        // 믿게 만들었다. 이번 프레임의 원시 이벤트는 그 뒤 디스패치에서 `addRawDelta` 가 반영한다.
        //
        // 그래서 이 함수가 하는 일은 하나다: **프레임 시작 시점의 위치 차이를 스무딩에 흘려 넣는다.**
        // 그 덕에 마우스를 멈추면 델타가 0 으로 돌아온다. 이것이 없으면 `getSmoothDelta()` 가
        // 마지막 움직임을 영원히 보고하고, 그 값으로 도는 카메라는 계속 돈다.
        //
        // 가속 · 스무딩 식은 `updateSmoothDelta` 한 곳에만 둔다. 여기 사본이 있으면 감각을 조정하는
        // 사람이 한쪽만 고쳐 **입력 경로에 따라 다르게 움직인다.**
        updateSmoothDelta( static_cast<float32>( _delta._x ), static_cast<float32>( _delta._y ) );
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

        // **(0,0) 은 "이 이벤트에 원시 성분이 없다" 는 뜻이지 "멈췄다" 가 아니다.** 여기서 스무딩까지
        // 갱신하면 같은 이벤트에서 바로 앞에 불린 `setPosition` 이 계산해 둔 델타를 덮어쓴다.
        // 스무딩이 꺼져 있으면(기본값 0) 곧장 0 이 되어 `getSmoothDelta()` 가 **영원히 0** 이었다.
        // Win32 · X11 둘 다 `makeMouseMove( x, y )` 를 원시 성분 없이 올리므로 기본 구성이 그 상태였다.
        // "멈추면 0 으로 돌아온다" 는 프레임당 한 번 도는 `poll()` 의 몫이다.
        if ( dx == 0.0f && dy == 0.0f )
            return;

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
