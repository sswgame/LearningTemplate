/**
 * @file PlatformInputManager.cpp
 * @brief 콜백 입력 창(웹 · Android)의 입력 플랫폼 훅입니다. 창이 실은 `PlatformInputEvent` 를 엔진 원시 이벤트로 옮깁니다.
 * @details 키 번호만 플랫폼마다 다르다(웹 — DOM `KeyboardEvent.code`, Android — `AKEYCODE_*`). 커서 모양 · 잠금은 두 플랫폼 모두 없다(브라우저 포인터 잠금은 남은 일).
 *          패드는 하드웨어 없는 슬롯을 둔다 — 가상 입력 · 재생이 같은 길로 들어온다(브라우저 Gamepad API · Android 패드 연결은 남은 일, docs/PlatformTargets.md).
 */
#include "pch.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/PlatformInputEvent.h"

#if defined( SW_PLATFORM_WEB ) || defined( SW_PLATFORM_ANDROID )
    #if defined( SW_PLATFORM_ANDROID )
        #include <android/keycodes.h>
    #endif

namespace sw
{
    namespace
    {
        /** @brief 하드웨어 없는 패드 슬롯입니다(헤드리스 서버와 같은 모양). */
        class PlatformGamepadDevice final : public GamepadDevice
        {
        public:
            explicit PlatformGamepadDevice( uint32 deviceIndex )
                : GamepadDevice( deviceIndex ) {}

            void poll( [[maybe_unused]] float32 deltaTime ) override {}

        protected:
            bool isHardwareConnected() const override { return false; }
        };

        struct PlatformInputManagerInternal
        {
    #if defined( SW_PLATFORM_WEB )
            /** @brief DOM code 와 엔진 키 이름이 다른 것만 적는다 — 나머지(KeyA → A, Digit1, F1, Enter, Numpad0 …)는 이름 규칙으로 맞는다. */
            struct CodeMapping
            {
                const utf8* _pDomCode;
                const utf8* _pKeyName;
            };
            static constexpr CodeMapping kArrCodeMapping[] = {
                {   "ArrowLeft",         "Left"},
                {  "ArrowRight",        "Right"},
                {     "ArrowUp",           "Up"},
                {   "ArrowDown",         "Down"},
                {   "ShiftLeft",    "LeftShift"},
                {  "ShiftRight",   "RightShift"},
                { "ControlLeft",  "LeftControl"},
                {"ControlRight", "RightControl"},
                {     "AltLeft",      "LeftAlt"},
                {    "AltRight",     "RightAlt"},
                {    "MetaLeft",    "LeftSuper"},
                {   "MetaRight",   "RightSuper"},
                { "ContextMenu",         "Menu"},
                { "BracketLeft",  "LeftBracket"},
                {"BracketRight", "RightBracket"},
                {       "Quote",   "Apostrophe"},
                {   "Backquote",        "Grave"},
            };

            static Key mapKey( const PlatformInputEvent& input )
            {
                const string_view code( input._arrText );
                for ( const CodeMapping& mapping : kArrCodeMapping )
                {
                    if ( code == mapping._pDomCode )
                        return KeyCodeUtil::fromName( mapping._pKeyName );
                }
                constexpr string_view kLetterPrefix = "Key"; // "KeyA" → "A"
                if ( code.size() == kLetterPrefix.size() + 1 && code.substr( 0, kLetterPrefix.size() ) == kLetterPrefix )
                    return KeyCodeUtil::fromName( code.substr( kLetterPrefix.size() ) );
                return KeyCodeUtil::fromName( code );
            }
    #elif defined( SW_PLATFORM_ANDROID )
            static Key mapKey( const PlatformInputEvent& input )
            {
                const int32 code = input._keyCode;
                if ( code >= AKEYCODE_A && code <= AKEYCODE_Z )
                    return static_cast<Key>( static_cast<uint32>( Key::A ) + static_cast<uint32>( code - AKEYCODE_A ) );
                if ( code >= AKEYCODE_0 && code <= AKEYCODE_9 )
                    return static_cast<Key>( static_cast<uint32>( Key::Digit0 ) + static_cast<uint32>( code - AKEYCODE_0 ) );
                if ( code >= AKEYCODE_F1 && code <= AKEYCODE_F12 )
                    return static_cast<Key>( static_cast<uint32>( Key::F1 ) + static_cast<uint32>( code - AKEYCODE_F1 ) );
                if ( code >= AKEYCODE_NUMPAD_0 && code <= AKEYCODE_NUMPAD_9 )
                    return static_cast<Key>( static_cast<uint32>( Key::Numpad0 ) + static_cast<uint32>( code - AKEYCODE_NUMPAD_0 ) );
                struct CodeKey
                {
                    int32 _code;
                    Key   _key;
                };
                // 뒤로 가기(AKEYCODE_BACK)는 Escape 다 — 일시 정지 메뉴 · 화면 닫기가 같은 액션을 받는다(언리얼 Android 의 Back → Escape 와 같다, 기억).
                static constexpr CodeKey kArrCode[] = {
                    {       AKEYCODE_BACK,       Key::Escape},
                    {     AKEYCODE_ESCAPE,       Key::Escape},
                    {        AKEYCODE_TAB,          Key::Tab},
                    {      AKEYCODE_ENTER,        Key::Enter},
                    {      AKEYCODE_SPACE,        Key::Space},
                    {        AKEYCODE_DEL,    Key::Backspace},
                    {AKEYCODE_FORWARD_DEL,       Key::Delete},
                    {  AKEYCODE_DPAD_LEFT,         Key::Left},
                    { AKEYCODE_DPAD_RIGHT,        Key::Right},
                    {    AKEYCODE_DPAD_UP,           Key::Up},
                    {  AKEYCODE_DPAD_DOWN,         Key::Down},
                    { AKEYCODE_SHIFT_LEFT,    Key::LeftShift},
                    {AKEYCODE_SHIFT_RIGHT,   Key::RightShift},
                    {  AKEYCODE_CTRL_LEFT,  Key::LeftControl},
                    { AKEYCODE_CTRL_RIGHT, Key::RightControl},
                    {   AKEYCODE_ALT_LEFT,      Key::LeftAlt},
                    {  AKEYCODE_ALT_RIGHT,     Key::RightAlt},
                    {      AKEYCODE_MINUS,        Key::Minus},
                    {     AKEYCODE_EQUALS,        Key::Equal},
                    {      AKEYCODE_COMMA,        Key::Comma},
                    {     AKEYCODE_PERIOD,       Key::Period},
                    {      AKEYCODE_SLASH,        Key::Slash},
                };
                for ( const CodeKey& entry : kArrCode )
                {
                    if ( entry._code == code )
                        return entry._key;
                }
                return Key::Unknown;
            }
    #else
        #error "Unsupported platform"
    #endif

