#include "pch.h"

#include "Engine/Input/InputManager.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/Defines.h"
    #include "Core/String/fixed_string.h"

    #include "Engine/Input/Devices/GamepadDevice.h"
    #include "Engine/Input/Devices/KeyboardDevice.h"
    #include "Engine/Input/Devices/MouseDevice.h"
    #include "Engine/Input/Events/RawInputEvent.h"
    #include "Engine/Input/InputKeyMap.h"
    #include "Engine/Input/Windows/GamepadXInput.h"
    #include "Engine/Window/IWindow.h"
    #include "Engine/Window/NativeWindowEvent.h"

    #include <imm.h>
    #pragma comment( lib, "imm32.lib" )

namespace sw
{
    namespace
    {
        uint8 getWin32ModifierMaskInternal()
        {
            uint8 mask = ModifierKey::None;
            if ( ( GetKeyState( VK_CONTROL ) & 0x8000 ) != 0 )
                mask |= ModifierKey::Ctrl;
            if ( ( GetKeyState( VK_SHIFT ) & 0x8000 ) != 0 )
                mask |= ModifierKey::Shift;
            if ( ( GetKeyState( VK_MENU ) & 0x8000 ) != 0 )
                mask |= ModifierKey::Alt;
            if ( ( ( GetKeyState( VK_LWIN ) & 0x8000 ) != 0 ) || ( ( GetKeyState( VK_RWIN ) & 0x8000 ) != 0 ) )
                mask |= ModifierKey::Super;
            return mask;
        }

        /** @brief SPI_xxxKEYS 접근성 단축키(고정 키/토글 키/필터 키) 이전 상태 보관 (disable/restoreAccessibilityShortcuts용). */
        struct AccessibilityInternal
        {
            static inline STICKYKEYS s_prevStickyKeys{ sizeof( STICKYKEYS ), 0 };
            static inline TOGGLEKEYS s_prevToggleKeys{ sizeof( TOGGLEKEYS ), 0 };
            static inline FILTERKEYS s_prevFilterKeys{ sizeof( FILTERKEYS ), 0, 0, 0, 0, 0 };
            static inline bool       s_bDisabled{ false };
        };
    } // namespace
} // namespace sw

namespace sw
{
    void InputManager::onNativeWindowEvent( const NativeWindowEvent& event )
    {
        processNativeEvent( event );
    }

