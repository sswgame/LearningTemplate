#include "pch.h"

#include "Engine/Input/InputManager.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/Defines.h"
    #include "Core/String/StringUtil.h"
    #include "Core/String/fixed_string.h"

    #include "Engine/Input/Devices/GamepadDevice.h"
    #include "Engine/Input/Devices/KeyboardDevice.h"
    #include "Engine/Input/Devices/MouseDevice.h"
    #include "Engine/Input/RawInputEvent.h"
    #include "Engine/Input/InputKeyMap.h"
    #include "Engine/Input/Windows/XInputGamepadDevice.h"
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

        /**
         * @brief 마우스 메시지의 lParam 에서 클라이언트 좌표를 꺼냅니다. 좌표는 부호 있는 16 비트입니다(창 밖 캡처 중에는 음수).
         * @details 장치에 적지 않습니다. 장치 상태는 `beginFrame` 이 큐를 재생할 때만 바뀝니다. lParam 0 을 "좌표 없음" 으로 보지
         *          않습니다 — (0, 0) 은 창 왼쪽 위 픽셀이라는 정당한 좌표입니다.
         */
        void readMouseEventPositionInternal( LPARAM lParam, int32& outX, int32& outY )
        {
            outX = static_cast<int32>( static_cast<int16>( LOWORD( lParam ) ) );
            outY = static_cast<int32>( static_cast<int16>( HIWORD( lParam ) ) );
        }

        /**
         * @brief 마우스 잠금 영역을 화면 좌표로 구합니다(클라이언트 영역, 서브 사각형이 있으면 그 안).
         * @details `applyMouseLockMode` 의 ClipCursor 와 가운데 고정의 되돌림이 같은 사각형을 봐야 합니다. 둘이 따로 계산하면 서브 사각형을
         *          쓸 때 되돌림이 클립 밖 좌표를 가리킵니다.
         * @param outClientTopLeft 잠금 영역의 왼쪽 위(클라이언트 좌표).
         */
        bool computeLockRectInternal( const MouseDevice& mouse, HWND pHwnd, RECT& outScreenRect, POINT& outClientTopLeft )
        {
            RECT clientRect{};
            if ( GetClientRect( pHwnd, &clientRect ) == FALSE )
                return false;
            POINT ptTopLeft{ clientRect.left, clientRect.top };
            POINT ptBottomRight{ clientRect.right, clientRect.bottom };
            ClientToScreen( pHwnd, &ptTopLeft );
            ClientToScreen( pHwnd, &ptBottomRight );

            outScreenRect    = RECT{ ptTopLeft.x, ptTopLeft.y, ptBottomRight.x, ptBottomRight.y };
            outClientTopLeft = POINT{ 0, 0 };
            if ( mouse.hasClipSubRect() )
            {
                int32 subL{ 0 }, subT{ 0 }, subR{ 0 }, subB{ 0 };
                mouse.getClipSubRect( subL, subT, subR, subB );
                outScreenRect    = RECT{ ptTopLeft.x + subL, ptTopLeft.y + subT, ptTopLeft.x + subR, ptTopLeft.y + subB };
                outClientTopLeft = POINT{ subL, subT };
            }
            return true;
        }

        /** @brief 포인터 이탈 알림(WM_MOUSELEAVE)을 요청해 둔 상태인지 여부입니다. TrackMouseEvent 는 이탈을 한 번 알리고 풀립니다. */
        struct PointerTrackingInternal
        {
            static inline bool s_bTrackingLeave{ false };
        };

        /** @brief SPI_xxxKEYS 접근성 단축키(고정 키 · 토글 키 · 필터 키)의 이전 상태를 보관합니다(disable/restoreAccessibilityShortcuts 용). */
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
                const Key   key          = InputKeyMap::mapWin32VirtualKey( event._wParam, event._lParam );
                const uint8 modifierMask = getWin32ModifierMaskInternal();
                const bool  bRepeat      = ( event._lParam & 0x40000000 ) != 0;
                postRawEvent( RawInputEvent::makeKeyDown( key, static_cast<uint16>( event._wParam ), bRepeat, modifierMask ) );
                break;
            }
            case WM_KEYUP:
            case WM_SYSKEYUP:
            {
                const Key   key          = InputKeyMap::mapWin32VirtualKey( event._wParam, event._lParam );
                const uint8 modifierMask = getWin32ModifierMaskInternal();
                postRawEvent( RawInputEvent::makeKeyUp( key, static_cast<uint16>( event._wParam ), modifierMask ) );
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
                            postRawEvent( RawInputEvent::makeMouseRawDelta( rawDx, rawDy ) );
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
                    int32 mouseX = 0;
                    int32 mouseY = 0;
                    readMouseEventPositionInternal( event._lParam, mouseX, mouseY );
                    const uint8 modifierMask = getWin32ModifierMaskInternal();

                    IWindow* pWindow = IWindow::getActiveWindow();
                    if ( pWindow != nullptr )
                    {
                        HWND pHwnd = static_cast<HWND>( pWindow->getNativeHandle() );
                        if ( pHwnd != nullptr )
                            SetCapture( pHwnd );
                    }

                    postRawEvent( RawInputEvent::makeMouseButtonDown( btn, mouseX, mouseY, modifierMask ) );
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
                    int32 mouseX = 0;
                    int32 mouseY = 0;
                    readMouseEventPositionInternal( event._lParam, mouseX, mouseY );
                    const uint8 modifierMask = getWin32ModifierMaskInternal();
                    postRawEvent( RawInputEvent::makeMouseDoubleClick( btn, mouseX, mouseY, modifierMask ) );
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
                    int32 mouseX = 0;
                    int32 mouseY = 0;
                    readMouseEventPositionInternal( event._lParam, mouseX, mouseY );
                    const uint8 modifierMask = getWin32ModifierMaskInternal();

                    if ( ( GetKeyState( VK_LBUTTON ) & 0x8000 ) == 0 && ( GetKeyState( VK_RBUTTON ) & 0x8000 ) == 0 && ( GetKeyState( VK_MBUTTON ) & 0x8000 ) == 0 )
                        ReleaseCapture();

                    postRawEvent( RawInputEvent::makeMouseButtonUp( btn, mouseX, mouseY, modifierMask ) );
                }
                break;
            }
            case WM_MOUSEMOVE:
            {
                int32 mouseX = 0;
                int32 mouseY = 0;
                readMouseEventPositionInternal( event._lParam, mouseX, mouseY );

                // 포인터가 창 안으로 들어온 첫 이동이다. 이탈 알림을 요청해 둬야 WM_MOUSELEAVE 가 온다 — 요청하지 않으면
                // `isPointerInside()` · `isPointerOverRect()` 가 늘 false 다.
                if ( PointerTrackingInternal::s_bTrackingLeave == false && event._pNativeWindow != nullptr )
                {
                    TRACKMOUSEEVENT trackEvent{};
                    trackEvent.cbSize    = sizeof( TRACKMOUSEEVENT );
                    trackEvent.dwFlags   = TME_LEAVE;
                    trackEvent.hwndTrack = static_cast<HWND>( event._pNativeWindow );
                    if ( TrackMouseEvent( &trackEvent ) != FALSE )
                        PointerTrackingInternal::s_bTrackingLeave = true;
                    postWindowStateEvent( RawInputEvent::makePointerCrossing( true ) );
                }
                postRawEvent( RawInputEvent::makeMouseMove( mouseX, mouseY ) );
                break;
            }
            case WM_MOUSELEAVE:
            {
                PointerTrackingInternal::s_bTrackingLeave = false;
                postWindowStateEvent( RawInputEvent::makePointerCrossing( false ) );
                break;
            }
            case WM_MOUSEWHEEL:
            {
                const float32 delta = static_cast<float32>( GET_WHEEL_DELTA_WPARAM( event._wParam ) ) / 120.0f;
                postRawEvent( RawInputEvent::makeMouseWheel( delta ) );
                break;
            }
            case WM_MOUSEHWHEEL:
            {
                const float32 delta = static_cast<float32>( GET_WHEEL_DELTA_WPARAM( event._wParam ) ) / 120.0f;
                postRawEvent( RawInputEvent::makeMouseHorizontalWheel( delta ) );
                break;
            }
            case WM_CHAR:
            {
                // BMP 밖 글자(이모지 · 확장 한자)는 높은 · 낮은 서로게이트 WM_CHAR 두 개로 온다. 높은 반쪽은 다음 WM_CHAR 까지 들고 있다가
                // 짝과 합쳐 한 글자(UTF-8 4 바이트)로 보낸다. 짝 없는 반쪽은 U+FFFD 한 글자다(`StringUtil::utf16ToUtf8` 과 같은 규칙).
                if ( event._wParam == 0 || event._wParam >= 0x10000 )
                    break;
                const uint32 codeUnit         = static_cast<uint32>( event._wParam );
                const uint32 highSurrogate    = _pendingHighSurrogate;
                const bool   bIsHighSurrogate = 0xD800 <= codeUnit && codeUnit <= 0xDBFF;
                const bool   bIsLowSurrogate  = 0xDC00 <= codeUnit && codeUnit <= 0xDFFF;
                _pendingHighSurrogate         = bIsHighSurrogate ? static_cast<uint16>( codeUnit ) : uint16{ 0 };

                string text;
                uint32 codepoint = codeUnit;
                if ( highSurrogate != 0 && bIsLowSurrogate )
                    codepoint = 0x10000 + ( ( highSurrogate - 0xD800 ) << 10 ) + ( codeUnit - 0xDC00 );
                else if ( highSurrogate != 0 )
                    StringUtil::appendUtf8( text, 0xFFFD ); // 짝을 잃은 앞 반쪽
                else if ( bIsLowSurrogate )
                    codepoint = 0xFFFD;

                const bool bIsText = bIsHighSurrogate == false && ( codepoint >= 32 || codepoint == '\t' || codepoint == '\n' || codepoint == '\r' );
                if ( bIsText )
                    StringUtil::appendUtf8( text, codepoint );
                if ( text.empty() == false )
                    postRawEvent( RawInputEvent::makeTextInput( text ) );
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
                            // `ImmGetCompositionStringW` 가 반환하는 것은 **바이트 수**이고 버퍼는
                            // 와이드 문자 배열이다. 그래서 상한을 버퍼 길이에서 직접 계산한다(바이트 상수와 문자 상수를
                            // 따로 두면 어느 한쪽만 고칠 때 조용히 넘친다).
                            using CompositionBuffer                  = fixed_wstring<constant::kMaxBuffer256>;
                            constexpr LONG kMaxCompositionByteLength = static_cast<LONG>( constant::kMaxBuffer256 * sizeof( utf16 ) );

                            const LONG size = ImmGetCompositionStringW( hImc, GCS_COMPSTR, nullptr, 0 );
                            if ( size > 0 && size < kMaxCompositionByteLength )
                            {
                                CompositionBuffer wstrBuf;
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
            // 포커스: OS 쪽 일(커서 가두기 · 풀기)은 지금 하고, 장치 상태 리셋은 큐 순서 안에서 한다(`beginFrame` 참고).
            // 커서 풀기를 미루면 다음 프레임이 오기 전까지 다른 창에서도 커서가 갇혀 있다.
            case WM_SETFOCUS:
            {
                applyMouseLockMode();
                postWindowStateEvent( RawInputEvent::makeFocusChange( true ) );
                break;
            }
            case WM_KILLFOCUS:
            {
                releaseMouseLockMode();
                postWindowStateEvent( RawInputEvent::makeFocusChange( false ) );
                break;
            }
            case WM_SIZE:
            case WM_MOVE:
            {
                applyMouseLockMode();
                break;
            }
            case WM_ACTIVATE:
            {
                const bool bGained = ( LOWORD( event._wParam ) != WA_INACTIVE );
                if ( bGained )
                    applyMouseLockMode();
                else
                    releaseMouseLockMode();
                postWindowStateEvent( RawInputEvent::makeFocusChange( bGained ) );
                break;
            }
            default:
            {
                break;
            }
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

            POINT cursorPoint{};
            if ( GetCursorPos( &cursorPoint ) )
            {
                IWindow* pWindow = IWindow::getActiveWindow();
                if ( pWindow != nullptr )
                {
                    HWND pHwnd = static_cast<HWND>( pWindow->getNativeHandle() );
                    if ( pHwnd != nullptr )
                        ScreenToClient( pHwnd, &cursorPoint );
                }
                _pMouse->setPosition( static_cast<int32>( cursorPoint.x ), static_cast<int32>( cursorPoint.y ) );
            }
        }
    }

    void InputManager::registerPlatformGamepads()
    {
        // 슬롯 수 · 0번 캐시 · 연결 콜백은 엔진 정책이라 기반 클래스가 맡는다. 여기서 정하는 것은 XInput 이라는 것뿐이다.
        registerGamepadSlots<XInputGamepadDevice>();
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

        RECT  clipRect{};
        POINT clientTopLeft{};
        if ( computeLockRectInternal( *_pMouse, pHwnd, clipRect, clientTopLeft ) == false )
            return;

        ClipCursor( &clipRect );

        if ( lockMode == MouseLockMode::LockedInCenter )
            recenterLockedCursorPlatform();
    }

    void InputManager::recenterLockedCursorPlatform()
    {
        if ( _pMouse == nullptr || _pMouse->getLockMode() != MouseLockMode::LockedInCenter )
            return;

        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr )
            return;
        HWND pHwnd = static_cast<HWND>( pWindow->getNativeHandle() );
        // 다른 창을 쓰는 동안 커서를 빼앗지 않는다.
        if ( pHwnd == nullptr || GetForegroundWindow() != pHwnd )
            return;

        RECT  lockRect{};
        POINT clientTopLeft{};
        if ( computeLockRectInternal( *_pMouse, pHwnd, lockRect, clientTopLeft ) == false )
            return;

        const int32 halfWidth  = static_cast<int32>( lockRect.right - lockRect.left ) / 2;
        const int32 halfHeight = static_cast<int32>( lockRect.bottom - lockRect.top ) / 2;
        const int2  clientCenter{ static_cast<int32>( clientTopLeft.x ) + halfWidth, static_cast<int32>( clientTopLeft.y ) + halfHeight };
        if ( _pMouse->getPosition() == clientCenter )
            return;

        SetCursorPos( static_cast<int32>( lockRect.left ) + halfWidth, static_cast<int32>( lockRect.top ) + halfHeight );
        // 되돌림은 사용자가 움직인 것이 아니다. 이번 프레임 델타는 그대로 두고 기준점만 옮긴다. SetCursorPos 가 만드는
        // WM_MOUSEMOVE(가운데)는 다음 프레임에 델타 0 으로 들어온다.
        _pMouse->setPositionWithoutDelta( clientCenter._x, clientCenter._y );
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

            STICKYKEYS stickyKeys = AccessibilityInternal::s_prevStickyKeys;
            stickyKeys.dwFlags &= static_cast<DWORD>( ~( SKF_STICKYKEYSON | SKF_HOTKEYACTIVE ) );
            SystemParametersInfo( SPI_SETSTICKYKEYS, sizeof( STICKYKEYS ), &stickyKeys, 0 );

            TOGGLEKEYS toggleKeys = AccessibilityInternal::s_prevToggleKeys;
            toggleKeys.dwFlags &= static_cast<DWORD>( ~( TKF_TOGGLEKEYSON | TKF_HOTKEYACTIVE ) );
            SystemParametersInfo( SPI_SETTOGGLEKEYS, sizeof( TOGGLEKEYS ), &toggleKeys, 0 );

            FILTERKEYS filterKeys = AccessibilityInternal::s_prevFilterKeys;
            filterKeys.dwFlags &= static_cast<DWORD>( ~( FKF_FILTERKEYSON | FKF_HOTKEYACTIVE ) );
            SystemParametersInfo( SPI_SETFILTERKEYS, sizeof( FILTERKEYS ), &filterKeys, 0 );

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
