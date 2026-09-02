#include "pch.h"

#include "Engine/Input/InputManager.h"

#if defined( SW_PLATFORM_LINUX )
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
            static inline void*  s_pImWindow{ nullptr }; ///< XIC를 만들 때 사용한 Display* (창이 바뀌면 재생성).
            static inline Cursor s_invisibleCursor{ None };
            static inline bool   s_bAccessibilityDisabled{ false };
            static inline uint32 s_prevXkbEnabledControls{ 0 };

            /** @brief 활성 창에 대한 XIC를 지연 생성해 반환합니다 (실패하면 nullptr). */
            static XIC getOrCreateInputContext()
            {
                IWindow* pWindow = IWindow::getActiveWindow();
                if ( pWindow == nullptr )
                    return nullptr;

                Display* pDisplay  = static_cast<Display*>( pWindow->getNativeDisplay() );
                Window   x11Window = static_cast<Window>( reinterpret_cast<uintptr_t>( pWindow->getNativeHandle() ) );
                if ( pDisplay == nullptr || x11Window == None )
                    return nullptr;

                if ( s_pInputContext != nullptr && s_pImWindow == pDisplay )
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
                s_pImWindow     = pDisplay;
                return s_pInputContext;
            }

            /** @brief 1x1 완전 투명 픽스맵으로 "보이지 않는 커서"를 만들어 캐싱합니다. */
            static Cursor getOrCreateInvisibleCursor( Display* pDisplay, Window x11Window )
            {
                if ( s_invisibleCursor != None )
                    return s_invisibleCursor;

                XColor      dummyColor{};
                const uint8 arrPixelData[1] = { 0 };
                Pixmap      pixmap          = XCreateBitmapFromData( pDisplay, x11Window, reinterpret_cast<const utf8*>( arrPixelData ), 1, 1 );
                s_invisibleCursor           = XCreatePixmapCursor( pDisplay, pixmap, pixmap, &dummyColor, &dummyColor, 0, 0 );
                XFreePixmap( pDisplay, pixmap );
                return s_invisibleCursor;
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

        Display* pDisplay  = static_cast<Display*>( pWindow->getNativeDisplay() );
        Window   x11Window = static_cast<Window>( reinterpret_cast<uintptr_t>( pWindow->getNativeHandle() ) );
        if ( pDisplay == nullptr || x11Window == None )
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
                        utf8        arrUtf8[32]{};
                        KeySym      lookupKeySym{};
                        Status      lookupStatus{};
                        const int32 byteCount = Xutf8LookupString( pInputContext, const_cast<XKeyPressedEvent*>( &pXev->xkey ),
                                                                   arrUtf8, static_cast<int32>( sizeof( arrUtf8 ) - 1 ), &lookupKeySym, &lookupStatus );
                        if ( ( lookupStatus == XLookupChars || lookupStatus == XLookupBoth ) && byteCount > 0 )
                        {
                            arrUtf8[byteCount] = '\0';
                            const string_view svText( arrUtf8, static_cast<size_t>( byteCount ) );
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
                const int32 mx = static_cast<int32>( pXev->xmotion.x );
                const int32 my = static_cast<int32>( pXev->xmotion.y );
                if ( _pMouse != nullptr )
                    _pMouse->setPosition( mx, my );
                postRawEvent( RawInputEvent::makeMouseMove( mx, my ) );
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
                onWindowFocusGained();
                break;
            case FocusOut:
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
        if ( pDisplay == nullptr || x11Window == None )
            return;

        if ( bVisible )
        {
            XUndefineCursor( pDisplay, x11Window );
        }
        else
        {
            XDefineCursor( pDisplay, x11Window, X11InputInternal::getOrCreateInvisibleCursor( pDisplay, x11Window ) );
        }
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
        if ( pDisplay == nullptr || x11Window == None )
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
        XGrabPointer( pDisplay, x11Window, True, mask, GrabModeAsync, GrabModeAsync, x11Window, None, CurrentTime );

        if ( lockMode == MouseLockMode::LockedInCenter )
        {
            XWindowAttributes attrs{};
            XGetWindowAttributes( pDisplay, x11Window, &attrs );
            XWarpPointer( pDisplay, None, x11Window, 0, 0, 0, 0, attrs.width / 2, attrs.height / 2 );
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

        if ( XkbGetControls( pDisplay, XkbAllControlsMask, pXkb ) == Success && pXkb->ctrls != nullptr )
        {
            X11InputInternal::s_prevXkbEnabledControls = pXkb->ctrls->enabled_ctrls;
            pXkb->ctrls->enabled_ctrls &= static_cast<uint32>( ~( XkbStickyKeysMask | XkbSlowKeysMask | XkbBounceKeysMask | XkbAccessXKeysMask ) );
            XkbSetControls( pDisplay, XkbControlsEnabledMask, pXkb );
            X11InputInternal::s_bAccessibilityDisabled = true;
        }
        XkbFreeKeyboard( pXkb, 0, True );
    }

    void InputManager::restoreWindowsAccessibilityShortcuts()
    {
        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr || X11InputInternal::s_bAccessibilityDisabled == false )
            return;

        Display* pDisplay = static_cast<Display*>( pWindow->getNativeDisplay() );
        if ( pDisplay == nullptr )
            return;

        XkbDescPtr pXkb = XkbAllocKeyboard();
        if ( pXkb == nullptr )
            return;

        if ( XkbGetControls( pDisplay, XkbAllControlsMask, pXkb ) == Success && pXkb->ctrls != nullptr )
        {
            pXkb->ctrls->enabled_ctrls = X11InputInternal::s_prevXkbEnabledControls;
            XkbSetControls( pDisplay, XkbControlsEnabledMask, pXkb );
            X11InputInternal::s_bAccessibilityDisabled = false;
        }
        XkbFreeKeyboard( pXkb, 0, True );
    }
} // namespace sw

#endif
