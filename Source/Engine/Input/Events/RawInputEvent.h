/**
 * @file RawInputEvent.h
 * @brief OS 윈도우 스레드 / 백그라운드 입력 폴러에서 발생하는 정밀 원시 입력 이벤트 패킷
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
    namespace ModifierKey
    {
        inline constexpr uint8 None  = 0;
        inline constexpr uint8 Ctrl  = 1 << 0;
        inline constexpr uint8 Shift = 1 << 1;
        inline constexpr uint8 Alt   = 1 << 2;
        inline constexpr uint8 Super = 1 << 3;
    } // namespace ModifierKey

    /** @brief 원시 입력 이벤트 종류 */
    enum class RawInputEventType : uint8
    {
        None = 0,
        KeyDown,
        KeyUp,
        MouseMove,
        MouseButtonDown,
        MouseButtonUp,
        MouseDoubleClick,
        MouseWheel,
        MouseWheelHorizontal,
        GamepadAxis,
        GamepadButtonDown,
        GamepadButtonUp,
        GamepadConnectionChanged,
        TextInput,
        TextComposition,
        FocusGained,
        FocusLost
    };

    /**
     * @struct RawInputEvent
     * @brief 락프리 큐를 통해 스레드 간 안전하게 전달되는 고성능 원시 입력 이벤트 구조체
     */
    struct RawInputEvent
    {
        RawInputEventType      _type{ RawInputEventType::None };
        InputDeviceKind        _deviceKind{ InputDeviceKind::Keyboard };
        uint8                  _deviceIndex{ 0 };
        uint8                  _modifierMask{ 0 }; /**< ModifierKey::Ctrl | Shift | Alt | Super */
        uint8                  _bRepeat  : 1;
        [[maybe_unused]] uint8 _reserved : 7;
        uint64                 _timestampUs{ 0 };

        union
        {
            struct
            {
                Key    _key;
                uint16 _nativeVirtualKey;
            } _keyData;

            struct
            {
                int32       _x;
                int32       _y;
                float32     _rawDeltaX;
                float32     _rawDeltaY;
                float32     _wheelDelta;
                MouseButton _button;
            } _mouseData;

            struct
            {
                GamepadButton _button;
                uint16        _axisIndex;
                float32       _axisValue;
                uint8         _bConnected : 1;
                uint8         _reserved   : 7;
            } _gamepadData;

            struct
            {
                utf8 _arrUtf8[constant::kMaxBuffer32];
            } _textData;
        } _payload{};

        RawInputEvent()
            : _type{ RawInputEventType::None }
            , _deviceKind{ InputDeviceKind::Keyboard }
            , _deviceIndex{ 0 }
            , _modifierMask{ 0 }
            , _bRepeat{ SW_FALSE }
            , _reserved{ 0 }
            , _timestampUs{ 0 }
            , _payload{} {}

        static RawInputEvent makeKeyDown( Key key, uint16 vk = 0, bool bRepeat = false, uint8 modifierMask = 0 )
        {
            RawInputEvent evt{};
            evt._type                               = RawInputEventType::KeyDown;
            evt._deviceKind                         = InputDeviceKind::Keyboard;
            evt._modifierMask                       = modifierMask;
            evt._bRepeat                            = bRepeat ? SW_TRUE : SW_FALSE;
            evt._payload._keyData._key              = key;
            evt._payload._keyData._nativeVirtualKey = vk;
            return evt;
        }

        static RawInputEvent makeKeyUp( Key key, uint16 vk = 0, uint8 modifierMask = 0 )
        {
            RawInputEvent evt{};
            evt._type                               = RawInputEventType::KeyUp;
            evt._deviceKind                         = InputDeviceKind::Keyboard;
            evt._modifierMask                       = modifierMask;
            evt._payload._keyData._key              = key;
            evt._payload._keyData._nativeVirtualKey = vk;
            return evt;
        }

        static RawInputEvent makeMouseMove( int32 x, int32 y, float32 rawDx = 0.0f, float32 rawDy = 0.0f )
        {
            RawInputEvent evt{};
            evt._type                          = RawInputEventType::MouseMove;
            evt._deviceKind                    = InputDeviceKind::Mouse;
            evt._payload._mouseData._x         = x;
            evt._payload._mouseData._y         = y;
            evt._payload._mouseData._rawDeltaX = rawDx;
            evt._payload._mouseData._rawDeltaY = rawDy;
            return evt;
        }

        static RawInputEvent makeMouseButtonDown( MouseButton btn, int32 x = 0, int32 y = 0, uint8 modifierMask = 0 )
        {
            RawInputEvent evt{};
            evt._type                       = RawInputEventType::MouseButtonDown;
            evt._deviceKind                 = InputDeviceKind::Mouse;
            evt._modifierMask               = modifierMask;
            evt._payload._mouseData._button = btn;
            evt._payload._mouseData._x      = x;
            evt._payload._mouseData._y      = y;
            return evt;
        }

        static RawInputEvent makeMouseButtonUp( MouseButton btn, int32 x = 0, int32 y = 0, uint8 modifierMask = 0 )
        {
            RawInputEvent evt{};
            evt._type                       = RawInputEventType::MouseButtonUp;
            evt._deviceKind                 = InputDeviceKind::Mouse;
            evt._modifierMask               = modifierMask;
            evt._payload._mouseData._button = btn;
            evt._payload._mouseData._x      = x;
            evt._payload._mouseData._y      = y;
            return evt;
        }

        static RawInputEvent makeMouseDoubleClick( MouseButton btn, int32 x = 0, int32 y = 0, uint8 modifierMask = 0 )
        {
            RawInputEvent evt{};
            evt._type                       = RawInputEventType::MouseDoubleClick;
            evt._deviceKind                 = InputDeviceKind::Mouse;
            evt._modifierMask               = modifierMask;
            evt._payload._mouseData._button = btn;
            evt._payload._mouseData._x      = x;
            evt._payload._mouseData._y      = y;
            return evt;
        }

        static RawInputEvent makeMouseWheel( float32 delta )
        {
            RawInputEvent evt{};
            evt._type                           = RawInputEventType::MouseWheel;
            evt._deviceKind                     = InputDeviceKind::Mouse;
            evt._payload._mouseData._wheelDelta = delta;
            return evt;
        }

        static RawInputEvent makeMouseHorizontalWheel( float32 delta )
        {
            RawInputEvent evt{};
            evt._type                           = RawInputEventType::MouseWheelHorizontal;
            evt._deviceKind                     = InputDeviceKind::Mouse;
            evt._payload._mouseData._wheelDelta = delta;
            return evt;
        }

        static RawInputEvent makeGamepadButtonDown( GamepadButton btn, uint8 padIndex = 0 )
        {
            RawInputEvent evt{};
            evt._type                         = RawInputEventType::GamepadButtonDown;
            evt._deviceKind                   = InputDeviceKind::Gamepad;
            evt._deviceIndex                  = padIndex;
            evt._payload._gamepadData._button = btn;
            return evt;
        }

        static RawInputEvent makeGamepadButtonUp( GamepadButton btn, uint8 padIndex = 0 )
        {
            RawInputEvent evt{};
            evt._type                         = RawInputEventType::GamepadButtonUp;
            evt._deviceKind                   = InputDeviceKind::Gamepad;
            evt._deviceIndex                  = padIndex;
            evt._payload._gamepadData._button = btn;
            return evt;
        }

        static RawInputEvent makeGamepadAxis( uint16 axisIndex, float32 value, uint8 padIndex = 0 )
        {
            RawInputEvent evt{};
            evt._type                            = RawInputEventType::GamepadAxis;
            evt._deviceKind                      = InputDeviceKind::Gamepad;
            evt._deviceIndex                     = padIndex;
            evt._payload._gamepadData._axisIndex = axisIndex;
            evt._payload._gamepadData._axisValue = value;
            return evt;
        }

        static RawInputEvent makeGamepadConnection( uint8 padIndex, bool bConnected )
        {
            RawInputEvent evt{};
            evt._type                             = RawInputEventType::GamepadConnectionChanged;
            evt._deviceKind                       = InputDeviceKind::Gamepad;
            evt._deviceIndex                      = padIndex;
            evt._payload._gamepadData._bConnected = bConnected ? SW_TRUE : SW_FALSE;
            evt._payload._gamepadData._reserved   = 0;
            return evt;
        }

        static RawInputEvent makeTextInput( string_view text )
        {
            RawInputEvent evt{};
            evt._type        = RawInputEventType::TextInput;
            evt._deviceKind  = InputDeviceKind::Keyboard;
            const size_t len = text.size() < 31 ? text.size() : 31;
            if ( len > 0 )
            {
                Memory::copy( evt._payload._textData._arrUtf8, text.data(), len );
            }
            evt._payload._textData._arrUtf8[len] = '\0';
            return evt;
        }

        static RawInputEvent makeTextComposition( string_view text )
        {
            RawInputEvent evt{};
            evt._type        = RawInputEventType::TextComposition;
            evt._deviceKind  = InputDeviceKind::Keyboard;
            const size_t len = text.size() < 31 ? text.size() : 31;
            if ( len > 0 )
            {
                Memory::copy( evt._payload._textData._arrUtf8, text.data(), len );
            }
            evt._payload._textData._arrUtf8[len] = '\0';
            return evt;
        }
    };
} // namespace sw
