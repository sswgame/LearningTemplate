#include "pch.h"

#include "Engine/Input/Virtual/VirtualCursor.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/Devices/GamepadDevice.h"

namespace sw
{
    void VirtualCursor::setEnabled( bool bEnabled )
    {
        _bEnabled          = bEnabled;
        _bEmittedLastFrame = false;
        // 켜고 끌 때 위치와 버튼 상태를 새로 잡는다. 꺼진 동안 누른 패드 버튼이 켜는 순간 "눌림" 으로 나오지 않게 한다.
        _bHasEmitted    = false;
        _bPrimaryDown   = false;
        _bSecondaryDown = false;
    }

    float2 VirtualCursor::computeVelocity( float2 stick ) const
    {
        const float32 magnitude = MathUtil::sqrt( stick._x * stick._x + stick._y * stick._y );
        if ( magnitude <= _settings._deadzone || magnitude <= 0.0f )
            return float2{ 0.0f, 0.0f };

        const float32 span = 1.0f - _settings._deadzone;
        float32       norm = span > 0.0001f ? MathUtil::clamp( ( magnitude - _settings._deadzone ) / span, 0.0f, 1.0f ) : 1.0f;
        if ( _settings._exponent > 0.0f && _settings._exponent != 1.0f )
            norm = MathUtil::pow( norm, _settings._exponent );
        const float32 pixelsPerSecond = norm * _settings._speed;
        return float2{ ( stick._x / magnitude ) * pixelsPerSecond, -( stick._y / magnitude ) * pixelsPerSecond };
    }

    void VirtualCursor::emitFrame( const GamepadDevice* pPad, int2 mousePosition, int2 clientSize, float32 deltaSeconds, vector<RawInputEvent>& outListEvent )
    {
        _bEmittedLastFrame = false;
        if ( _bEnabled == false || pPad == nullptr || pPad->isConnected() == false || clientSize._x <= 0 || clientSize._y <= 0 )
            return;

        // 실제 마우스가 움직였으면(지난 프레임에 낸 위치와 다르면) 거기서 이어 움직인다.
        if ( _bHasEmitted == false || mousePosition != _lastEmitted )
            _position = mousePosition.toFloat2();
        const size_t eventCountBefore = outListEvent.size();

        const float2 stick    = _settings._bRightStick ? pPad->getRightStick() : pPad->getLeftStick();
        const float2 velocity = computeVelocity( stick );
        int2         target   = mousePosition;
        if ( velocity._x != 0.0f || velocity._y != 0.0f )
        {
            _position._x = MathUtil::clamp( _position._x + velocity._x * deltaSeconds, 0.0f, static_cast<float32>( clientSize._x - 1 ) );
            _position._y = MathUtil::clamp( _position._y + velocity._y * deltaSeconds, 0.0f, static_cast<float32>( clientSize._y - 1 ) );
            target       = int2{ static_cast<int32>( MathUtil::round( _position._x ) ), static_cast<int32>( MathUtil::round( _position._y ) ) };
            if ( target != mousePosition )
                outListEvent.push_back( RawInputEvent::makeMouseMove( target._x, target._y ) );
        }

        const bool bPrimaryDown = pPad->isButtonDown( _settings._primaryButton );
        if ( bPrimaryDown != _bPrimaryDown )
        {
            outListEvent.push_back( bPrimaryDown ? RawInputEvent::makeMouseButtonDown( MouseButton::Left, target._x, target._y )
                                                 : RawInputEvent::makeMouseButtonUp( MouseButton::Left, target._x, target._y ) );
            _bPrimaryDown = bPrimaryDown;
        }
        const bool bSecondaryDown = pPad->isButtonDown( _settings._secondaryButton );
        if ( bSecondaryDown != _bSecondaryDown )
        {
            outListEvent.push_back( bSecondaryDown ? RawInputEvent::makeMouseButtonDown( MouseButton::Right, target._x, target._y )
                                                   : RawInputEvent::makeMouseButtonUp( MouseButton::Right, target._x, target._y ) );
            _bSecondaryDown = bSecondaryDown;
        }

        _lastEmitted       = target;
        _bHasEmitted       = true;
        _bEmittedLastFrame = outListEvent.size() > eventCountBefore;
    }
} // namespace sw