    void InputManager::processNativeEvent( const NativeWindowEvent& event )
    {
        switch ( event._message )
        {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            {
                const Key   key     = InputKeyMap::mapWin32VirtualKey( event._wParam, event._lParam );
                const uint8 modMask = getWin32ModifierMaskInternal();
                const bool  bRepeat = ( event._lParam & 0x40000000 ) != 0;
                if ( _pKeyboard != nullptr )
                    _pKeyboard->setKeyDown( key, true );
                postRawEvent( RawInputEvent::makeKeyDown( key, static_cast<uint16>( event._wParam ), bRepeat, modMask ) );
                break;
            }
            case WM_KEYUP:
            case WM_SYSKEYUP:
            {
                const Key   key     = InputKeyMap::mapWin32VirtualKey( event._wParam, event._lParam );
                const uint8 modMask = getWin32ModifierMaskInternal();
                if ( _pKeyboard != nullptr )
                    _pKeyboard->setKeyDown( key, false );
                postRawEvent( RawInputEvent::makeKeyUp( key, static_cast<uint16>( event._wParam ), modMask ) );
                break;
            }
            case WM_INPUT:
            {
                HRAWINPUT hRawInput = reinterpret_cast<HRAWINPUT>( event._lParam );
                RAWINPUT  rawInput{};
                UINT      size = sizeof( RAWINPUT );
                if ( GetRawInputData( hRawInput, RID_INPUT, &rawInput, &size, sizeof( RAWINPUTHEADER ) ) != static_cast<UINT>( -1 ) )
                {
                    if ( rawInput.header.dwType == RIM_TYPEMOUSE )
                    {
                        const float32 rawDx = static_cast<float32>( rawInput.data.mouse.lLastX );
                        const float32 rawDy = static_cast<float32>( rawInput.data.mouse.lLastY );
                        if ( rawDx != 0.0f || rawDy != 0.0f )
                        {
                            if ( _pMouse != nullptr )
                                _pMouse->addRawDelta( rawDx, rawDy );
                        }
                    }
                }
                break;
            }
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_XBUTTONDOWN:
            {
                const MouseButton btn = InputKeyMap::mapWin32MouseButton( event._message, event._wParam );
                if ( btn < MouseButton::Count )
                {
                    int32 mx = 0;
                    int32 my = 0;
                    if ( _pMouse != nullptr )
                        _pMouse->getPosition( mx, my );
                    if ( event._lParam != 0 )
                    {
                        mx = static_cast<int32>( static_cast<int16>( LOWORD( event._lParam ) ) );
                        my = static_cast<int32>( static_cast<int16>( HIWORD( event._lParam ) ) );
                        if ( _pMouse != nullptr )
                            _pMouse->setPosition( mx, my );
                    }
                    const uint8 modMask = getWin32ModifierMaskInternal();
                    if ( _pMouse != nullptr )
                        _pMouse->setButtonDown( btn, true );

                    IWindow* pWindow = IWindow::getActiveWindow();
                    if ( pWindow != nullptr )
                    {
                        HWND pHwnd = static_cast<HWND>( pWindow->getNativeHandle() );
                        if ( pHwnd != nullptr )
                            SetCapture( pHwnd );
                    }

                    postRawEvent( RawInputEvent::makeMouseButtonDown( btn, mx, my, modMask ) );
                }
                break;
            }
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDBLCLK:
            {
                const MouseButton btn = InputKeyMap::mapWin32MouseButton( event._message, event._wParam );
                if ( btn < MouseButton::Count )
                {
                    int32 mx = 0;
                    int32 my = 0;
                    if ( _pMouse != nullptr )
                        _pMouse->getPosition( mx, my );
                    if ( event._lParam != 0 )
                    {
                        mx = static_cast<int32>( static_cast<int16>( LOWORD( event._lParam ) ) );
                        my = static_cast<int32>( static_cast<int16>( HIWORD( event._lParam ) ) );
                        if ( _pMouse != nullptr )
                            _pMouse->setPosition( mx, my );
                    }
                    const uint8 modMask = getWin32ModifierMaskInternal();
                    if ( _pMouse != nullptr )
                        _pMouse->setButtonDown( btn, true );
                    postRawEvent( RawInputEvent::makeMouseDoubleClick( btn, mx, my, modMask ) );
                }
                break;
            }
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            case WM_XBUTTONUP:
            {
                const MouseButton btn = InputKeyMap::mapWin32MouseButton( event._message, event._wParam );
                if ( btn < MouseButton::Count )
                {
                    int32 mx = 0;
                    int32 my = 0;
                    if ( _pMouse != nullptr )
                        _pMouse->getPosition( mx, my );
                    if ( event._lParam != 0 )
                    {
                        mx = static_cast<int32>( static_cast<int16>( LOWORD( event._lParam ) ) );
                        my = static_cast<int32>( static_cast<int16>( HIWORD( event._lParam ) ) );
                        if ( _pMouse != nullptr )
                            _pMouse->setPosition( mx, my );
                    }
                    const uint8 modMask = getWin32ModifierMaskInternal();
                    if ( _pMouse != nullptr )
                        _pMouse->setButtonDown( btn, false );

                    if ( ( GetKeyState( VK_LBUTTON ) & 0x8000 ) == 0 && ( GetKeyState( VK_RBUTTON ) & 0x8000 ) == 0 && ( GetKeyState( VK_MBUTTON ) & 0x8000 ) == 0 )
                    {
                        ReleaseCapture();
                    }

                    postRawEvent( RawInputEvent::makeMouseButtonUp( btn, mx, my, modMask ) );
                }
                break;
            }
            case WM_MOUSEMOVE:
            {
                const int32 mx = static_cast<int32>( static_cast<int16>( LOWORD( event._lParam ) ) );
                const int32 my = static_cast<int32>( static_cast<int16>( HIWORD( event._lParam ) ) );
                if ( _pMouse != nullptr )
                    _pMouse->setPosition( mx, my );
                postRawEvent( RawInputEvent::makeMouseMove( mx, my ) );
                break;
            }
            case WM_MOUSEWHEEL:
            {
                const float32 delta = static_cast<float32>( GET_WHEEL_DELTA_WPARAM( event._wParam ) ) / 120.0f;
                if ( _pMouse != nullptr )
                    _pMouse->addWheelDelta( delta );
                postRawEvent( RawInputEvent::makeMouseWheel( delta ) );
                break;
            }
            case WM_MOUSEHWHEEL:
            {
                const float32 delta = static_cast<float32>( GET_WHEEL_DELTA_WPARAM( event._wParam ) ) / 120.0f;
                if ( _pMouse != nullptr )
                    _pMouse->addHorizontalWheelDelta( delta );
                postRawEvent( RawInputEvent::makeMouseHorizontalWheel( delta ) );
                break;
            }
            case WM_CHAR:
            {
                if ( event._wParam > 0 && event._wParam < 0x10000 )
                {
                    const utf16 wch = static_cast<utf16>( event._wParam );
                    if ( wch >= 32 || wch == static_cast<utf16>( '\t' ) || wch == static_cast<utf16>( '\n' ) || wch == static_cast<utf16>( '\r' ) )
                    {
                        utf8   arrUtf8[4] = {};
                        size_t utf8Len    = 0;
                        if ( wch < 0x80 )
                        {
                            arrUtf8[0] = static_cast<utf8>( wch );
                            utf8Len    = 1;
                        }
                        else if ( wch < 0x800 )
                        {
                            arrUtf8[0] = static_cast<utf8>( 0xC0 | ( ( wch >> 6 ) & 0x1F ) );
                            arrUtf8[1] = static_cast<utf8>( 0x80 | ( wch & 0x3F ) );
                            utf8Len    = 2;
                        }
                        else
                        {
                            arrUtf8[0] = static_cast<utf8>( 0xE0 | ( ( wch >> 12 ) & 0x0F ) );
                            arrUtf8[1] = static_cast<utf8>( 0x80 | ( ( wch >> 6 ) & 0x3F ) );
                            arrUtf8[2] = static_cast<utf8>( 0x80 | ( wch & 0x3F ) );
                            utf8Len    = 3;
                        }

                        const string_view svText( reinterpret_cast<const utf8*>( arrUtf8 ), utf8Len );
                        postRawEvent( RawInputEvent::makeTextInput( svText ) );
                    }
                }
                break;
            }
            case WM_IME_COMPOSITION:
            {
                IWindow* pWindow = IWindow::getActiveWindow();
                if ( pWindow != nullptr )
                {
                    HWND pHwnd = static_cast<HWND>( pWindow->getNativeHandle() );
                    if ( pHwnd != nullptr && ( event._lParam & GCS_COMPSTR ) != 0 )
                    {
                        HIMC hImc = ImmGetContext( pHwnd );
                        if ( hImc != nullptr )
                        {
                            const LONG size = ImmGetCompositionStringW( hImc, GCS_COMPSTR, nullptr, 0 );
                            if ( size > 0 && size < static_cast<LONG>( constant::kMaxBuffer512 ) )
                            {
                                fixed_wstring<constant::kMaxBuffer256> wstrBuf;
                                ImmGetCompositionStringW( hImc, GCS_COMPSTR, wstrBuf.data(), static_cast<DWORD>( size ) );
                                wstrBuf.data()[static_cast<size_t>( size ) / sizeof( utf16 )] = L'\0';
                                fixed_string<constant::kMaxBuffer512> utf8Buf;
                                const int32                           utf8Len = WideCharToMultiByte( CP_UTF8, 0, wstrBuf.data(), -1, utf8Buf.data(), static_cast<int32>( utf8Buf.capacity() ), nullptr, nullptr );
                                if ( utf8Len > 0 )
                                {
                                    const string_view svComp( utf8Buf.data(), static_cast<size_t>( utf8Len - 1 ) );
                                    postRawEvent( RawInputEvent::makeTextComposition( svComp ) );
                                }
                            }
                            ImmReleaseContext( pHwnd, hImc );
                        }
                    }
                }
                break;
            }
            case WM_SETFOCUS:
                onWindowFocusGained();
                break;
            case WM_KILLFOCUS:
                onWindowFocusLost();
                break;
            case WM_SIZE:
            case WM_MOVE:
                applyMouseLockMode();
                break;
            case WM_ACTIVATE:
                if ( LOWORD( event._wParam ) == WA_INACTIVE )
                    onWindowFocusLost();
                else
                    onWindowFocusGained();
                break;
            default:
                break;
        }
    }

