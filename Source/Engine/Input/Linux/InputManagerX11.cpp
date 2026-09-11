#include "pch.h"

#include "Engine/Input/InputManager.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/String/fixed_string.h"

    #include "Engine/Input/Devices/GamepadDevice.h"
    #include "Engine/Input/Devices/KeyboardDevice.h"
    #include "Engine/Input/Devices/MouseDevice.h"
    #include "Engine/Input/Events/RawInputEvent.h"
    #include "Engine/Input/InputKeyMap.h"
    #include "Engine/Input/Linux/GamepadJoystick.h"
    #include "Engine/Window/IWindow.h"
    #include "Engine/Window/NativeWindowEvent.h"

    #include <X11/XKBlib.h>

namespace sw
{
    namespace
    {
        /**
         * @brief 텍스트 입력(XIM/XIC)과 접근성(XKB) 상태를 창 생명주기와 별개로 지연 초기화해 보관합니다.
         * @note XIM은 "on-the-spot"/"root-window" 스타일 입력기의 커밋 문자열은 잡아내지만, 조합(preedit)
         *       후보 창을 직접 그려주는 완전한 프리에딧 렌더링은 구현하지 않았습니다.
         */
        struct X11InputInternal
        {
            static inline XIM    s_pInputMethod{ nullptr };
            static inline XIC    s_pInputContext{ nullptr };
            static inline void*  s_pImDisplay{ nullptr };     ///< XIC를 만들 때 쓴 Display*.
            static inline Window s_imWindow{ 0 };             ///< XIC의 XNClientWindow. **창이 바뀌면 반드시 재생성해야 한다.**
            static inline Cursor s_invisibleCursor{ 0 };      ///< X11 None(리소스 없음). X11MacroUndef.h가 None 매크로를 지우므로 리터럴 0을 씁니다.
            static inline void*  s_pCursorDisplay{ nullptr }; ///< s_invisibleCursor 를 만든 Display* (다르면 다시 만든다).
            static inline bool   s_bAccessibilityDisabled{ false };
            /** @brief 창이 X11 포커스를 쥐고 있는가 (FocusIn/FocusOut 로 갱신). 보조 폴링 생략 판단에 쓴다. */
            static inline bool   s_bWindowFocused{ false };
            static inline uint32 s_prevXkbEnabledControls{ 0 };

