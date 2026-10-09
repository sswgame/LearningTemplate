#include "pch.h"

#include "Engine/Input/Virtual/VirtualInputScript.h"

namespace sw
{
    VirtualInputScript::VirtualInputScript()
        : _listEntry{}
        , _cursor{ 0 }
    {
    }

    void VirtualInputScript::addEvent( uint32 frameIndex, const RawInputEvent& rawEvent )
    {
        // 같은 프레임 안의 순서를 지키려면 그 프레임의 마지막 항목 뒤에 넣는다.
        size_t insertIndex = _listEntry.size();
        while ( insertIndex > 0 && _listEntry[insertIndex - 1]._frameIndex > frameIndex )
        {
            --insertIndex;
        }
        VirtualInputScriptEntry entry{};
        entry._frameIndex = frameIndex;
        entry._event      = rawEvent;
        _listEntry.insert( _listEntry.begin() + static_cast<ptrdiff_t>( insertIndex ), entry );
        _cursor = 0;
    }

    bool VirtualInputScript::addSlot( uint32 frameIndex, const InputSlot& slot, bool bDown )
    {
        switch ( slot._deviceKind )
        {
            case InputDeviceKind::Keyboard:
            {
                if ( slot._controlIndex >= static_cast<uint16>( Key::Count ) )
                    return false;
                const Key key = static_cast<Key>( slot._controlIndex );
                addEvent( frameIndex, bDown ? RawInputEvent::makeKeyDown( key ) : RawInputEvent::makeKeyUp( key ) );
                return true;
            }
            case InputDeviceKind::Mouse:
            {
                if ( slot._controlIndex >= static_cast<uint16>( MouseButton::Count ) )
                    return false;
                const MouseButton button  = static_cast<MouseButton>( slot._controlIndex );
                const int2        pointer = findPointerPosition( frameIndex );
                addEvent( frameIndex, bDown ? RawInputEvent::makeMouseButtonDown( button, pointer._x, pointer._y )
                                            : RawInputEvent::makeMouseButtonUp( button, pointer._x, pointer._y ) );
                return true;
            }
            case InputDeviceKind::Gamepad:
            {
                // 트리거(100 · 101)와 스틱은 축이다 — `addGamepadAxis` 로 낸다.
                if ( slot._controlIndex >= static_cast<uint16>( GamepadButton::Count ) )
                    return false;
                const GamepadButton button = static_cast<GamepadButton>( slot._controlIndex );
                addEvent( frameIndex, bDown ? RawInputEvent::makeGamepadButtonDown( button, slot._deviceIndex )
                                            : RawInputEvent::makeGamepadButtonUp( button, slot._deviceIndex ) );
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    bool VirtualInputScript::addTap( uint32 frameIndex, const InputSlot& slot, uint32 holdFrameCount )
    {
        if ( addSlot( frameIndex, slot, true ) == false )
            return false;
        return addSlot( frameIndex + holdFrameCount, slot, false );
    }

    void VirtualInputScript::addMouseDelta( uint32 frameIndex, float32 deltaX, float32 deltaY )
    {
        addEvent( frameIndex, RawInputEvent::makeMouseRawDelta( deltaX, deltaY ) );
    }

    void VirtualInputScript::addMousePosition( uint32 frameIndex, int32 x, int32 y )
    {
        addEvent( frameIndex, RawInputEvent::makeMouseMove( x, y ) );
    }

    int2 VirtualInputScript::findPointerPosition( uint32 frameIndex ) const
    {
        for ( size_t index = _listEntry.size(); index > 0; --index )
        {
            const VirtualInputScriptEntry& entry = _listEntry[index - 1];
            if ( entry._frameIndex <= frameIndex && entry._event._type == RawInputEventType::MouseMove )
                return int2{ entry._event._payload._mouseData._x, entry._event._payload._mouseData._y };
        }
        return int2{};
    }

    void VirtualInputScript::addGamepadAxis( uint32 frameIndex, uint16 axisIndex, float32 value, uint8 padIndex )
    {
        addEvent( frameIndex, RawInputEvent::makeGamepadAxis( axisIndex, value, padIndex ) );
    }

    void VirtualInputScript::clear()
    {
        _listEntry.clear();
        _cursor = 0;
    }

    void VirtualInputScript::emitFrame( uint32 frameIndex, vector<RawInputEvent>& outListEvent )
    {
        // 되돌아간 프레임(떼었다 다시 붙임)이면 처음부터 찾는다.
        if ( _cursor > 0 && _cursor <= _listEntry.size() && _listEntry[_cursor - 1]._frameIndex >= frameIndex )
            _cursor = 0;
        while ( _cursor < _listEntry.size() && _listEntry[_cursor]._frameIndex < frameIndex )
        {
            ++_cursor;
        }
        while ( _cursor < _listEntry.size() && _listEntry[_cursor]._frameIndex == frameIndex )
        {
            outListEvent.push_back( _listEntry[_cursor]._event );
            ++_cursor;
        }
    }

    bool VirtualInputScript::isFinished( uint32 frameIndex ) const
    {
        return _listEntry.empty() || _listEntry.back()._frameIndex < frameIndex;
    }
} // namespace sw