    void InputManager::pollPlatform()
    {
        if ( _pKeyboard != nullptr )
        {
            uint32                        count{ 0 };
            const InputKeyMap::VkKeyPair* pTable = InputKeyMap::getWin32PollKeyTable( count );
            for ( uint32 eventIndex = 0; eventIndex < count; ++eventIndex )
            {
                _pKeyboard->setKeyDown( pTable[eventIndex]._key, ( GetAsyncKeyState( pTable[eventIndex]._vk ) & 0x8000 ) != 0 );
            }
        }

        if ( _pMouse != nullptr )
        {
            _pMouse->setButtonDown( MouseButton::Left, ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0 );
            _pMouse->setButtonDown( MouseButton::Right, ( GetAsyncKeyState( VK_RBUTTON ) & 0x8000 ) != 0 );
            _pMouse->setButtonDown( MouseButton::Middle, ( GetAsyncKeyState( VK_MBUTTON ) & 0x8000 ) != 0 );
            _pMouse->setButtonDown( MouseButton::X1, ( GetAsyncKeyState( VK_XBUTTON1 ) & 0x8000 ) != 0 );
            _pMouse->setButtonDown( MouseButton::X2, ( GetAsyncKeyState( VK_XBUTTON2 ) & 0x8000 ) != 0 );

            POINT pt{};
            if ( GetCursorPos( &pt ) )
            {
                IWindow* pWindow = IWindow::getActiveWindow();
                if ( pWindow != nullptr )
                {
                    HWND pHwnd = static_cast<HWND>( pWindow->getNativeHandle() );
                    if ( pHwnd != nullptr )
                        ScreenToClient( pHwnd, &pt );
                }
                _pMouse->setPosition( static_cast<int32>( pt.x ), static_cast<int32>( pt.y ) );
            }
        }
    }