            /** @brief 활성 창에 대한 XIC를 지연 생성해 반환합니다 (실패하면 nullptr). */
            static XIC getOrCreateInputContext()
            {
                IWindow* pWindow = IWindow::getActiveWindow();
                if ( pWindow == nullptr )
                    return nullptr;

                Display* pDisplay  = static_cast<Display*>( pWindow->getNativeDisplay() );
                Window   x11Window = static_cast<Window>( reinterpret_cast<uintptr_t>( pWindow->getNativeHandle() ) );
                if ( pDisplay == nullptr || x11Window == 0 )
                    return nullptr;

                // 예전엔 Display 만 비교했다. 같은 X 서버에서 창을 다시 만들면(에디터 창 재생성, 테스트가
                // 창을 반복 생성/파괴) 캐시된 XIC 가 **이미 파괴된 Window** 를 XNClientWindow 로 물고 있어
                // 텍스트 입력이 조용히 죽는다. 창까지 함께 봐야 한다.
                if ( s_pInputContext != nullptr && s_pImDisplay == pDisplay && s_imWindow == x11Window )
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

            /** @brief 1x1 완전 투명 픽스맵으로 "보이지 않는 커서"를 만들어 캐싱합니다. */
            static Cursor getOrCreateInvisibleCursor( Display* pDisplay, Window x11Window )
            {
                // 커서도 Display 소유 리소스다 — 다른 Display 에서 그대로 쓰면 잘못된 리소스 id 가 된다.
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

            /** @brief XIM/XIC 와 커서를 해제합니다. 창이나 Display 가 사라지기 전에 불러야 합니다. */
            static void releaseAll()
            {
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
        // 키보드는 X11 이벤트(KeyPress/KeyRelease)로 빠짐없이 들어오므로 폴링 폴백이 필요 없습니다
        // (Win32의 GetAsyncKeyState 폴백은 메시지 유실을 보완하기 위한 것으로, X11엔 대응 문제가 없음).
        // 마우스는 창 밖에서 버튼을 뗀 경우 등 이벤트를 놓칠 수 있는 경로가 있어 위치/버튼을 보조로 폴링합니다.
        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr || _pMouse == nullptr )
            return;

        // 포인터가 창 안에 있고 포커스도 있으면 MotionNotify/ButtonRelease 가 빠짐없이 들어오므로
        // 보조 폴링이 필요 없다. XQueryPointer 는 **서버 동기 왕복**이라 프레임마다 물면 그대로 비용이다
        // (Win32 의 GetAsyncKeyState/GetCursorPos 는 값싼 호출이라 그쪽엔 없던 문제다).
        // 놓칠 수 있는 건 "창 밖에서 뗀 버튼" 이고 그건 포인터가 나갔거나 포커스를 잃은 경우다.
        if ( _pMouse->isPointerInside() && X11InputInternal::s_bWindowFocused )
            return;

        Display* pDisplay  = static_cast<Display*>( pWindow->getNativeDisplay() );
        Window   x11Window = static_cast<Window>( reinterpret_cast<uintptr_t>( pWindow->getNativeHandle() ) );
        if ( pDisplay == nullptr || x11Window == 0 )
            return;

        Window rootReturn{}, childReturn{};
        int32  rootX{}, rootY{}, winX{}, winY{};
        // XQueryPointer가 요구하는 포인터 폭과 uint32가 동일합니다.
        uint32 maskReturn{};
        if ( XQueryPointer( pDisplay, x11Window, &rootReturn, &childReturn, &rootX, &rootY, &winX, &winY, &maskReturn ) )
        {
            _pMouse->setPosition( winX, winY );
            _pMouse->setButtonDown( MouseButton::Left, ( maskReturn & Button1Mask ) != 0 );
            _pMouse->setButtonDown( MouseButton::Middle, ( maskReturn & Button2Mask ) != 0 );
            _pMouse->setButtonDown( MouseButton::Right, ( maskReturn & Button3Mask ) != 0 );
        }
    }

    void InputManager::onNativeWindowEvent( const NativeWindowEvent& event )
    {
        processNativeEvent( event );
    }

    void InputManager::processNativeEvent( const NativeWindowEvent& event )
    {
        if ( event._message != NativeWindowEvent::kMessageX11 || event._lParam == 0 )
            return;

        const XEvent* pXev = reinterpret_cast<const XEvent*>( event._lParam );
        switch ( pXev->type )
        {
            case KeyPress:
            case KeyRelease:
            {
                const KeySym keySym = XLookupKeysym( const_cast<XKeyEvent*>( &pXev->xkey ), 0 );
                const Key    key    = InputKeyMap::mapX11KeySym( static_cast<uint64>( keySym ) );
                const bool   bDown  = ( pXev->type == KeyPress );
                if ( _pKeyboard != nullptr )
                    _pKeyboard->setKeyDown( key, bDown );
                if ( bDown )
                    postRawEvent( RawInputEvent::makeKeyDown( key ) );
                else
                    postRawEvent( RawInputEvent::makeKeyUp( key ) );

                // 텍스트 입력: XIC를 통해 커밋된 UTF-8 문자열을 얻습니다 (완전한 프리에딧 후보창 렌더링은 미구현).
                if ( bDown )
                {
                    XIC pInputContext = X11InputInternal::getOrCreateInputContext();
                    if ( pInputContext != nullptr )
                    {
                        fixed_string<constant::kMaxBuffer32> utf8Buf;
                        KeySym                               lookupKeySym{};
                        Status                               lookupStatus{};
                        const int32                          byteCount = Xutf8LookupString( pInputContext, const_cast<XKeyPressedEvent*>( &pXev->xkey ),
                                                                                            utf8Buf.data(), static_cast<int32>( utf8Buf.capacity() ), &lookupKeySym, &lookupStatus );
                        if ( ( lookupStatus == XLookupChars || lookupStatus == XLookupBoth ) && byteCount > 0 )
                        {
                            utf8Buf.data()[byteCount] = '\0';
                            const string_view svText( utf8Buf.data(), static_cast<size_t>( byteCount ) );
                            postRawEvent( RawInputEvent::makeTextInput( svText ) );
                        }
                    }
                }
                break;
            }
            case ButtonPress:
            case ButtonRelease:
            {
                const bool bDown = ( pXev->type == ButtonPress );
                switch ( pXev->xbutton.button )
                {
                    case Button1:
                        if ( _pMouse != nullptr )
                            _pMouse->setButtonDown( MouseButton::Left, bDown );
                        postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( MouseButton::Left ) : RawInputEvent::makeMouseButtonUp( MouseButton::Left ) );
                        break;
                    case Button2:
                        if ( _pMouse != nullptr )
                            _pMouse->setButtonDown( MouseButton::Middle, bDown );
                        postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( MouseButton::Middle ) : RawInputEvent::makeMouseButtonUp( MouseButton::Middle ) );
                        break;
                    case Button3:
                        if ( _pMouse != nullptr )
                            _pMouse->setButtonDown( MouseButton::Right, bDown );
                        postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( MouseButton::Right ) : RawInputEvent::makeMouseButtonUp( MouseButton::Right ) );
                        break;
                    case Button4:
                        if ( bDown )
                        {
                            if ( _pMouse != nullptr )
                                _pMouse->addWheelDelta( 1.0f );
                            postRawEvent( RawInputEvent::makeMouseWheel( 1.0f ) );
                        }
                        break;
                    case Button5:
                        if ( bDown )
                        {
                            if ( _pMouse != nullptr )
                                _pMouse->addWheelDelta( -1.0f );
                            postRawEvent( RawInputEvent::makeMouseWheel( -1.0f ) );
                        }
                        break;
                    case 6:
                        if ( bDown )
                        {
                            if ( _pMouse != nullptr )
                                _pMouse->addHorizontalWheelDelta( -1.0f );
                            postRawEvent( RawInputEvent::makeMouseHorizontalWheel( -1.0f ) );
                        }
                        break;
                    case 7:
                        if ( bDown )
                        {
                            if ( _pMouse != nullptr )
                                _pMouse->addHorizontalWheelDelta( 1.0f );
                            postRawEvent( RawInputEvent::makeMouseHorizontalWheel( 1.0f ) );
                        }
                        break;
                    case 8:
                        if ( _pMouse != nullptr )
                            _pMouse->setButtonDown( MouseButton::X1, bDown );
                        postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( MouseButton::X1 ) : RawInputEvent::makeMouseButtonUp( MouseButton::X1 ) );
                        break;
                    case 9:
                        if ( _pMouse != nullptr )
                            _pMouse->setButtonDown( MouseButton::X2, bDown );
                        postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( MouseButton::X2 ) : RawInputEvent::makeMouseButtonUp( MouseButton::X2 ) );
                        break;
                    default:
                        break;
                }
                break;
            }
            case MotionNotify:
            {
                const int32 mouseX = static_cast<int32>( pXev->xmotion.x );
                const int32 mouseY = static_cast<int32>( pXev->xmotion.y );
                if ( _pMouse != nullptr )
                    _pMouse->setPosition( mouseX, mouseY );
                postRawEvent( RawInputEvent::makeMouseMove( mouseX, mouseY ) );
                break;
            }
            case EnterNotify:
                if ( _pMouse != nullptr )
                    _pMouse->setPointerInsideState( true );
                break;
            case LeaveNotify:
                if ( _pMouse != nullptr )
                    _pMouse->setPointerInsideState( false );
                break;
            case FocusIn:
                X11InputInternal::s_bWindowFocused = true;
                onWindowFocusGained();
                break;
            case FocusOut:
                X11InputInternal::s_bWindowFocused = false;
                onWindowFocusLost();
                break;
            case ConfigureNotify:
                applyMouseLockMode();
                break;
            default:
                break;
        }
    }

    void InputManager::registerPlatformGamepads()
    {
        // 최대 4개 컨트롤러 (/dev/input/js0 ~ js3). 연결되지 않은 슬롯은 poll()이 알아서 재시도 타이머로 넘어갑니다.
        for ( uint32 padIdx = 0; padIdx < 4; ++padIdx )
        {
            auto pGamepad = make_unique<GamepadJoystick>( padIdx );
            if ( padIdx == 0 )
                _pGamepad = pGamepad.get();
            pGamepad->setConnectionCallback( [this]( uint32 index, bool bConnected )
            {
                if ( _onGamepadConnectionChanged.isBound() )
                    _onGamepadConnectionChanged( index, bConnected );
            } );
            registerDevice( std::move( pGamepad ) );
        }
    }

    void InputManager::setCursorVisiblePlatform( bool bVisible )
    {
        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr )
            return;

        Display* pDisplay  = static_cast<Display*>( pWindow->getNativeDisplay() );
        Window   x11Window = static_cast<Window>( reinterpret_cast<uintptr_t>( pWindow->getNativeHandle() ) );
        if ( pDisplay == nullptr || x11Window == 0 )
            return;

        if ( bVisible )
            XUndefineCursor( pDisplay, x11Window );
        else
            XDefineCursor( pDisplay, x11Window, X11InputInternal::getOrCreateInvisibleCursor( pDisplay, x11Window ) );
        XFlush( pDisplay );
    }

    void InputManager::applyMouseLockMode()
    {
        if ( _pMouse == nullptr )
            return;

        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr )
            return;

        Display* pDisplay  = static_cast<Display*>( pWindow->getNativeDisplay() );
        Window   x11Window = static_cast<Window>( reinterpret_cast<uintptr_t>( pWindow->getNativeHandle() ) );
        if ( pDisplay == nullptr || x11Window == 0 )
            return;

        const MouseLockMode lockMode = _pMouse->getLockMode();
        if ( lockMode == MouseLockMode::None )
        {
            XUngrabPointer( pDisplay, CurrentTime );
            XFlush( pDisplay );
            return;
        }

        // X11 XGrabPointer는 창 전체에만 가둘 수 있습니다 (임의의 서브 사각형 confine은 네이티브 지원이
        // 없어, 포인터를 매 MotionNotify마다 되돌리는 소프트웨어 클리핑이 필요합니다 — 여기선 미구현).
        const uint32 mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
        // owner_events=1(True), confine_to/cursor 뒤 두 인자는 각각 x11Window/None(0). X11MacroUndef.h가
        // True/None 매크로를 지우므로 리터럴 값을 씁니다.
        XGrabPointer( pDisplay, x11Window, 1, mask, GrabModeAsync, GrabModeAsync, x11Window, 0, CurrentTime );

        if ( lockMode == MouseLockMode::LockedInCenter )
        {
            XWindowAttributes attrs{};
            XGetWindowAttributes( pDisplay, x11Window, &attrs );
            XWarpPointer( pDisplay, 0, x11Window, 0, 0, 0, 0, attrs.width / 2, attrs.height / 2 );
        }
        XFlush( pDisplay );
    }

    void InputManager::releaseMouseLockMode()
    {
        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr )
            return;

        Display* pDisplay = static_cast<Display*>( pWindow->getNativeDisplay() );
        if ( pDisplay == nullptr )
            return;

        XUngrabPointer( pDisplay, CurrentTime );
        XFlush( pDisplay );
    }

    void InputManager::disableWindowsAccessibilityShortcuts()
    {
        // 이름은 Windows API 시절 이름을 그대로 쓰지만(공용 InputManager.h의 공개 API), 여기선 X11
        // AccessX(XKB StickyKeys/SlowKeys/BounceKeys)를 억제합니다 — 게임 도중 방향키를 연타하다
        // AccessX 팝업이 뜨는 것을 막기 위함입니다.
        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr || X11InputInternal::s_bAccessibilityDisabled )
            return;

        Display* pDisplay = static_cast<Display*>( pWindow->getNativeDisplay() );
        if ( pDisplay == nullptr )
            return;

        XkbDescPtr pXkb = XkbAllocKeyboard();
        if ( pXkb == nullptr )
            return;

        // 반환값 0 == X11 Success. X11MacroUndef.h가 Success 매크로를 지우므로 리터럴 0을 씁니다.
        if ( XkbGetControls( pDisplay, XkbAllControlsMask, pXkb ) == 0 && pXkb->ctrls != nullptr )
        {
            X11InputInternal::s_prevXkbEnabledControls = pXkb->ctrls->enabled_ctrls;
            pXkb->ctrls->enabled_ctrls &= static_cast<uint32>( ~( XkbStickyKeysMask | XkbSlowKeysMask | XkbBounceKeysMask | XkbAccessXKeysMask ) );
            XkbSetControls( pDisplay, XkbControlsEnabledMask, pXkb );
            X11InputInternal::s_bAccessibilityDisabled = true;
        }
        XkbFreeKeyboard( pXkb, 0, 1 ); // freeDesc=1(True). X11MacroUndef.h가 True 매크로를 지웁니다.
    }

    void InputManager::restoreWindowsAccessibilityShortcuts()
    {
        // 이 함수가 InputManager::shutdown 이 부르는 유일한 플랫폼 훅이라, XIM/XIC/커서 해제도 여기서 한다.
        // Display 가 닫히기 전에 풀어야 X 리소스가 남지 않는다.
        X11InputInternal::releaseAll();

        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr || X11InputInternal::s_bAccessibilityDisabled == false )
            return;

        Display* pDisplay = static_cast<Display*>( pWindow->getNativeDisplay() );
        if ( pDisplay == nullptr )
            return;

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
