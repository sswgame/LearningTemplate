#include "pch.h"

#include "Engine/Input/Devices/TouchDevice.h"

namespace sw
{
    TouchDevice::TouchDevice()
        : _arrPosition{}
        , _downMask{ 0 }
        , _pressedMask{ 0 }
        , _releasedMask{ 0 }
    {
    }

    void TouchDevice::poll( [[maybe_unused]] float32 deltaTime )
    {
    }

    void TouchDevice::onFrameBegin( [[maybe_unused]] float32 deltaTime )
    {
        _pressedMask  = 0;
        _releasedMask = 0;
    }

    void TouchDevice::onFrameEnd()
    {
        _pressedMask  = 0;
        _releasedMask = 0;
    }

    void TouchDevice::resetState()
    {
        _downMask     = 0;
        _pressedMask  = 0;
        _releasedMask = 0;
    }

    bool TouchDevice::isControlDown( const uint16 controlIndex ) const
    {
        return controlIndex < kMaxPointerCount && ( _downMask & ( 1u << controlIndex ) ) != 0;
    }

    bool TouchDevice::wasControlPressed( const uint16 controlIndex ) const
    {
        return controlIndex < kMaxPointerCount && ( _pressedMask & ( 1u << controlIndex ) ) != 0;
    }

    bool TouchDevice::wasControlReleased( const uint16 controlIndex ) const
    {
        return controlIndex < kMaxPointerCount && ( _releasedMask & ( 1u << controlIndex ) ) != 0;
    }

    void TouchDevice::setPointerState( const uint16 pointerIndex, const int32 x, const int32 y, const bool bDown )
    {
        if ( pointerIndex >= kMaxPointerCount )
            return;
        _arrPosition[pointerIndex] = int2{ x, y };
        const uint16 bit           = static_cast<uint16>( 1u << pointerIndex );
        const bool   bWasDown      = ( _downMask & bit ) != 0;
        if ( bDown && bWasDown == false )
        {
            _downMask |= bit;
            _pressedMask |= bit;
        }
        else if ( bDown == false && bWasDown )
        {
            _downMask = static_cast<uint16>( _downMask & ~bit );
            _releasedMask |= bit;
        }
    }

    int2 TouchDevice::getPointerPosition( const uint16 pointerIndex ) const
    {
        return pointerIndex < kMaxPointerCount ? _arrPosition[pointerIndex] : int2{};
    }
} // namespace sw