    void InputManager::registerPlatformGamepads()
    {
        // 최대 4개 컨트롤러 (XInput 슬롯 규격)
        for ( uint32 padIdx = 0; padIdx < 4; ++padIdx )
        {
            auto pGamepad = make_unique<GamepadXInput>( padIdx );
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
        ShowCursor( bVisible ? TRUE : FALSE );
    }

    void InputManager::applyMouseLockMode()
    {
        if ( _pMouse == nullptr )
            return;

        const MouseLockMode lockMode = _pMouse->getLockMode();
        if ( lockMode == MouseLockMode::None )
        {
            ClipCursor( nullptr );
            return;
        }

        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr )
            return;

        HWND pHwnd = static_cast<HWND>( pWindow->getNativeHandle() );
        if ( pHwnd == nullptr )
            return;

        RECT clientRect{};
        GetClientRect( pHwnd, &clientRect );
        POINT ptTopLeft{ clientRect.left, clientRect.top };
        POINT ptBottomRight{ clientRect.right, clientRect.bottom };
        ClientToScreen( pHwnd, &ptTopLeft );
        ClientToScreen( pHwnd, &ptBottomRight );

        RECT clipRect{ ptTopLeft.x, ptTopLeft.y, ptBottomRight.x, ptBottomRight.y };

        if ( _pMouse->hasClipSubRect() )
        {
            int32 subL{ 0 }, subT{ 0 }, subR{ 0 }, subB{ 0 };
            _pMouse->getClipSubRect( subL, subT, subR, subB );
            clipRect.left   = ptTopLeft.x + subL;
            clipRect.top    = ptTopLeft.y + subT;
            clipRect.right  = ptTopLeft.x + subR;
            clipRect.bottom = ptTopLeft.y + subB;
        }

        ClipCursor( &clipRect );

        if ( lockMode == MouseLockMode::LockedInCenter )
        {
            const int32 centerX = ( clipRect.left + clipRect.right ) / 2;
            const int32 centerY = ( clipRect.top + clipRect.bottom ) / 2;
            SetCursorPos( centerX, centerY );
        }
    }