            /** @brief DOM 버튼 번호(0 왼쪽 · 1 가운데 · 2 오른쪽)를 엔진 버튼으로 옮깁니다. 모르는 번호는 Count 입니다. */
            static MouseButton mapButton( uint8 button )
            {
                if ( button == 0 )
                    return MouseButton::Left;
                if ( button == 1 )
                    return MouseButton::Middle;
                if ( button == 2 )
                    return MouseButton::Right;
                return MouseButton::Count;
            }
        };
    } // namespace

    void InputManager::pollPlatform()
    {
    }

    void InputManager::onNativeWindowEvent( const NativeWindowEvent& event )
    {
        processNativeEvent( event );
    }

    // 길이를 나누지 않는다: 입력 종류별로 갈라 주는 `switch` 디스패처다(X11 짝과 같다).
    void InputManager::processNativeEvent( const NativeWindowEvent& event )
    {
        using Internal = PlatformInputManagerInternal;
        if ( event._message != NativeWindowEvent::kMessagePlatformInput || event._lParam == 0 )
            return;
        const PlatformInputEvent& input = *reinterpret_cast<const PlatformInputEvent*>( event._lParam );
        switch ( input._kind )
        {
            case PlatformInputKind::KeyDown:
            case PlatformInputKind::KeyUp:
            {
                const Key key = Internal::mapKey( input );
                if ( key == Key::Unknown )
                    break;
                postRawEvent( input._kind == PlatformInputKind::KeyDown ? RawInputEvent::makeKeyDown( key, 0, input._bRepeat != 0 ) : RawInputEvent::makeKeyUp( key ) );
                break;
            }
            case PlatformInputKind::PointerMove:
            {
                _lastPlatformPointer = int2{ input._x, input._y };
                postRawEvent( RawInputEvent::makeMouseMove( input._x, input._y ) );
                break;
            }
            case PlatformInputKind::PointerDown:
            case PlatformInputKind::PointerUp:
            {
                const MouseButton button = Internal::mapButton( input._button );
                const bool        bDown  = input._kind == PlatformInputKind::PointerDown;
                if ( button == MouseButton::Count || ( bDown ? onPlatformPointerPressed( button ) : onPlatformPointerReleased( button ) ) )
                    break;
                postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( button, input._x, input._y ) : RawInputEvent::makeMouseButtonUp( button, input._x, input._y ) );
                break;
            }
            case PlatformInputKind::Wheel:
            {
                postRawEvent( RawInputEvent::makeMouseWheel( input._wheel ) );
                break;
            }
            case PlatformInputKind::TouchDown:
            case PlatformInputKind::TouchMove:
            case PlatformInputKind::TouchUp:
            {
                const RawInputEventType type = input._kind == PlatformInputKind::TouchDown ? RawInputEventType::TouchDown
                                                                                           : ( input._kind == PlatformInputKind::TouchMove ? RawInputEventType::TouchMove : RawInputEventType::TouchUp );
                postRawEvent( RawInputEvent::makeTouch( type, input._pointerIndex, input._x, input._y ) );
                break;
            }
            case PlatformInputKind::Text:
            {
                postRawEvent( RawInputEvent::makeTextInput( string_view( input._arrText ) ) );
                break;
            }
        }
    }

    void InputManager::registerPlatformGamepads()
    {
        registerGamepadSlots<PlatformGamepadDevice>();
    }

    void InputManager::setCursorVisiblePlatform( [[maybe_unused]] bool bVisible )
    {
    }

    void InputManager::applyCursorShapePlatform()
    {
    }

    void InputManager::applyMouseLockMode()
    {
    }

    void InputManager::recenterLockedCursorPlatform()
    {
    }

    void InputManager::releaseMouseLockMode()
    {
    }

    bool InputManager::isWindowFocusedPlatform() const
    {
        return _bWindowFocused == SW_TRUE;
    }

    void InputManager::disableWindowsAccessibilityShortcuts()
    {
    }

    void InputManager::restoreWindowsAccessibilityShortcuts()
    {
    }
} // namespace sw

#endif
