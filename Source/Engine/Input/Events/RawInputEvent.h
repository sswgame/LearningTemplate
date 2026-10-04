/**
 * @file RawInputEvent.h
 * @brief OS 창 스레드 · 백그라운드 입력 폴러에서 생기는 정밀 원시 입력 이벤트 패킷입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodeUtil.h"

namespace sw
{
    namespace ModifierKey
    {
        inline constexpr uint8 None  = 0;
        inline constexpr uint8 Ctrl  = SW_BIT( 0 );
        inline constexpr uint8 Shift = SW_BIT( 1 );
        inline constexpr uint8 Alt   = SW_BIT( 2 );
        inline constexpr uint8 Super = SW_BIT( 3 );
        /** @brief 아는 수정 키 비트 전부입니다. 이보다 큰 마스크는 모르는 비트를 든 것입니다(파일에서 읽은 마스크의 범위 검사). */
        inline constexpr uint8 All = Ctrl | Shift | Alt | Super;
    } // namespace ModifierKey

    /** @brief 원시 입력 이벤트 종류입니다. */
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
        FocusLost,
        PointerEntered, ///< 포인터가 창 안으로 들어왔습니다. 값은 뒤에 덧붙입니다(리플레이 파일이 번호를 담습니다).
        PointerLeft,    ///< 포인터가 창 밖으로 나갔습니다.
        MouseRawDelta   ///< 위치 없이 장치가 보고한 이동량(Win32 WM_INPUT)만 담습니다. 위치를 건드리지 않습니다.
    };

    /**
     * @struct RawInputEvent
     * @brief 락프리 큐로 스레드 사이에 안전하게 넘기는 원시 입력 이벤트 구조체입니다.
     */
    struct RawInputEvent
    {
        RawInputEventType      _type{ RawInputEventType::None };
        InputDeviceKind        _deviceKind{ InputDeviceKind::Keyboard };
        uint8                  _deviceIndex{ 0 };
        uint8                  _modifierMask{ 0 }; /**< ModifierKey::Ctrl | Shift | Alt | Super */
        uint8                  _bRepeat  : 1;
        [[maybe_unused]] uint8 _reserved : 7;

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
                float2      _rawDelta;
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
            , _payload{} {}

        static RawInputEvent makeKeyDown( Key key, uint16 vk = 0, bool bRepeat = false, uint8 modifierMask = 0 )
        {
            RawInputEvent event{};
            event._type                               = RawInputEventType::KeyDown;
            event._deviceKind                         = InputDeviceKind::Keyboard;
            event._modifierMask                       = modifierMask;
            event._bRepeat                            = bRepeat ? SW_TRUE : SW_FALSE;
            event._payload._keyData._key              = key;
            event._payload._keyData._nativeVirtualKey = vk;
            return event;
        }

        static RawInputEvent makeKeyUp( Key key, uint16 vk = 0, uint8 modifierMask = 0 )
        {
            RawInputEvent event{};
            event._type                               = RawInputEventType::KeyUp;
            event._deviceKind                         = InputDeviceKind::Keyboard;
            event._modifierMask                       = modifierMask;
            event._payload._keyData._key              = key;
            event._payload._keyData._nativeVirtualKey = vk;
            return event;
        }

        static RawInputEvent makeMouseMove( int32 x, int32 y, float32 rawDx = 0.0f, float32 rawDy = 0.0f )
        {
            RawInputEvent event{};
            event._type                            = RawInputEventType::MouseMove;
            event._deviceKind                      = InputDeviceKind::Mouse;
            event._payload._mouseData._x           = x;
            event._payload._mouseData._y           = y;
            event._payload._mouseData._rawDelta._x = rawDx;
            event._payload._mouseData._rawDelta._y = rawDy;
            return event;
        }

        static RawInputEvent makeMouseButtonDown( MouseButton btn, int32 x = 0, int32 y = 0, uint8 modifierMask = 0 )
        {
            RawInputEvent event{};
            event._type                       = RawInputEventType::MouseButtonDown;
            event._deviceKind                 = InputDeviceKind::Mouse;
            event._modifierMask               = modifierMask;
            event._payload._mouseData._button = btn;
            event._payload._mouseData._x      = x;
            event._payload._mouseData._y      = y;
            return event;
        }

        static RawInputEvent makeMouseButtonUp( MouseButton btn, int32 x = 0, int32 y = 0, uint8 modifierMask = 0 )
        {
            RawInputEvent event{};
            event._type                       = RawInputEventType::MouseButtonUp;
            event._deviceKind                 = InputDeviceKind::Mouse;
            event._modifierMask               = modifierMask;
            event._payload._mouseData._button = btn;
            event._payload._mouseData._x      = x;
            event._payload._mouseData._y      = y;
            return event;
        }

        static RawInputEvent makeMouseDoubleClick( MouseButton btn, int32 x = 0, int32 y = 0, uint8 modifierMask = 0 )
        {
            RawInputEvent event{};
            event._type                       = RawInputEventType::MouseDoubleClick;
            event._deviceKind                 = InputDeviceKind::Mouse;
            event._modifierMask               = modifierMask;
            event._payload._mouseData._button = btn;
            event._payload._mouseData._x      = x;
            event._payload._mouseData._y      = y;
            return event;
        }

        static RawInputEvent makeMouseWheel( float32 delta )
        {
            RawInputEvent event{};
            event._type                           = RawInputEventType::MouseWheel;
            event._deviceKind                     = InputDeviceKind::Mouse;
            event._payload._mouseData._wheelDelta = delta;
            return event;
        }

        static RawInputEvent makeMouseHorizontalWheel( float32 delta )
        {
            RawInputEvent event{};
            event._type                           = RawInputEventType::MouseWheelHorizontal;
            event._deviceKind                     = InputDeviceKind::Mouse;
            event._payload._mouseData._wheelDelta = delta;
            return event;
        }

        /** @brief 위치 없이 이동량만 담은 이벤트를 만듭니다(원시 마우스 입력). */
        static RawInputEvent makeMouseRawDelta( float32 rawDx, float32 rawDy )
        {
            RawInputEvent event{};
            event._type                            = RawInputEventType::MouseRawDelta;
            event._deviceKind                      = InputDeviceKind::Mouse;
            event._payload._mouseData._rawDelta._x = rawDx;
            event._payload._mouseData._rawDelta._y = rawDy;
            return event;
        }

        /** @brief 포인터가 창 안으로 들어오거나(true) 나간(false) 이벤트를 만듭니다. */
        static RawInputEvent makePointerCrossing( bool bEntered )
        {
            RawInputEvent event{};
            event._type       = bEntered ? RawInputEventType::PointerEntered : RawInputEventType::PointerLeft;
            event._deviceKind = InputDeviceKind::Mouse;
            return event;
        }

        /** @brief 창이 포커스를 얻거나(true) 잃은(false) 이벤트를 만듭니다. */
        static RawInputEvent makeFocusChange( bool bGained )
        {
            RawInputEvent event{};
            event._type = bGained ? RawInputEventType::FocusGained : RawInputEventType::FocusLost;
            return event;
        }

        static RawInputEvent makeGamepadButtonDown( GamepadButton btn, uint8 padIndex = 0 )
        {
            RawInputEvent event{};
            event._type                         = RawInputEventType::GamepadButtonDown;
            event._deviceKind                   = InputDeviceKind::Gamepad;
            event._deviceIndex                  = padIndex;
            event._payload._gamepadData._button = btn;
            return event;
        }

        static RawInputEvent makeGamepadButtonUp( GamepadButton btn, uint8 padIndex = 0 )
        {
            RawInputEvent event{};
            event._type                         = RawInputEventType::GamepadButtonUp;
            event._deviceKind                   = InputDeviceKind::Gamepad;
            event._deviceIndex                  = padIndex;
            event._payload._gamepadData._button = btn;
            return event;
        }

        static RawInputEvent makeGamepadAxis( uint16 axisIndex, float32 value, uint8 padIndex = 0 )
        {
            RawInputEvent event{};
            event._type                            = RawInputEventType::GamepadAxis;
            event._deviceKind                      = InputDeviceKind::Gamepad;
            event._deviceIndex                     = padIndex;
            event._payload._gamepadData._axisIndex = axisIndex;
            event._payload._gamepadData._axisValue = value;
            return event;
        }

        static RawInputEvent makeGamepadConnection( uint8 padIndex, bool bConnected )
        {
            RawInputEvent event{};
            event._type                             = RawInputEventType::GamepadConnectionChanged;
            event._deviceKind                       = InputDeviceKind::Gamepad;
            event._deviceIndex                      = padIndex;
            event._payload._gamepadData._bConnected = bConnected ? SW_TRUE : SW_FALSE;
            event._payload._gamepadData._reserved   = 0;
            return event;
        }

        static RawInputEvent makeTextInput( string_view text )
        {
            RawInputEvent event{};
            event._type       = RawInputEventType::TextInput;
            event._deviceKind = InputDeviceKind::Keyboard;
            copyTextPayload( event, text );
            return event;
        }

        static RawInputEvent makeTextComposition( string_view text )
        {
            RawInputEvent event{};
            event._type       = RawInputEventType::TextComposition;
            event._deviceKind = InputDeviceKind::Keyboard;
            copyTextPayload( event, text );
            return event;
        }

    private:
        /**
         * @brief 글자를 페이로드에 담습니다. 넘치면 **UTF-8 글자 경계에서** 자릅니다.
         * @details 바이트 수로만 자르면 한글 조합 문자열(글자당 3 바이트)이 길 때 마지막 글자의 앞 바이트만 남아 받는 쪽이 깨진
         *          UTF-8 을 받습니다.
         */
        static void copyTextPayload( RawInputEvent& outEvent, string_view text )
        {
            constexpr size_t kMaxTextBytes = constant::kMaxBuffer32 - 1;
            size_t           len           = text.size() < kMaxTextBytes ? text.size() : kMaxTextBytes;
            if ( len < text.size() )
            {
                // 잘리는 자리가 연속 바이트(10xxxxxx)면 그 글자의 선두 바이트 앞까지 물러난다.
                while ( len > 0 && ( static_cast<uint8>( text[len] ) & 0xC0 ) == 0x80 )
                {
                    --len;
                }
            }
            if ( len > 0 )
                Memory::copy( outEvent._payload._textData._arrUtf8, text.data(), len );
            outEvent._payload._textData._arrUtf8[len] = '\0';
        }
    };
} // namespace sw