    void InputManager::releaseMouseLockMode()
    {
        ClipCursor( nullptr );
    }

    void InputManager::disableWindowsAccessibilityShortcuts()
    {
        if ( AccessibilityInternal::s_bDisabled == false )
        {
            SystemParametersInfo( SPI_GETSTICKYKEYS, sizeof( STICKYKEYS ), &AccessibilityInternal::s_prevStickyKeys, 0 );
            SystemParametersInfo( SPI_GETTOGGLEKEYS, sizeof( TOGGLEKEYS ), &AccessibilityInternal::s_prevToggleKeys, 0 );
            SystemParametersInfo( SPI_GETFILTERKEYS, sizeof( FILTERKEYS ), &AccessibilityInternal::s_prevFilterKeys, 0 );

            STICKYKEYS sk = AccessibilityInternal::s_prevStickyKeys;
            sk.dwFlags &= static_cast<DWORD>( ~( SKF_STICKYKEYSON | SKF_HOTKEYACTIVE ) );
            SystemParametersInfo( SPI_SETSTICKYKEYS, sizeof( STICKYKEYS ), &sk, 0 );

            TOGGLEKEYS tk = AccessibilityInternal::s_prevToggleKeys;
            tk.dwFlags &= static_cast<DWORD>( ~( TKF_TOGGLEKEYSON | TKF_HOTKEYACTIVE ) );
            SystemParametersInfo( SPI_SETTOGGLEKEYS, sizeof( TOGGLEKEYS ), &tk, 0 );

            FILTERKEYS fk = AccessibilityInternal::s_prevFilterKeys;
            fk.dwFlags &= static_cast<DWORD>( ~( FKF_FILTERKEYSON | FKF_HOTKEYACTIVE ) );
            SystemParametersInfo( SPI_SETFILTERKEYS, sizeof( FILTERKEYS ), &fk, 0 );

            AccessibilityInternal::s_bDisabled = true;
        }
    }

    void InputManager::restoreWindowsAccessibilityShortcuts()
    {
        if ( AccessibilityInternal::s_bDisabled == true )
        {
            SystemParametersInfo( SPI_SETSTICKYKEYS, sizeof( STICKYKEYS ), &AccessibilityInternal::s_prevStickyKeys, 0 );
            SystemParametersInfo( SPI_SETTOGGLEKEYS, sizeof( TOGGLEKEYS ), &AccessibilityInternal::s_prevToggleKeys, 0 );
            SystemParametersInfo( SPI_SETFILTERKEYS, sizeof( FILTERKEYS ), &AccessibilityInternal::s_prevFilterKeys, 0 );
            AccessibilityInternal::s_bDisabled = false;
        }
    }
} // namespace sw

#endif
