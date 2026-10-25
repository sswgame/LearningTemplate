#include "pch.h"

#include "Core/Common/PlatformOsHeaders.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Window/MainWindowSubsystem.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    #include "Core/Common/X11Headers.h"
    #include "Core/Concurrency/atomic.h"
    #include "Core/String/fixed_string.h"

    #include "Engine/Input/Devices/GamepadDevice.h"
    #include "Engine/Input/Devices/KeyboardDevice.h"
    #include "Engine/Input/Devices/MouseDevice.h"
    #include "Engine/Input/RawInputEvent.h"
    #include "Engine/Input/Map/InputKeyTable.h"
    #include "Engine/Input/Linux/LinuxGamepadDevice.h"
    #include "Engine/Window/IWindow.h"
    #include "Engine/Window/Linux/Wayland/WaylandInputEvent.h"
    #include "Engine/Window/NativeWindowEvent.h"

    #include <X11/XKBlib.h>
    #include <X11/cursorfont.h>
    #include <linux/input-event-codes.h>

    #include "Core/Common/X11MacroUndef.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 텍스트 입력(XIM/XIC)과 접근성(XKB) 상태를 창 수명 주기와 따로 지연 초기화해 보관합니다.
         * @note XIM 은 "on-the-spot", "root-window" 스타일 입력기의 커밋 문자열은 받지만, 조합(preedit)
         *       후보 창을 직접 그려 주는 완전한 프리에딧 렌더링은 구현하지 않았습니다.
         */
        struct X11InputInternal
        {
            static inline XIM    s_pInputMethod{ nullptr };
            static inline XIC    s_pInputContext{ nullptr };
            static inline void*  s_pImDisplay{ nullptr };                                                ///< XIC 를 만들 때 쓴 Display* 입니다.
            static inline Window s_imWindow{ 0 };                                                        ///< XIC 의 XNClientWindow 입니다. 창이 바뀌면 반드시 XIC 를 다시 만들어야 합니다.
            static inline Cursor s_invisibleCursor{ 0 };                                                 ///< X11 None(리소스 없음)입니다. X11MacroUndef.h 가 None 매크로를 지우므로 리터럴 0 을 씁니다.
            static inline void*  s_pCursorDisplay{ nullptr };                                            ///< s_invisibleCursor 를 만든 Display* 입니다(다르면 다시 만듭니다).
            static inline Cursor s_arrShapeCursor[static_cast<size_t>( CursorShape::NotAllowed ) + 1]{}; ///< 모양마다 만든 글꼴 커서입니다(0 이면 아직 없음)
            static inline void*  s_pShapeCursorDisplay{ nullptr };                                       ///< s_arrShapeCursor 를 만든 Display* 입니다
            static inline bool   s_bAccessibilityDisabled{ false };
            /** @brief 창이 X11 포커스를 가지고 있는지 나타냅니다(FocusIn/FocusOut 으로 갱신). 보조 폴링을 건너뛸지 정하는 데 씁니다. */
            static inline bool   s_bWindowFocused{ false };
            static inline uint32 s_prevXkbEnabledControls{ 0 };
            /** @brief 감지 가능한 자동 반복을 켜 둔 Display* 입니다(Display 마다 한 번). */
            static inline void* s_pAutoRepeatDisplay{ nullptr };

            /** @brief 누르고 있는 키가 Release/Press 쌍 대신 Press 만 되풀이하도록 X 서버에 요청합니다(Display 마다 한 번). */
            static void enableDetectableAutoRepeat( Display* pDisplay )
            {
                if ( pDisplay == nullptr || s_pAutoRepeatDisplay == pDisplay )
                    return;
                Bool bSupported = 0;
                XkbSetDetectableAutoRepeat( pDisplay, 1, &bSupported ); // 1 == True. X11MacroUndef.h 가 True 매크로를 지운다.
                s_pAutoRepeatDisplay = pDisplay;
            }

            /** @brief 활성 창에 대한 XIC 를 지연 생성해 반환합니다(실패하면 nullptr). */
            static XIC getOrCreateInputContext()
            {
                // XIM/XIC 칸은 창 메시지 처리(`postTextInput` ← `processNativeEvent`, 창을 만든 메인 스레드)에서만 닿는다 — 워커에서 오면 검사 뒤 채우기가 겹친다.
                SW_ASSERT( engine::isOnPoolWorker() == false );
                MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                if ( pWindow == nullptr )
                    return nullptr;

                Display* pDisplay  = nullptr;
                Window   x11Window = 0;
                if ( tryGetX11( pWindow, pDisplay, x11Window ) == false )
                    return nullptr;

                // Display 만 비교하면 안 된다. 같은 X 서버에서 창을 다시 만들면(에디터 창 재생성, 테스트가
                // 창을 반복해서 생성하고 파괴할 때) 캐시된 XIC 가 이미 파괴된 Window 를 XNClientWindow 로 가리키고 있어
                // 텍스트 입력이 조용히 죽는다. 창까지 함께 비교해야 한다.
                const bool bCachedContextReusable = s_pInputContext != nullptr && s_pImDisplay == pDisplay && s_imWindow == x11Window;
                if ( bCachedContextReusable )
                    return s_pInputContext;

                if ( s_pInputContext != nullptr )
                {
                    XDestroyIC( s_pInputContext );
                    s_pInputContext = nullptr;
                }
                if ( s_pInputMethod != nullptr )
                {
                    XCloseIM( s_pInputMethod );
                    s_pInputMethod = nullptr;
                }

                XSetLocaleModifiers( "" );
                s_pInputMethod = XOpenIM( pDisplay, nullptr, nullptr, nullptr );
                if ( s_pInputMethod == nullptr )
                    return nullptr;

                s_pInputContext = XCreateIC( s_pInputMethod,
                                             XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                                             XNClientWindow, x11Window,
                                             XNFocusWindow, x11Window,
                                             nullptr );
                s_pImDisplay    = pDisplay;
                s_imWindow      = x11Window;
                return s_pInputContext;
            }

            /**
             * @brief X11 창이면 서버 연결과 창 ID 를 담고 true 입니다. X11 호출(`XQueryPointer` 등)은 이 함수로 얻은 값으로만 한다.
             * @details Wayland 창의 `getNativeDisplay` 는 wl_display* 라 `Display*` 로 읽으면 죽는다(`IWindow::tryGetX11Surface`). X11 창이 아니거나 연결이 아직 없으면 false 이고 출력은 비어 있다.
             *          이 입력 관리자는 창 종류를 가르지 않는다 — X11 서버 호출(보조 폴링 · 가운데 되돌리기 · AccessX 억제)은 이 함수가 true 일 때만 하고, false 인 창(Wayland)에는
             *          그 동작이 해당 없음이라 건너뛰는 것이 정상이다. 창이 스스로 하는 동작(커서 모양 · 포인터 묶기 · 커서 숨기기)은 `IWindow` 의 질의로 가르고 안 되면 한 번 오류로 알린다.
             */
            [[nodiscard]] static bool tryGetX11( const IWindow* pWindow, Display*& pOutDisplay, Window& outWindow )
            {
                pOutDisplay = nullptr;
                outWindow   = 0;
                X11SurfaceHandles handles;
                if ( pWindow == nullptr || pWindow->tryGetX11Surface( handles ) == false )
                    return false;
                pOutDisplay = static_cast<Display*>( handles._pDisplay );
                outWindow   = static_cast<Window>( handles._window );
                return true;
            }

            /** @brief 창이 못 하는 동작을 알린 기록의 비트입니다(`markUnsupportedReported`). */
            static constexpr uint32 kUnsupportedPointerLockBit   = 1u << 0; ///< 포인터 묶기
            static constexpr uint32 kUnsupportedCursorShapeBit   = 1u << 1; ///< 커서 모양
            static constexpr uint32 kUnsupportedCursorVisibleBit = 1u << 2; ///< 커서 숨기기 · 보이기
            /** @brief 못 하는 동작을 이미 알린 비트의 모음입니다. */
            static inline atomic<uint32> s_unsupportedReportedMask{ 0 };

            /** @brief @p featureBit 를 알린 것으로 표시합니다. 처음이면 true — 검사와 표시를 `fetch_or` 한 번으로 해 두 스레드가 동시에 와도 알림은 한 번이다. */
            static bool markUnsupportedReported( uint32 featureBit ) { return ( s_unsupportedReportedMask.fetch_or( featureBit ) & featureBit ) == 0; }
            /** @brief Wayland 포인터의 마지막 자리입니다(버튼 이벤트에는 좌표가 없다). */
            static inline int2 s_waylandPointer{ 0, 0 };

            /** @brief 리눅스 이벤트 버튼 번호(Wayland `wl_pointer.button`)를 마우스 버튼으로 바꿉니다. 모르는 번호면 Count 입니다. */
            static MouseButton mapWaylandButton( uint32 button )
            {
                switch ( button )
                {
                    case BTN_LEFT:
                    {
                        return MouseButton::Left;
                    }
                    case BTN_MIDDLE:
                    {
                        return MouseButton::Middle;
                    }
                    case BTN_RIGHT:
                    {
                        return MouseButton::Right;
                    }
                    case BTN_SIDE:
                    {
                        return MouseButton::X1;
                    }
                    case BTN_EXTRA:
                    {
                        return MouseButton::X2;
                    }
                    default:
                    {
                        return MouseButton::Count;
                    }
                }
            }

            /** @brief X11 버튼 번호를 마우스 버튼으로 바꿉니다. 휠 번호(4 ~ 7)나 모르는 번호면 Count 입니다. */
            static MouseButton mapButton( uint32 xButton )
            {
                switch ( xButton )
                {
                    case Button1:
                    {
                        return MouseButton::Left;
                    }
                    case Button2:
                    {
                        return MouseButton::Middle;
                    }
                    case Button3:
                    {
                        return MouseButton::Right;
                    }
                    case 8:
                    {
                        return MouseButton::X1;
                    }
                    case 9:
                    {
                        return MouseButton::X2;
                    }
                    default:
                    {
                        return MouseButton::Count;
                    }
                }
            }

            /** @brief 휠 버튼 번호(4 ~ 7)가 눌렸을 때 그에 맞는 휠 이벤트를 큐에 넣습니다. 그 밖의 번호는 무시합니다. */
            static void postWheelButton( InputManager& manager, uint32 xButton )
            {
                switch ( xButton )
                {
                    case Button4:
                    {
                        manager.postRawEvent( RawInputEvent::makeMouseWheel( 1.0f ) );
                        break;
                    }
                    case Button5:
                    {
                        manager.postRawEvent( RawInputEvent::makeMouseWheel( -1.0f ) );
                        break;
                    }
                    case 6:
                    {
                        manager.postRawEvent( RawInputEvent::makeMouseHorizontalWheel( -1.0f ) );
                        break;
                    }
                    case 7:
                    {
                        manager.postRawEvent( RawInputEvent::makeMouseHorizontalWheel( 1.0f ) );
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }

            /** @brief 텍스트 입력: XIC 로 커밋된 UTF-8 문자열을 얻어 큐에 넣습니다(완전한 프리에딧 후보 창 렌더링은 구현하지 않음). */
            static void postTextInput( InputManager& manager, const XEvent& xev )
            {
                XIC pInputContext = getOrCreateInputContext();
                if ( pInputContext == nullptr )
                    return;
                fixed_string<constant::kMaxBuffer32> utf8Buf;
                KeySym                               lookupKeySym{};
                Status                               lookupStatus{};
                const int32                          byteCount = Xutf8LookupString( pInputContext, const_cast<XKeyPressedEvent*>( &xev.xkey ),
                                                                                    utf8Buf.data(), static_cast<int32>( utf8Buf.capacity() ), &lookupKeySym, &lookupStatus );
                if ( ( lookupStatus == XLookupChars || lookupStatus == XLookupBoth ) && byteCount > 0 )
                {
                    utf8Buf.data()[byteCount] = '\0';
                    const string_view svText( utf8Buf.data(), static_cast<size_t>( byteCount ) );
                    manager.postRawEvent( RawInputEvent::makeTextInput( svText ) );
                }
            }

            /** @brief 1x1 완전 투명 픽스맵으로 "보이지 않는 커서" 를 만들어 캐시합니다. */
            static Cursor getOrCreateInvisibleCursor( Display* pDisplay, Window x11Window )
            {
                // 커서 칸은 `setCursorVisiblePlatform` ← `syncMouseLock`(프레임 시작, 메인 스레드)에서만 닿는다 — `PlayerControllerComponent` 는 `_pMouse` 의 플래그만 바꾼다.
                SW_ASSERT( engine::isOnPoolWorker() == false );
                // 커서도 Display 소유 리소스다. 다른 Display 에서 그대로 쓰면 잘못된 리소스 id 가 된다.
                if ( s_invisibleCursor != 0 && s_pCursorDisplay == pDisplay )
                    return s_invisibleCursor;
                if ( s_invisibleCursor != 0 && s_pCursorDisplay != nullptr )
                {
                    XFreeCursor( static_cast<Display*>( s_pCursorDisplay ), s_invisibleCursor );
                    s_invisibleCursor = 0;
                }

                XColor      dummyColor{};
                const uint8 arrPixelData[1] = { 0 };
                Pixmap      pixmap          = XCreateBitmapFromData( pDisplay, x11Window, reinterpret_cast<const utf8*>( arrPixelData ), 1, 1 );
                s_invisibleCursor           = XCreatePixmapCursor( pDisplay, pixmap, pixmap, &dummyColor, &dummyColor, 0, 0 );
                XFreePixmap( pDisplay, pixmap );
                s_pCursorDisplay = pDisplay;
                return s_invisibleCursor;
            }

            /** @brief 모양의 X11 글꼴 커서 번호(`XC_*`)입니다. */
            static uint32 getFontCursorID( CursorShape shape )
            {
                switch ( shape )
                {
                    case CursorShape::Arrow:
                        return XC_left_ptr;
                    case CursorShape::Hand:
                        return XC_hand2;
                    case CursorShape::TextBeam:
                        return XC_xterm;
                    case CursorShape::ResizeHorizontal:
                        return XC_sb_h_double_arrow;
                    case CursorShape::ResizeVertical:
                        return XC_sb_v_double_arrow;
                    case CursorShape::NotAllowed:
                        return XC_X_cursor;
                }
                return XC_left_ptr;
            }

            /** @brief 모양의 글꼴 커서를 만들어 캐시합니다(Display 가 바뀌면 다시 만든다 — 옛 Display 의 리소스는 그 Display 와 함께 사라졌다). */
            static Cursor getOrCreateShapeCursor( Display* pDisplay, CursorShape shape )
            {
                if ( s_pShapeCursorDisplay != pDisplay )
                {
                    for ( Cursor& cursor : s_arrShapeCursor )
                    {
                        cursor = 0;
                    }
                    s_pShapeCursorDisplay = pDisplay;
                }
                Cursor& cached = s_arrShapeCursor[static_cast<size_t>( shape )];
                if ( cached == 0 )
                    cached = XCreateFontCursor( pDisplay, getFontCursorID( shape ) );
                return cached;
            }

            /** @brief XIM/XIC 와 커서를 해제합니다. 창이나 Display 가 사라지기 전에 불러야 합니다. */
            static void releaseAll()
            {
                for ( Cursor& cursor : s_arrShapeCursor )
                {
                    if ( cursor != 0 && s_pShapeCursorDisplay != nullptr )
                        XFreeCursor( static_cast<Display*>( s_pShapeCursorDisplay ), cursor );
                    cursor = 0;
                }
                s_pShapeCursorDisplay = nullptr;
                if ( s_pInputContext != nullptr )
                {
                    XDestroyIC( s_pInputContext );
                    s_pInputContext = nullptr;
                }
                if ( s_pInputMethod != nullptr )
                {
                    XCloseIM( s_pInputMethod );
                    s_pInputMethod = nullptr;
                }
                s_pImDisplay = nullptr;
                s_imWindow   = 0;

                if ( s_invisibleCursor != 0 && s_pCursorDisplay != nullptr )
                    XFreeCursor( static_cast<Display*>( s_pCursorDisplay ), s_invisibleCursor );
                s_invisibleCursor = 0;
                s_pCursorDisplay  = nullptr;
                s_bWindowFocused  = false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void InputManager::pollPlatform()
    {
        // 키보드는 X11 이벤트(KeyPress/KeyRelease)로 빠짐없이 들어오므로 폴링 폴백이 필요 없다
        // (Win32 의 GetAsyncKeyState 폴백은 메시지 유실을 메우기 위한 것이고, X11 에는 같은 문제가 없다).
        // 마우스는 창 밖에서 버튼을 뗀 경우처럼 이벤트를 놓칠 수 있는 경로가 있어 위치와 버튼을 보조로 폴링한다.
        MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pWindow == nullptr || _pMouse == nullptr )
            return;
        // 포인터가 창 안에 있고 포커스도 있으면 MotionNotify/ButtonRelease 가 빠짐없이 들어오므로
        // 보조 폴링이 필요 없다. XQueryPointer 는 서버와 동기 왕복하는 호출이라 프레임마다 부르면 그만큼 비용이 든다
        // (Win32 의 GetAsyncKeyState/GetCursorPos 는 값싼 호출이라 그쪽에는 없는 문제다).
        // 놓칠 수 있는 이벤트는 "창 밖에서 뗀 버튼" 이고, 그것은 포인터가 나갔거나 포커스를 잃은 경우에만 생긴다.
        if ( _pMouse->isPointerInside() && X11InputInternal::s_bWindowFocused )
            return;

        Display* pDisplay  = nullptr;
        Window   x11Window = 0;
        if ( X11InputInternal::tryGetX11( pWindow, pDisplay, x11Window ) == false )
            return; // X11 창이 아니면 해당 없음 — Wayland 는 위치를 물어볼 길이 없고 들어옴 · 나감 · 버튼이 빠짐없이 온다

        Window rootReturn{}, childReturn{};
        int32  rootX{}, rootY{}, winX{}, winY{};
        // XQueryPointer 의 mask_return 은 unsigned int 라 uint32 와 폭이 같다.
        uint32 maskReturn{};
        if ( XQueryPointer( pDisplay, x11Window, &rootReturn, &childReturn, &rootX, &rootY, &winX, &winY, &maskReturn ) )
        {
            _pMouse->setPosition( winX, winY );
            // 잠금을 다시 잡느라 삼킨 버튼은 떼어질 때까지 눌리지 않은 것으로 둔다(이벤트 경로와 같은 규칙).
            _pMouse->setButtonDown( MouseButton::Left, isMouseButtonConsumed( MouseButton::Left ) == false && ( maskReturn & Button1Mask ) != 0 );
            _pMouse->setButtonDown( MouseButton::Middle, isMouseButtonConsumed( MouseButton::Middle ) == false && ( maskReturn & Button2Mask ) != 0 );
            _pMouse->setButtonDown( MouseButton::Right, isMouseButtonConsumed( MouseButton::Right ) == false && ( maskReturn & Button3Mask ) != 0 );
        }
    }

    void InputManager::onNativeWindowEvent( const NativeWindowEvent& event )
    {
        processNativeEvent( event );
    }

    // 길이를 나누지 않는다: Win32 짝과 같이 이벤트 종류별로 갈라 주는 `switch` 디스패처다.
    void InputManager::processNativeEvent( const NativeWindowEvent& event )
    {
        if ( event._message == NativeWindowEvent::kMessageWayland && event._lParam != 0 )
        {
            const WaylandInputEvent& input = *reinterpret_cast<const WaylandInputEvent*>( event._lParam );
            // X11 분기와 같은 규칙이다 — 장치 상태는 바꾸지 않고 큐에만 넣는다. 키는 xkb keysym(= X11 KeySym)이라 같은 키 표를 쓴다.
            switch ( input._kind )
            {
                case WaylandInputKind::KeyDown:
                case WaylandInputKind::KeyUp:
                {
                    const Key  key   = InputKeyTable::mapX11KeySym( static_cast<uint64>( input._keySym ) );
                    const bool bDown = input._kind == WaylandInputKind::KeyDown;
                    if ( key == Key::LeftAlt || key == Key::RightAlt )
                        onPlatformAltChanged( bDown );
                    postRawEvent( bDown ? RawInputEvent::makeKeyDown( key ) : RawInputEvent::makeKeyUp( key ) );
                    break;
                }
                case WaylandInputKind::Text:
                {
                    postRawEvent( RawInputEvent::makeTextInput( string_view{ input._arrText } ) );
                    break;
                }
                case WaylandInputKind::PointerMove:
                {
                    X11InputInternal::s_waylandPointer = int2{ input._x, input._y };
                    postRawEvent( RawInputEvent::makeMouseMove( input._x, input._y ) );
                    break;
                }
                case WaylandInputKind::PointerDown:
                case WaylandInputKind::PointerUp:
                {
                    const bool        bDown  = input._kind == WaylandInputKind::PointerDown;
                    const MouseButton button = X11InputInternal::mapWaylandButton( input._button );
                    if ( button == MouseButton::Count )
                        break;
                    if ( bDown ? onPlatformPointerPressed( button ) : onPlatformPointerReleased( button ) )
                        break;
                    // Wayland 버튼 이벤트에는 좌표가 없다 — 마지막 움직임의 자리를 싣는다(0, 0 이면 커서가 왼쪽 위로 튄다).
                    postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( button, X11InputInternal::s_waylandPointer._x, X11InputInternal::s_waylandPointer._y )
                                        : RawInputEvent::makeMouseButtonUp( button, X11InputInternal::s_waylandPointer._x, X11InputInternal::s_waylandPointer._y ) );
                    break;
                }
                case WaylandInputKind::PointerWheel:
                {
                    if ( input._wheel != 0.0f )
                        postRawEvent( RawInputEvent::makeMouseWheel( input._wheel ) );
                    if ( input._horizontalWheel != 0.0f )
                        postRawEvent( RawInputEvent::makeMouseHorizontalWheel( input._horizontalWheel ) );
                    break;
                }
                case WaylandInputKind::PointerRelative:
                {
                    // 포인터가 잠긴 동안의 움직임이다(위치는 서 있다) — Win32 WM_INPUT 과 같이 위치 없이 이동량만 넘긴다.
                    postRawEvent( RawInputEvent::makeMouseRawDelta( input._relativeX, input._relativeY ) );
                    break;
                }
                case WaylandInputKind::PointerEnter:
                case WaylandInputKind::PointerLeave:
                {
                    postWindowStateEvent( RawInputEvent::makePointerCrossing( input._kind == WaylandInputKind::PointerEnter ) );
                    break;
                }
                case WaylandInputKind::FocusGained:
                case WaylandInputKind::FocusLost:
                {
                    const bool bFocused                = input._kind == WaylandInputKind::FocusGained;
                    X11InputInternal::s_bWindowFocused = bFocused;
                    onPlatformFocusChanged( bFocused );
                    postWindowStateEvent( RawInputEvent::makeFocusChange( bFocused ) );
                    break;
                }
            }
            return;
        }
        if ( event._message != NativeWindowEvent::kMessageX11 || event._lParam == 0 )
            return;

        const XEvent* pXev = reinterpret_cast<const XEvent*>( event._lParam );
        // 장치 상태는 여기서 바꾸지 않는다. 큐에만 넣고 `beginFrame` 이 순서대로 적용한다(Win32 와 같은 규칙,
        // `InputManager::beginFrame` 설명 참고). 여기서 상태를 바로 바꾸면 엣지가 사라진다.
        switch ( pXev->type )
        {
            case KeyPress:
            case KeyRelease:
            {
                // 누르고 있으면 X 서버는 기본으로 Release/Press 쌍을 반복해서 보낸다(자동 반복). 그대로 받으면 누르고 있는 동안
                // 눌림과 뗌 엣지가 계속 생긴다. 감지 가능한 자동 반복을 켜면 반복 이벤트는 Press 만 온다.
                X11InputInternal::enableDetectableAutoRepeat( pXev->xany.display );

                const KeySym keySym = XLookupKeysym( const_cast<XKeyEvent*>( &pXev->xkey ), 0 );
                const Key    key    = InputKeyTable::mapX11KeySym( static_cast<uint64>( keySym ) );
                const bool   bDown  = ( pXev->type == KeyPress );
                if ( key == Key::LeftAlt || key == Key::RightAlt )
                    onPlatformAltChanged( bDown );
                if ( bDown )
                    postRawEvent( RawInputEvent::makeKeyDown( key ) );
                else
                    postRawEvent( RawInputEvent::makeKeyUp( key ) );

                if ( bDown )
                    X11InputInternal::postTextInput( *this, *pXev );
                break;
            }
            case ButtonPress:
            case ButtonRelease:
            {
                const bool  bDown  = ( pXev->type == ButtonPress );
                const int32 mouseX = static_cast<int32>( pXev->xbutton.x );
                const int32 mouseY = static_cast<int32>( pXev->xbutton.y );
                // 버튼 이벤트에도 좌표를 실어 보낸다. 좌표 없이(0, 0) 보내면 클릭할 때마다 커서가 창 왼쪽 위로 튄다.
                const MouseButton button = X11InputInternal::mapButton( pXev->xbutton.button );
                if ( button == MouseButton::Count )
                {
                    if ( bDown )
                        X11InputInternal::postWheelButton( *this, pXev->xbutton.button );
                    break;
                }
                // 창 안의 누름만 여기로 온다(창 관리자의 제목 표시줄은 우리 창 밖이다). 포커스를 잃었다 돌아온 뒤 잠금을 다시 잡는 지점이다.
                // 잠금을 건 그 누름과 짝인 뗌은 게임에 넘기지 않는다(Win32 와 같은 규칙).
                if ( bDown ? onPlatformPointerPressed( button ) : onPlatformPointerReleased( button ) )
                    break;
                postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( button, mouseX, mouseY ) : RawInputEvent::makeMouseButtonUp( button, mouseX, mouseY ) );
                break;
            }
            case MotionNotify:
            {
                const int32 mouseX = static_cast<int32>( pXev->xmotion.x );
                const int32 mouseY = static_cast<int32>( pXev->xmotion.y );
                postRawEvent( RawInputEvent::makeMouseMove( mouseX, mouseY ) );
                break;
            }
            case EnterNotify:
            {
                postWindowStateEvent( RawInputEvent::makePointerCrossing( true ) );
                break;
            }
            case LeaveNotify:
            {
                postWindowStateEvent( RawInputEvent::makePointerCrossing( false ) );
                break;
            }
            // 포커스: OS 쪽 일(포인터 잡기와 놓기)은 지금 하고, 장치 상태 리셋은 큐 순서 안에서 한다. 포커스를 얻어도 잠금은
            // 다시 걸지 않는다. 제목 표시줄이나 닫기 버튼을 눌러 돌아온 사용자가 그 버튼까지 포인터를 가져갈 수 있어야 하기 때문이다.
            case FocusIn:
            {
                X11InputInternal::s_bWindowFocused = true;
                onPlatformFocusChanged( true );
                postWindowStateEvent( RawInputEvent::makeFocusChange( true ) );
                break;
            }
            case FocusOut:
            {
                X11InputInternal::s_bWindowFocused = false;
                onPlatformFocusChanged( false );
                postWindowStateEvent( RawInputEvent::makeFocusChange( false ) );
                break;
            }
            case ConfigureNotify:
            {
                syncMouseLock();
                break;
            }
            default:
            {
                break;
            }
        }
    }

    void InputManager::registerPlatformGamepads()
    {
        // 슬롯 수, 0번 캐시, 연결 콜백은 엔진 정책이라 기반 클래스가 맡는다. 여기서 정하는 것은 `/dev/input/js*`
        // 를 읽는 구현이라는 것뿐이다. 연결되지 않은 슬롯은 poll() 이 재시도 타이머로 넘어간다.
        registerGamepadSlots<LinuxGamepadDevice>();
    }

    void InputManager::setCursorVisiblePlatform( bool bVisible )
    {
        MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pWindow == nullptr )
            return;
        Display* pDisplay  = nullptr;
        Window   x11Window = 0;
        if ( X11InputInternal::tryGetX11( pWindow, pDisplay, x11Window ) == false )
        {
            // X11 이 아닌 창은 스스로 커서를 숨긴다(Wayland `wl_pointer.set_cursor`). 못 하면 커서가 그대로 보이므로 한 번 오류로 알린다.
            if ( pWindow->setCursorVisible( bVisible ) == false && X11InputInternal::markUnsupportedReported( X11InputInternal::kUnsupportedCursorVisibleBit ) )
                SW_LOG_ERROR( "This window cannot hide or show the cursor (Wayland needs a pointer device and a cursor theme) - the cursor stays as it is" );
            return;
        }

        if ( bVisible )
            XUndefineCursor( pDisplay, x11Window );
        else
            XDefineCursor( pDisplay, x11Window, X11InputInternal::getOrCreateInvisibleCursor( pDisplay, x11Window ) );
        XFlush( pDisplay );
    }

    void InputManager::applyCursorShapePlatform()
    {
        MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pWindow == nullptr )
            return;
        Display* pDisplay  = nullptr;
        Window   x11Window = 0;
        if ( X11InputInternal::tryGetX11( pWindow, pDisplay, x11Window ) == false )
        {
            // X11 이 아닌 창은 스스로 모양을 바꾼다(Wayland `wp_cursor_shape_manager_v1`). 컴포지터에 그 확장이 없으면 못 바꾸고, 기본 화살표를 청한 것이면 이미 그 모양이라 알릴 일이 없다.
            // 다른 모양을 청했는데 못 바꾸는 것은 오류로 한 번 알린다(조용히 건너뛰지 않는다).
            const bool bApplied     = pWindow->supportsCursorShape() && pWindow->setCursorShape( _cursorShape );
            const bool bNeedsReport = bApplied == false && _cursorShape != CursorShape::Arrow;
            if ( bNeedsReport && X11InputInternal::markUnsupportedReported( X11InputInternal::kUnsupportedCursorShapeBit ) )
                SW_LOG_ERROR( "This window cannot change the cursor shape (Wayland needs the compositor's cursor-shape-v1) - the cursor keeps its current shape" );
            return;
        }

        XDefineCursor( pDisplay, x11Window, X11InputInternal::getOrCreateShapeCursor( pDisplay, _cursorShape ) );
        XFlush( pDisplay );
    }

    void InputManager::applyMouseLockMode()
    {
        if ( _pMouse == nullptr )
            return;

        MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pWindow == nullptr )
            return;
        Display* pDisplay  = nullptr;
        Window   x11Window = 0;
        if ( X11InputInternal::tryGetX11( pWindow, pDisplay, x11Window ) == false )
        {
            // X11 이 아닌 창은 포인터를 스스로 묶는다 — 클라이언트가 포인터를 옮길 수 없는 Wayland 는 가두기(ConfinedToWindow) · 잠그기(LockedInCenter)를 zwp_pointer_constraints 로 컴포지터에 청한다.
            // 잠긴 포인터는 가운데로 되돌릴 필요가 없다(제자리에 서 있고 상대 이동만 온다). 못 걸면 잠그지 않은 채로 한 번 오류로 알린다.
            // "X11 연결 전"과 헷갈리지 않는다: 창은 `initializeWindow` 가 성공한 뒤에야 `MainWindowSubsystem` 에 들어오고(`App.cpp` adopt), 다시 만들기(`recreate`)는 같은 스레드에서 끝나므로 여기 온 X11 창은 항상 연결이 있다.
            // 그래서 이 갈래는 X11 이 아닌 창뿐이다. 실패해도 상태가 바뀌지 않아 다음 동기화가 다시 청한다(포인터 장치가 늦게 생긴 경우).
            const MouseLockMode     mode = _pMouse->getLockMode();
            const WindowPointerLock lock = mode == MouseLockMode::LockedInCenter ? WindowPointerLock::Locked : ( mode == MouseLockMode::ConfinedToWindow ? WindowPointerLock::Confined : WindowPointerLock::Released );
            if ( pWindow->setPointerLock( lock ) == false && X11InputInternal::markUnsupportedReported( X11InputInternal::kUnsupportedPointerLockBit ) )
                SW_LOG_ERROR( "This window cannot lock the pointer (Wayland needs the compositor's pointer-constraints, relative-pointer for a centre lock, and a pointer device) - the pointer stays free" );
            return;
        }

        // 포커스가 없는 창은 포인터를 잡지 않는다. 잡으면 창 관리자의 장식(제목 표시줄)에도 갈 수 없다.
        if ( X11InputInternal::s_bWindowFocused == false )
        {
            XUngrabPointer( pDisplay, CurrentTime );
            XFlush( pDisplay );
            return;
        }

        // X11 XGrabPointer 는 창 전체에만 가둘 수 있다. 임의의 서브 사각형에 가두는 기능은 네이티브로
        // 없어서, 포인터를 MotionNotify 마다 되돌리는 소프트웨어 클리핑이 필요하다(여기서는 구현하지 않는다).
        const uint32 mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
        // owner_events=1(True), confine_to/cursor 뒤 두 인자는 각각 x11Window/None(0). X11MacroUndef.h 가
        // True/None 매크로를 지우므로 리터럴 값을 쓴다.
        XGrabPointer( pDisplay, x11Window, 1, mask, GrabModeAsync, GrabModeAsync, x11Window, 0, CurrentTime );
        XFlush( pDisplay );

        if ( _pMouse->getLockMode() == MouseLockMode::LockedInCenter )
            recenterLockedCursorPlatform();
    }

    void InputManager::recenterLockedCursorPlatform()
    {
        const bool bCannotRecenter = _pMouse == nullptr || _pMouse->getLockMode() != MouseLockMode::LockedInCenter || X11InputInternal::s_bWindowFocused == false;
        if ( bCannotRecenter )
            return;

        MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pWindow == nullptr )
            return;
        Display* pDisplay  = nullptr;
        Window   x11Window = 0;
        if ( X11InputInternal::tryGetX11( pWindow, pDisplay, x11Window ) == false )
            return; // X11 창이 아니면 해당 없음 — Wayland 는 포인터를 옮길 수 없고 잠긴 포인터는 되돌릴 필요가 없다(`applyMouseLockMode`)

        XWindowAttributes attrs{};
        if ( XGetWindowAttributes( pDisplay, x11Window, &attrs ) == 0 )
            return;
        const int2 center{ attrs.width / 2, attrs.height / 2 };
        if ( _pMouse->getPosition() == center )
            return;

        XWarpPointer( pDisplay, 0, x11Window, 0, 0, 0, 0, center._x, center._y );
        XFlush( pDisplay );
        // 되돌림은 사용자가 움직인 것이 아니다. XWarpPointer 가 만드는 MotionNotify(가운데)는 다음 프레임에 델타 0 으로 들어온다.
        _pMouse->setPositionWithoutDelta( center._x, center._y );
    }

    void InputManager::releaseMouseLockMode()
    {
        MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pWindow == nullptr )
            return;
        Display* pDisplay  = nullptr;
        Window   x11Window = 0;
        if ( X11InputInternal::tryGetX11( pWindow, pDisplay, x11Window ) == false )
        {
            (void)pWindow->setPointerLock( WindowPointerLock::Released ); // 풀기는 항상 되고, 걸린 것이 없으면 아무 일도 없다
            return;
        }

        XUngrabPointer( pDisplay, CurrentTime );
        XFlush( pDisplay );
    }

    bool InputManager::isWindowFocusedPlatform() const
    {
        // X11 포커스는 FocusIn/FocusOut 으로만 알 수 있다(서버에 묻는 XGetInputFocus 는 왕복 호출이다). 창이 뜬 직후 FocusIn 전이면 false 이므로 첫 클릭에 잡는다.
        const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        const bool                 bHasWindow  = pWindowHost != nullptr && pWindowHost->getWindow() != nullptr;
        return _bWindowFocused == SW_TRUE && ( bHasWindow == false || X11InputInternal::s_bWindowFocused );
    }

    void InputManager::disableWindowsAccessibilityShortcuts()
    {
        // 이름은 Windows API 시절 이름을 그대로 쓰지만(공용 InputManager.h 의 공개 API), 여기서는 X11
        // AccessX(XKB StickyKeys/SlowKeys/BounceKeys)를 억제한다. 게임 도중 방향키를 연타하다
        // AccessX 팝업이 뜨는 것을 막기 위해서다.
        MainWindowSubsystem* pWindowHost       = engine::findSubsystem<MainWindowSubsystem>();
        IWindow*             pWindow           = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        const bool           bNothingToDisable = pWindow == nullptr || X11InputInternal::s_bAccessibilityDisabled;
        if ( bNothingToDisable )
            return;

        Display* pDisplay  = nullptr;
        Window   x11Window = 0;
        if ( X11InputInternal::tryGetX11( pWindow, pDisplay, x11Window ) == false )
            return; // X11 창이 아니면 해당 없음 — Wayland 에는 클라이언트가 끌 AccessX 가 없다(컴포지터의 설정이다)

        XkbDescPtr pXkb = XkbAllocKeyboard();
        if ( pXkb == nullptr )
            return;

        // 반환값 0 은 X11 Success 이다. X11MacroUndef.h 가 Success 매크로를 지우므로 리터럴 0 을 쓴다.
        if ( XkbGetControls( pDisplay, XkbAllControlsMask, pXkb ) == 0 && pXkb->ctrls != nullptr )
        {
            X11InputInternal::s_prevXkbEnabledControls = pXkb->ctrls->enabled_ctrls;
            pXkb->ctrls->enabled_ctrls &= static_cast<uint32>( ~( XkbStickyKeysMask | XkbSlowKeysMask | XkbBounceKeysMask | XkbAccessXKeysMask ) );
            XkbSetControls( pDisplay, XkbControlsEnabledMask, pXkb );
            X11InputInternal::s_bAccessibilityDisabled = true;
        }
        XkbFreeKeyboard( pXkb, 0, 1 ); // freeDesc=1(True). X11MacroUndef.h 가 True 매크로를 지운다.
    }

    void InputManager::restoreWindowsAccessibilityShortcuts()
    {
        // 이 함수는 InputManager::shutdown 이 부르는 유일한 플랫폼 훅이라, XIM/XIC 와 커서 해제도 여기서 한다.
        // Display 가 닫히기 전에 해제해야 X 리소스가 남지 않는다.
        X11InputInternal::releaseAll();

        MainWindowSubsystem* pWindowHost       = engine::findSubsystem<MainWindowSubsystem>();
        IWindow*             pWindow           = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        const bool           bNothingToRestore = pWindow == nullptr || X11InputInternal::s_bAccessibilityDisabled == false;
        if ( bNothingToRestore )
            return;

        Display* pDisplay  = nullptr;
        Window   x11Window = 0;
        if ( X11InputInternal::tryGetX11( pWindow, pDisplay, x11Window ) == false )
            return; // 끈 적이 없는 창(X11 이 아니다)이다

        XkbDescPtr pXkb = XkbAllocKeyboard();
        if ( pXkb == nullptr )
            return;

        if ( XkbGetControls( pDisplay, XkbAllControlsMask, pXkb ) == 0 && pXkb->ctrls != nullptr )
        {
            pXkb->ctrls->enabled_ctrls = X11InputInternal::s_prevXkbEnabledControls;
            XkbSetControls( pDisplay, XkbControlsEnabledMask, pXkb );
            X11InputInternal::s_bAccessibilityDisabled = false;
        }
        XkbFreeKeyboard( pXkb, 0, 1 );
    }
} // namespace sw

#endif
