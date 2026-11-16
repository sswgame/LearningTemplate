/**
 * @file PlatformInputManager.cpp
 * @brief 콜백 입력 창(웹 · Android)의 입력 플랫폼 훅입니다. 창이 실은 `PlatformInputEvent` 를 엔진 원시 이벤트로 옮깁니다.
 * @details 키 번호만 플랫폼마다 다르다(웹 — DOM `KeyboardEvent.code`, Android — `AKEYCODE_*`). 커서 모양은 두 플랫폼 모두 없다.
 *          포인터 잠금은 창이 묶는다(`IWindow::setPointerLock` — 웹은 Pointer Lock API, Android 는 묶지 않는다). 잠긴 동안의 이동은 `PointerRelative` 다.
 *          패드: 웹은 Gamepad API 를 프레임마다 읽고(standard 배치만 — `PlatformGamepadLayout`), Android 는 창이 패드 입력 장치의 키 · 축 이벤트와 연결 · 끊김을
 *          패드 칸 번호로 실어 보낸다. 둘 다 가상 입력 · 재생과 같은 원시 이벤트 길(`makeGamepad*`)로 들어온다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/Devices/PlatformGamepadLayout.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputKeyTable.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/PlatformInputEvent.h"

#if defined( SW_PLATFORM_WEB ) || defined( SW_PLATFORM_ANDROID )
    #if defined( SW_PLATFORM_ANDROID )
        #include <android/input.h>
        #include <android/keycodes.h>
    #elif defined( SW_PLATFORM_WEB )
        #include <emscripten/html5.h>
    #else
        #error "Unsupported platform"
    #endif

namespace sw
{
    static_assert( kPlatformGamepadSlotCount == kMaxGamepadSlot, "the platform window's gamepad slots must match the input manager's gamepad slots" );

    namespace
    {
        /**
         * @class PlatformGamepadDevice
         * @brief 콜백 입력 플랫폼의 패드 칸 하나입니다. 웹은 `poll` 이 Gamepad API 를 읽고, Android 는 창이 보낸 연결 · 끊김(`setHardwareConnected`)과 원시 이벤트로 채운다.
         */
        class PlatformGamepadDevice final : public GamepadDevice
        {
        public:
            explicit PlatformGamepadDevice( uint32 deviceIndex )
                : GamepadDevice( deviceIndex )
                , _bConnected{ SW_FALSE }
                , _bMappingReported{ SW_FALSE }
                , _reserved{ 0 }
            {
            }

            void poll( [[maybe_unused]] float32 deltaTime ) override;

            /** @brief 패드가 붙었거나(true) 떨어졌습니다(false). 바뀌었을 때만 연결 콜백을 부르고, 떨어지면 상태를 비운다(XInput 짝과 같다). */
            void setHardwareConnected( bool bConnected )
            {
                const bool bWasConnected = _bConnected != SW_FALSE;
                _bConnected              = bConnected ? SW_TRUE : SW_FALSE;
                if ( bConnected == false )
                {
                    _buttonMask   = 0;
                    _leftStick    = float2{};
                    _rightStick   = float2{};
                    _leftTrigger  = 0.0f;
                    _rightTrigger = 0.0f;
                }
                if ( bWasConnected != bConnected && _onConnectionChanged.isBound() )
                    _onConnectionChanged( _deviceIndex, bConnected );
            }

        protected:
            bool isHardwareConnected() const override { return _bConnected != SW_FALSE; }

        private:
            uint8                  _bConnected       : 1;
            uint8                  _bMappingReported : 1; ///< standard 배치가 아닌 패드를 이미 알렸다(웹)
            [[maybe_unused]] uint8 _reserved         : 6;
        };

    #if defined( SW_PLATFORM_WEB )
        void PlatformGamepadDevice::poll( [[maybe_unused]] float32 deltaTime )
        {
            // Gamepad API 는 끌어 읽는다 — 읽기 전에 표본을 떠야 한다(https://emscripten.org/docs/api_reference/html5.h.html#c.emscripten_sample_gamepad_data).
            // 브라우저는 사용자가 패드를 한 번 누르기 전에는 패드를 보이지 않는다(Gamepad 명세의 지문 막기) — 그때까지는 끊긴 칸이다.
            EmscriptenGamepadEvent state{};
            const bool             bSampled  = emscripten_sample_gamepad_data() == EMSCRIPTEN_RESULT_SUCCESS && emscripten_get_num_gamepads() > static_cast<int>( _deviceIndex );
            const bool             bPresent  = bSampled && emscripten_get_gamepad_status( static_cast<int>( _deviceIndex ), &state ) == EMSCRIPTEN_RESULT_SUCCESS && state.connected;
            const bool             bStandard = bPresent && string_view( state.mapping ) == "standard";
            if ( bPresent && bStandard == false && _bMappingReported == SW_FALSE )
            {
                // 배치를 모르는 패드는 버튼 번호의 뜻이 패드마다 다르다 — 짐작해 옮기지 않고 연결하지 않는다.
                SW_LOG_ERROR_ID( "PlatformInput.GamepadNotStandard", "Gamepad %# ('%#') does not report the standard mapping ('%#') - it is not connected", _deviceIndex, state.id, state.mapping );
                _bMappingReported = SW_TRUE;
            }
            setHardwareConnected( bStandard );
            if ( bStandard == false )
                return;
            _buttonMask = PlatformGamepadLayout::makeStandardButtonMask( state.digitalButton, static_cast<uint32>( MathUtil::min( state.numButtons, static_cast<int>( PlatformGamepadLayout::kStandardButtonCount ) ) ) );
            if ( state.numAxes >= static_cast<int>( PlatformGamepadLayout::kStandardAxisCount ) )
            {
                setAxis( 0, static_cast<float32>( state.axis[0] ) );
                setAxis( 1, PlatformGamepadLayout::toEngineStickY( static_cast<float32>( state.axis[1] ) ) );
                setAxis( 2, static_cast<float32>( state.axis[2] ) );
                setAxis( 3, PlatformGamepadLayout::toEngineStickY( static_cast<float32>( state.axis[3] ) ) );
            }
            if ( state.numButtons > static_cast<int>( PlatformGamepadLayout::kStandardRightTrigger ) )
            {
                setAxis( 4, static_cast<float32>( state.analogButton[PlatformGamepadLayout::kStandardLeftTrigger] ) );
                setAxis( 5, static_cast<float32>( state.analogButton[PlatformGamepadLayout::kStandardRightTrigger] ) );
            }
        }
    #elif defined( SW_PLATFORM_ANDROID )
        void PlatformGamepadDevice::poll( [[maybe_unused]] float32 deltaTime )
        {
            // Android 패드는 밀어 준다 — 창이 보낸 원시 이벤트가 버튼 · 축을 채운다(`processNativeEvent`).
        }
    #else
        #error "Unsupported platform"
    #endif

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
            static Key mapKey( const PlatformInputEvent& input ) { return InputKeyTable::mapAndroidKeyCode( input._keyCode ); }

            /**
             * @brief 패드 장치의 키 번호를 엔진 버튼으로 옮깁니다. 엔진 버튼이 없는 키(L2 · R2 디지털 — 방아쇠는 축이 준다, 모드 키)는 Count 입니다.
             * @details https://developer.android.com/develop/ui/views/touch-and-input/game-controllers/controller-input — 얼굴 버튼 BUTTON_A/B/X/Y, 어깨 L1/R1, 스틱 누름 THUMBL/R, START · SELECT,
             *          D패드는 패드에 따라 DPAD 키 또는 HAT 축으로 온다(둘 다 받는다).
             */
            static GamepadButton mapGamepadKey( int32 code )
            {
                struct CodeButton
                {
                    int32         _code;
                    GamepadButton _button;
                };
                static constexpr CodeButton kArrCode[] = {
                    {     AKEYCODE_BUTTON_A,             GamepadButton::A},
                    {     AKEYCODE_BUTTON_B,             GamepadButton::B},
                    {     AKEYCODE_BUTTON_X,             GamepadButton::X},
                    {     AKEYCODE_BUTTON_Y,             GamepadButton::Y},
                    {    AKEYCODE_BUTTON_L1,  GamepadButton::LeftShoulder},
                    {    AKEYCODE_BUTTON_R1, GamepadButton::RightShoulder},
                    {AKEYCODE_BUTTON_THUMBL,     GamepadButton::LeftThumb},
                    {AKEYCODE_BUTTON_THUMBR,    GamepadButton::RightThumb},
                    { AKEYCODE_BUTTON_START,         GamepadButton::Start},
                    {AKEYCODE_BUTTON_SELECT,          GamepadButton::Back},
                    {      AKEYCODE_DPAD_UP,        GamepadButton::DPadUp},
                    {    AKEYCODE_DPAD_DOWN,      GamepadButton::DPadDown},
                    {    AKEYCODE_DPAD_LEFT,      GamepadButton::DPadLeft},
                    {   AKEYCODE_DPAD_RIGHT,     GamepadButton::DPadRight},
                };
                for ( const CodeButton& entry : kArrCode )
                {
                    if ( entry._code == code )
                        return entry._button;
                }
                return GamepadButton::Count;
            }

            /** @brief 해트 축 하나(@p value)를 D패드 두 버튼(@p negative · @p positive)의 원시 이벤트로 냅니다 — 해트 값 하나가 두 버튼의 상태를 다 정한다. */
            static void postHatAxis( InputManager& manager, float32 value, GamepadButton negative, GamepadButton positive, uint8 padIndex )
            {
                bool bNegative = false;
                bool bPositive = false;
                PlatformGamepadLayout::splitHatAxis( value, bNegative, bPositive );
                manager.postRawEvent( bNegative ? RawInputEvent::makeGamepadButtonDown( negative, padIndex ) : RawInputEvent::makeGamepadButtonUp( negative, padIndex ) );
                manager.postRawEvent( bPositive ? RawInputEvent::makeGamepadButtonDown( positive, padIndex ) : RawInputEvent::makeGamepadButtonUp( positive, padIndex ) );
            }

            /** @brief 패드 축 하나를 엔진 축(0 ~ 5) · D패드로 옮겨 원시 이벤트로 냅니다. 엔진이 쓰지 않는 축은 버린다. */
            static void postGamepadAxis( InputManager& manager, int32 axis, float32 value, uint8 padIndex )
            {
                switch ( axis )
                {
                    case AMOTION_EVENT_AXIS_X:
                        manager.postRawEvent( RawInputEvent::makeGamepadAxis( 0, value, padIndex ) );
                        break;
                    case AMOTION_EVENT_AXIS_Y:
                        manager.postRawEvent( RawInputEvent::makeGamepadAxis( 1, PlatformGamepadLayout::toEngineStickY( value ), padIndex ) );
                        break;
                    case AMOTION_EVENT_AXIS_Z:
                        manager.postRawEvent( RawInputEvent::makeGamepadAxis( 2, value, padIndex ) );
                        break;
                    case AMOTION_EVENT_AXIS_RZ:
                        manager.postRawEvent( RawInputEvent::makeGamepadAxis( 3, PlatformGamepadLayout::toEngineStickY( value ), padIndex ) );
                        break;
                    case AMOTION_EVENT_AXIS_LTRIGGER: // 창이 BRAKE 와 합쳐 보낸다(`AndroidWindow::queueGamepadMotion`)
                        manager.postRawEvent( RawInputEvent::makeGamepadAxis( 4, value, padIndex ) );
                        break;
                    case AMOTION_EVENT_AXIS_RTRIGGER: // GAS 와 합쳐 보낸다
                        manager.postRawEvent( RawInputEvent::makeGamepadAxis( 5, value, padIndex ) );
                        break;
                    case AMOTION_EVENT_AXIS_HAT_X:
                        postHatAxis( manager, value, GamepadButton::DPadLeft, GamepadButton::DPadRight, padIndex );
                        break;
                    case AMOTION_EVENT_AXIS_HAT_Y:
                        postHatAxis( manager, value, GamepadButton::DPadUp, GamepadButton::DPadDown, padIndex );
                        break;
                    default:
                        break;
                }
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
            case PlatformInputKind::PointerRelative:
            {
                // 포인터가 잠긴 동안의 이동이다(위치는 서 있다) — Wayland 상대 포인터 · Win32 WM_INPUT 과 같이 위치 없이 이동량만 넘긴다.
                postRawEvent( RawInputEvent::makeMouseRawDelta( input._relativeX, input._relativeY ) );
                break;
            }
            case PlatformInputKind::GamepadConnected:
            case PlatformInputKind::GamepadDisconnected:
            {
                // 칸의 장치는 이 파일이 등록한 형식이다(`registerPlatformGamepads`).
                GamepadDevice* const pPad = getGamepad( input._pointerIndex );
                if ( pPad != nullptr )
                    static_cast<PlatformGamepadDevice*>( pPad )->setHardwareConnected( input._kind == PlatformInputKind::GamepadConnected );
                break;
            }
            case PlatformInputKind::GamepadKeyDown:
            case PlatformInputKind::GamepadKeyUp:
            case PlatformInputKind::GamepadAxis:
            {
    #if defined( SW_PLATFORM_ANDROID )
                const uint8 padIndex = static_cast<uint8>( input._pointerIndex );
                if ( input._kind == PlatformInputKind::GamepadAxis )
                {
                    Internal::postGamepadAxis( *this, input._keyCode, input._wheel, padIndex );
                    break;
                }
                const GamepadButton button = Internal::mapGamepadKey( input._keyCode );
                if ( button != GamepadButton::Count )
                    postRawEvent( input._kind == PlatformInputKind::GamepadKeyDown ? RawInputEvent::makeGamepadButtonDown( button, padIndex ) : RawInputEvent::makeGamepadButtonUp( button, padIndex ) );
    #elif defined( SW_PLATFORM_WEB )
                    // 웹 패드는 이벤트가 아니라 `PlatformGamepadDevice::poll` 이 읽는다 — 창이 이 종류를 보내지 않는다.
    #else
        #error "Unsupported platform"
    #endif
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
        // 창이 포인터를 묶는다 — 웹은 Pointer Lock API(가두기는 브라우저에 없다), Android 는 묶지 않는다. 못 하면 묶지 않은 채로 한 번 오류로 알린다(Wayland 갈래와 같은 규칙).
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        IWindow* const             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( _pMouse == nullptr || pWindow == nullptr )
            return;
        const MouseLockMode     mode            = _pMouse->getLockMode();
        const WindowPointerLock lock            = mode == MouseLockMode::LockedInCenter ? WindowPointerLock::Locked : ( mode == MouseLockMode::ConfinedToWindow ? WindowPointerLock::Confined : WindowPointerLock::Released );
        constexpr uint32        kPointerLockBit = 1u << 0;
        if ( pWindow->setPointerLock( lock ) == false && ( _unsupportedReportedMask.fetch_or( kPointerLockBit ) & kPointerLockBit ) == 0 )
            SW_LOG_ERROR_ID( "PlatformInput.WindowCannotLockPointer", "This window cannot lock the pointer this way (the browser has no confine, Android has no pointer lock) - the pointer stays free" );
    }

    void InputManager::recenterLockedCursorPlatform()
    {
        // 잠긴 포인터는 서 있고 상대 이동만 온다 — 되돌릴 것이 없다(Wayland 잠금과 같다).
    }

    void InputManager::releaseMouseLockMode()
    {
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        IWindow* const             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pWindow != nullptr )
            (void)pWindow->setPointerLock( WindowPointerLock::Released ); // 풀기는 항상 된다
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
