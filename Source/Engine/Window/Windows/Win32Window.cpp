#include "pch.h"

#include "Engine/Window/Windows/Win32Window.h"

#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/WindowMonitor.h"

#include <imm.h>

#pragma comment( lib, "imm32.lib" )

namespace sw
{
    namespace
    {
        struct Win32WindowInternal
        {
            using PfnGetDpiForMonitor = HRESULT( WINAPI* )( HMONITOR, int32, UINT*, UINT* );

            /** @brief Shcore.dll 의 `GetDpiForMonitor` 입니다. Engine 은 Shcore 를 링크하지 않는다(imgui_impl_win32 도 올려 쓴다). 프로세스 끝까지 둔다. */
            static PfnGetDpiForMonitor findGetDpiForMonitor()
            {
                void* const pModule = ModuleLibraryUtil::loadSystemLibrary( "Shcore.dll" );
                return pModule != nullptr ? reinterpret_cast<PfnGetDpiForMonitor>( ModuleLibraryUtil::getDynamicSymbol( pModule, "GetDpiForMonitor" ) ) : nullptr;
            }

            static WindowMonitorRect toRect( const RECT& rect )
            {
                return WindowMonitorRect{
                    int2{             rect.left,               rect.top},
                    int2{rect.right - rect.left, rect.bottom - rect.top}
                };
            }

            static BOOL CALLBACK onEnumMonitor( HMONITOR hMonitor, HDC /*hdc*/, LPRECT /*pClip*/, LPARAM userData )
            {
                static const PfnGetDpiForMonitor s_pfnGetDpiForMonitor = findGetDpiForMonitor();

                MONITORINFOEXW info{};
                info.cbSize = sizeof( info );
                if ( GetMonitorInfoW( hMonitor, &info ) == FALSE )
                    return TRUE; // 그 사이 빠진 모니터다 - 나머지를 읽는다

                WindowMonitor monitor;
                monitor._name                    = StringUtil::utf16ToUTF8( info.szDevice );
                monitor._area                    = toRect( info.rcMonitor );
                monitor._workArea                = toRect( info.rcWork );
                monitor._bPrimary                = ( info.dwFlags & MONITORINFOF_PRIMARY ) != 0;
                UINT            dpiX             = 0;
                UINT            dpiY             = 0;
                constexpr int32 kMdtEffectiveDpi = 0; // MDT_EFFECTIVE_DPI(shellscalingapi.h 를 끌어오지 않는다)
                if ( s_pfnGetDpiForMonitor != nullptr && SUCCEEDED( s_pfnGetDpiForMonitor( hMonitor, kMdtEffectiveDpi, &dpiX, &dpiY ) ) && dpiX > 0 )
                    monitor._contentScale = static_cast<float32>( dpiX ) / IWindow::kReferenceDPI;

                DEVMODEW mode{};
                mode.dmSize = sizeof( mode );
                for ( DWORD modeIndex = 0; EnumDisplaySettingsW( info.szDevice, modeIndex, &mode ) != FALSE; ++modeIndex )
                {
                    if ( mode.dmBitsPerPel >= 32 )
                        monitor._listResolution.push_back( int2{ static_cast<int32>( mode.dmPelsWidth ), static_cast<int32>( mode.dmPelsHeight ) } );
                }
                monitor.normalizeResolutions();
                reinterpret_cast<vector<WindowMonitor>*>( userData )->push_back( std::move( monitor ) );
                return TRUE;
            }

            /**
             * @brief 클라이언트 크기에 테두리 · 제목 줄을 더한 창 사각형입니다. @p dpi 의 테두리 두께로 잰다 — `AdjustWindowRect` 는 시스템 DPI 로 재서
             *        배율이 다른 모니터에서 클라이언트 크기가 어긋났다(감사 P-12, 언리얼 · SDL 의 `AdjustWindowRectExForDpi`).
             */
            static RECT computeFrameRect( uint32 clientWidth, uint32 clientHeight, UINT dpi )
            {
                RECT frameRect = { 0, 0, static_cast<LONG>( clientWidth ), static_cast<LONG>( clientHeight ) };
                AdjustWindowRectExForDpi( &frameRect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi );
                return frameRect;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    Win32Window::Win32Window()
        : _hWnd{ nullptr }
        , _bResizing{ SW_FALSE }
        , _reservedWin32{ 0 }
        , _padding{ 0 }
    {
        // 복원 위치는 기반(`IWindow`)이 가지고 절차도 기반이 맡는다. 플랫폼은 "알아서" 값만 정한다.
        clearRestorePosition();
    }

    Win32Window::~Win32Window()
    {
        Win32Window::destroy();
    }

    /**
     * @brief Win32 창 클래스를 등록하고 오버랩 창(WS_OVERLAPPEDWINDOW)을 만듭니다. 띄우는 것은 showWindow() 입니다.
     */
    bool Win32Window::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        _width  = width;
        _height = height;
        if ( storeTitle( pTitle ) == false )
            return false; // 오류는 storeTitle 이 남겼다
        const wstring titleW = StringUtil::utf8ToUTF16( _arrTitle );

        HINSTANCE hInstance = GetModuleHandleW( nullptr );

        // CS_OWNDC: DXGI↔OpenGL 핫스왑 때 WGL GetDC/SwapBuffers 가 안정적으로 돌려면 꼭 필요하다
        WNDCLASSEXW wc{};
        wc.cbSize      = sizeof( WNDCLASSEXW );
        wc.style       = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = wndProc;
        wc.hInstance   = hInstance;
        // 실행 파일(App.exe)의 아이콘 리소스다. hInstance 는 프로세스 exe 라 Engine.dll 안에서도 App.exe 리소스를 찾는다. 없으면 nullptr(기본 아이콘).
        wc.hIcon   = LoadIconW( hInstance, MAKEINTRESOURCEW( constant::window::kAppIconAssetID ) );
        wc.hIconSm = wc.hIcon;
        // IDC_* 는 TCHAR 매크로(A 판 포인터 모양의 정수 자원 id)라 W 판에 맞게 넘긴다.
        wc.hCursor       = LoadCursorW( nullptr, reinterpret_cast<LPCWSTR>( IDC_ARROW ) );
        wc.lpszClassName = L"SWEngineWindowClass_OWNDC";

        RegisterClassExW( &wc );

        const RECT rc = Win32WindowInternal::computeFrameRect( width, height, GetDpiForSystem() ); // 창이 아직 없어 놓일 모니터를 모른다 — 시스템 DPI 로 잰다

        _hWnd = CreateWindowExW(
            0,
            L"SWEngineWindowClass_OWNDC",
            titleW.c_str(),
            WS_OVERLAPPEDWINDOW,
            _restoreX,
            _restoreY,
            rc.right - rc.left,
            rc.bottom - rc.top,
            nullptr,
            nullptr,
            hInstance,
            this );

        if ( _hWnd == nullptr )
            return false;
        // 원하는 쪽이 없으면 입력기는 꺼 둔다(X11 · Wayland 와 같다) - 한글 입력기가 켜진 채 게임을 해도 WASD 가 조합으로 가지 않는다.
        applyTextInputWanted( isTextInputWanted() );

        SW_LOG_INFO_ID( "Win32Window.NativeWin32WindowCreated", "Native Win32 Window created successfully! (%#×%#)", width, height );
        return true;
    }

    /**
     * @brief Win32 창 핸들을 파괴하고 리소스를 정리합니다.
     */
    void Win32Window::destroy()
    {
        if ( _hWnd != nullptr )
        {
            DestroyWindow( _hWnd );
            _hWnd = nullptr;
        }
    }

    void Win32Window::applyTitle()
    {
        if ( _hWnd != nullptr )
            SetWindowTextW( _hWnd, StringUtil::utf8ToUTF16( _arrTitle ).c_str() );
    }

    void Win32Window::applyWindowVisibility( bool bShow )
    {
        if ( _hWnd == nullptr )
            return;

        if ( bShow )
        {
            ShowWindow( _hWnd, SW_SHOWNORMAL );
            UpdateWindow( _hWnd );
            SetForegroundWindow( _hWnd );
            SetFocus( _hWnd );
        }
        else
        {
            ShowWindow( _hWnd, SW_HIDE );
        }
    }

    bool Win32Window::isVisible() const
    {
        return ( _hWnd != nullptr ) ? ( IsWindowVisible( _hWnd ) != FALSE ) : false;
    }

    float32 Win32Window::getContentScale() const
    {
        if ( _hWnd == nullptr )
            return 1.0f;
        const UINT dpi = GetDpiForWindow( _hWnd );
        return dpi > 0 ? static_cast<float32>( dpi ) / kReferenceDPI : 1.0f;
    }

    bool Win32Window::tryGetMonitors( vector<WindowMonitor>& outListMonitor ) const
    {
        outListMonitor.clear();
        if ( _hWnd == nullptr )
            return false;
        if ( EnumDisplayMonitors( nullptr, nullptr, &Win32WindowInternal::onEnumMonitor, reinterpret_cast<LPARAM>( &outListMonitor ) ) == FALSE )
            SW_LOG_WARNING_ID( "Win32Window.EnumDisplayMonitorsFailed", "EnumDisplayMonitors failed (error %#) - the monitor list is incomplete", GetLastError() );
        WindowMonitor::orderPrimaryFirst( outListMonitor );
        return true;
    }

    void Win32Window::applyTextInputWanted( bool bWanted )
    {
        if ( _hWnd != nullptr )
            (void)ImmAssociateContextEx( _hWnd, nullptr, bWanted ? IACE_DEFAULT : 0 );
    }

    void Win32Window::applyTextInputArea( const WindowTextInputArea& area )
    {
        if ( _hWnd == nullptr )
            return;
        HIMC hImc = ImmGetContext( _hWnd );
        if ( hImc == nullptr )
            return; // 입력기가 꺼진 동안이다 - 켜진 뒤 다음 자리 변화가 놓는다
        const POINT     position{ area._position._x, area._position._y };
        COMPOSITIONFORM composition{};
        composition.dwStyle      = CFS_FORCE_POSITION;
        composition.ptCurrentPos = position;
        (void)ImmSetCompositionWindow( hImc, &composition );
        CANDIDATEFORM candidate{};
        candidate.dwStyle        = CFS_CANDIDATEPOS;
        candidate.ptCurrentPos.x = area._position._x;
        candidate.ptCurrentPos.y = area._position._y + area._lineHeight;
        (void)ImmSetCandidateWindow( hImc, &candidate );
        ImmReleaseContext( _hWnd, hImc );
    }

    bool Win32Window::setMinimized( bool bMinimized )
    {
        if ( _hWnd == nullptr )
            return false;
        // 상태는 이 호출 안에서 오는 WM_SIZE(SIZE_MINIMIZED · SIZE_RESTORED)가 바꾼다(`trackActivityMessage`).
        ShowWindow( _hWnd, bMinimized ? SW_MINIMIZE : SW_RESTORE );
        return true;
    }

    bool Win32Window::sendFocusEvent( bool bFocused )
    {
        if ( _hWnd == nullptr )
            return false;
        SendMessageW( _hWnd, bFocused ? WM_SETFOCUS : WM_KILLFOCUS, 0, 0 );
        return true;
    }

    bool Win32Window::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        if ( _hWnd == nullptr )
            return false;

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof( MONITORINFO );
        if ( GetMonitorInfoW( MonitorFromWindow( _hWnd, MONITOR_DEFAULTTONEAREST ), &monitorInfo ) == FALSE )
            return false;

        // 스타일을 바꿔도 보이던 창은 보이게, 숨은 창은 숨은 채로 둔다(기동 중에는 아직 숨어 있다).
        const LONG_PTR visibleStyle = ( IsWindowVisible( _hWnd ) != FALSE ) ? WS_VISIBLE : 0;
        const bool     bWasWindowed = _displayMode == WindowDisplayMode::Windowed;
        _displayMode                = mode;
        if ( mode == WindowDisplayMode::BorderlessFullscreen )
        {
            const RECT& monitorRect = monitorInfo.rcMonitor;
            SetWindowLongPtrW( _hWnd, GWL_STYLE, WS_POPUP | visibleStyle );
            SetWindowPos( _hWnd, HWND_TOP, monitorRect.left, monitorRect.top, monitorRect.right - monitorRect.left, monitorRect.bottom - monitorRect.top,
                          SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE );
            SW_LOG_INFO_ID( "Win32Window.DisplayModeBorderlessFullscreen", "Display mode: borderless fullscreen (%#x%#)", monitorRect.right - monitorRect.left, monitorRect.bottom - monitorRect.top );
            return true;
        }

        // 창 모드는 요청한 클라이언트 크기다. 이미 창이었으면 크기만 바꾸고 왼쪽 위를 지킨다(X11 · 언리얼의 창 크기 변경과 같다 - 예전에는 늘 가운데로 옮겨
        // 자동화가 바둑판으로 놓은 백엔드 창이 단계마다 한가운데로 겹쳤다). 전체화면에서 돌아올 때만 그 모니터의 작업 영역 가운데에 놓는다.
        const RECT  frameRect   = Win32WindowInternal::computeFrameRect( width, height, GetDpiForWindow( _hWnd ) );
        const LONG  frameWidth  = frameRect.right - frameRect.left;
        const LONG  frameHeight = frameRect.bottom - frameRect.top;
        const RECT& workRect    = monitorInfo.rcWork;
        RECT        currentRect{};
        const bool  bKeepPosition = bWasWindowed && GetWindowRect( _hWnd, &currentRect ) != FALSE;
        const LONG  frameX        = bKeepPosition ? currentRect.left : workRect.left + ( ( workRect.right - workRect.left ) - frameWidth ) / 2;
        const LONG  frameY        = bKeepPosition ? currentRect.top : workRect.top + ( ( workRect.bottom - workRect.top ) - frameHeight ) / 2;
        SetWindowLongPtrW( _hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | visibleStyle );
        SetWindowPos( _hWnd, nullptr, frameX < workRect.left ? workRect.left : frameX, frameY < workRect.top ? workRect.top : frameY, frameWidth, frameHeight,
                      SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE );
        SW_LOG_INFO_ID( "Win32Window.DisplayModeWindowedX", "Display mode: windowed (%#x%#)", width, height );
        return true;
    }

    bool Win32Window::tryGetFrameExtents( WindowFrameExtents& outExtents ) const
    {
        outExtents = WindowFrameExtents{};
        RECT       windowRect{};
        RECT       clientRect{};
        POINT      clientOrigin{ 0, 0 };
        const bool bRead = _hWnd != nullptr && GetWindowRect( _hWnd, &windowRect ) != FALSE && GetClientRect( _hWnd, &clientRect ) != FALSE &&
                           ClientToScreen( _hWnd, &clientOrigin ) != FALSE;
        if ( bRead == false )
        {
            SW_LOG_ERROR_ID( "Win32Window.FrameExtentsUnreadable", "Window frame extents could not be read (no window or GetWindowRect failed, error %#)", GetLastError() );
            return false;
        }
        outExtents._left   = clientOrigin.x - windowRect.left;
        outExtents._top    = clientOrigin.y - windowRect.top;
        outExtents._right  = windowRect.right - ( clientOrigin.x + clientRect.right );
        outExtents._bottom = windowRect.bottom - ( clientOrigin.y + clientRect.bottom );
        return true;
    }

    bool Win32Window::setFramePosition( int32 x, int32 y )
    {
        return _hWnd != nullptr && SetWindowPos( _hWnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE ) != FALSE;
    }

    /**
     * @brief 다시 만들기 직전의 창 위치를 `GetWindowRect` 로 `_restoreX` 와 `_restoreY` 에 담습니다.
     */
    void Win32Window::captureRestorePosition()
    {
        if ( _hWnd == nullptr )
            return;

        RECT windowRect{};
        if ( GetWindowRect( _hWnd, &windowRect ) )
        {
            _restoreX = windowRect.left;
            _restoreY = windowRect.top;
        }
    }

    void Win32Window::clearRestorePosition()
    {
        _restoreX = CW_USEDEFAULT;
        _restoreY = CW_USEDEFAULT;
    }

    bool Win32Window::processMessages()
    {
        MSG msg{};
        while ( PeekMessageW( &msg, nullptr, 0, 0, PM_REMOVE ) != 0 )
        {
            if ( msg.message == WM_QUIT )
                return false;

            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
        return _bShouldClose == SW_FALSE;
    }

    void Win32Window::waitForMessages( uint32 timeoutMilliseconds )
    {
        // 이미 큐에 든 메시지에도 깨도록 QS_ALLINPUT 이다(입력 · 페인트 · 타이머 · 보낸 메시지).
        (void)MsgWaitForMultipleObjects( 0, nullptr, FALSE, timeoutMilliseconds, QS_ALLINPUT );
    }

    void Win32Window::trackActivityMessage( UINT msg, WPARAM wParam )
    {
        if ( msg == WM_SIZE )
        {
            // SIZE_MAXHIDE · SIZE_MAXSHOW 는 다른 창의 소식이라 상태를 바꾸지 않는다.
            if ( wParam == SIZE_MINIMIZED )
                setMinimizedState( true );
            else if ( wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED )
                setMinimizedState( false );
        }
        else if ( msg == WM_ACTIVATEAPP )
            setFocusState( wParam != FALSE );
        else if ( msg == WM_DISPLAYCHANGE || msg == WM_DPICHANGED || ( msg == WM_SETTINGCHANGE && wParam == SPI_SETWORKAREA ) )
            notifyMonitorsChanged();
    }

    void Win32Window::onSizeMessage( WPARAM wParam, LPARAM lParam )
    {
        // **최소화한 크기를 창 크기로 기억하지 않는다.** 최소화는 클라이언트 영역
        // 0x0 짜리 WM_SIZE 로 온다. 그 값을 `_width`/`_height` 에 적어 두면 창이
        // 크기 0 이 됐다는 뜻이 되어 버린다. 그 상태에서 `recreate()`(백엔드 교체가
        // 이 경로로 온다)가 돌면 기억해 둔 0x0 으로 창을 다시 만들고, 복원해도 그 크기가
        // 그대로 남는다. 최소화가 말하는 것은 "안 보인다" 이지 "크기가 0 이다" 가 아니다.
        const uint32 clientWidth   = LOWORD( lParam );
        const uint32 clientHeight  = HIWORD( lParam );
        const bool   bIgnoreResize = wParam == SIZE_MINIMIZED || clientWidth == 0 || clientHeight == 0;
        if ( bIgnoreResize )
            return;

        _width  = clientWidth;
        _height = clientHeight;
        // DPI 변경 등으로 ShowWindow/SetForegroundWindow 를 처리하는 중에 OS 가 GetSystemMetricsForDpi
        // 등을 거쳐 SendMessageW 로 같은 스레드에 WM_SIZE 를 재진입시킬 수 있다. 재진입 가드
        // 없이 onResize(스왑체인 리사이즈)를 중첩 호출하면 아직 다시 만드는 중인 렌더 타깃을
        // 다시 정리 · 생성하게 되어 DataRaceDetector 가 레이스로 감지해 크래시한다.
        const bool bCanNotifyResize = _bRecreating == SW_FALSE && _bResizing == SW_FALSE && _onResize.isBound();
        if ( bCanNotifyResize == false )
            return;
        // 재진입한 WM_SIZE 는 위에서 크기만 적고 콜백을 건너뛴다. 그래서 콜백이 돌아온 뒤 크기가 바뀌었으면 **마지막 크기로 한 번
        // 더** 부른다. 바깥 호출이 넘긴 옛 크기로 끝나면 스왑체인이 다음 WM_SIZE 까지 옛 크기로 남는다(DPI 변경의
        // SetWindowPos 가 바로 그 재진입이다). 끝없이 돌지 않게 횟수를 제한한다.
        _bResizing           = SW_TRUE;
        uint32 resizedWidth  = 0;
        uint32 resizedHeight = 0;
        for ( int32 round = 0; round < 4 && ( _width != resizedWidth || _height != resizedHeight ); ++round )
        {
            resizedWidth  = _width;
            resizedHeight = _height;
            _onResize.get()( resizedWidth, resizedHeight );
        }
        _bResizing = SW_FALSE;
    }

    bool Win32Window::fillMinimumTrackSize( LPARAM lParam ) const
    {
        // 창 모드에서 사용자가 줄일 수 있는 하한(`setMinimumClientSize`)이다. 클라이언트 크기에 테두리와 제목 줄을 더해 창 크기로 넘긴다
        // (`setDisplayMode` 와 같은 `computeFrameRect`). SetWindowPos 도 DefWindowProcW 의 WM_WINDOWPOSCHANGING 이 이 값으로 자른다.
        const bool bNoMinimumSize = _displayMode != WindowDisplayMode::Windowed || _minClientWidth == 0 || _minClientHeight == 0;
        if ( bNoMinimumSize )
            return false;
        const RECT  frameRect   = Win32WindowInternal::computeFrameRect( _minClientWidth, _minClientHeight, GetDpiForWindow( _hWnd ) );
        MINMAXINFO* pInfo       = reinterpret_cast<MINMAXINFO*>( lParam );
        pInfo->ptMinTrackSize.x = frameRect.right - frameRect.left;
        pInfo->ptMinTrackSize.y = frameRect.bottom - frameRect.top;
        return true;
    }

    LRESULT CALLBACK Win32Window::wndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
    {
        Win32Window* pThis{ nullptr };
        if ( msg == WM_NCCREATE )
        {
            CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>( lParam );
            pThis                  = reinterpret_cast<Win32Window*>( pCreate->lpCreateParams );
            SetWindowLongPtrW( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pThis ) );
            pThis->_hWnd = hWnd;
        }
        else
            pThis = reinterpret_cast<Win32Window*>( GetWindowLongPtrW( hWnd, GWLP_USERDATA ) );

        if ( pThis != nullptr )
        {
            pThis->trackActivityMessage( msg, wParam );
            if ( pThis->_customHandler.isBound() )
            {
                NativeWindowEvent event{};
                event.setNativeWindow( NativeSurfaceKind::Win32, hWnd );
                event._message = msg;
                event._wParam  = wParam;
                event._lParam  = lParam;
                if ( pThis->_customHandler.get()( event ) )
                    return true;
            }

            switch ( msg )
            {
                case WM_GETOBJECT:
                {
                    // 접근성 클라이언트(내레이터)가 창의 UI Automation 제공자를 청한다. 답이 있으면 그 값을 돌려주고, 없으면 기본 처리(창 자체의 제공자)로 간다.
                    intptr_t result = 0;
                    if ( pThis->_accessibilityQuery.isBound() && pThis->_accessibilityQuery.get()( static_cast<uintptr_t>( wParam ), static_cast<intptr_t>( lParam ), result ) )
                        return static_cast<LRESULT>( result );
                    break;
                }
                case WM_SIZE:
                {
                    pThis->onSizeMessage( wParam, lParam );
                    return 0;
                }

                case WM_DPICHANGED:
                {
                    // 모니터별 DPI 인식(PerMonitorV2)에서는 OS 가 창 크기를 바꿔 주지 않는다. 권장 사각형으로 옮긴다. 그 뒤의 WM_SIZE 가
                    // 스왑체인을 맞춘다. 이 처리가 없으면 배율이 다른 모니터로 옮길 때 창이 실제 크기의 절반이나 두 배가 된다.
                    const RECT* pSuggested = reinterpret_cast<const RECT*>( lParam );
                    if ( pSuggested != nullptr )
                    {
                        SetWindowPos( hWnd, nullptr, pSuggested->left, pSuggested->top, pSuggested->right - pSuggested->left,
                                      pSuggested->bottom - pSuggested->top, SWP_NOZORDER | SWP_NOACTIVATE );
                    }
                    return 0;
                }

                case WM_GETMINMAXINFO:
                {
                    // 이 메시지는 WM_NCCREATE 보다 먼저 오는 첫 메시지다. 그때는 pThis 가 없어 기본 처리로 간다.
                    if ( pThis->fillMinimumTrackSize( lParam ) )
                        return 0;
                    break;
                }

                case WM_SYSCOMMAND:
                {
                    // Alt · F10 만 누르면 OS 가 창 메뉴 모드(SC_KEYMENU)로 들어가 다음 키를 삼킨다(감사 P0-15) — 게임 입력의 Alt(달리기 · 조준)가 다음 키를 잃는다.
                    // 창 메뉴를 쓰지 않으므로 막는다. Alt+Space(창 시스템 메뉴 — 키보드로 창을 옮기는 길)는 남긴다(언리얼 FWindowsApplication 과 같은 자리).
                    constexpr WPARAM kSystemCommandMask = 0xFFF0; // 아래 4 비트는 OS 가 쓴다(SC_* 비교 규칙)
                    if ( ( wParam & kSystemCommandMask ) == SC_KEYMENU && lParam != VK_SPACE )
                        return 0;
                    break;
                }

                case WM_MENUCHAR:
                {
                    // 메뉴 없는 창에서 Alt+글자를 누르면 기본 처리가 경고음을 낸다 — 메뉴를 닫는다고 답한다.
                    return MAKELRESULT( 0, MNC_CLOSE );
                }

                case WM_CLOSE:
                {
                    if ( pThis->_bRecreating == SW_FALSE )
                        (void)pThis->tryBeginClose(); // 거절되면(저장 확인) 창이 그대로다
                    return 0;
                }

                case WM_DESTROY:
                {
                    if ( pThis->_bRecreating == SW_FALSE )
                    {
                        pThis->_bShouldClose = SW_TRUE;
                        pThis->_hWnd         = nullptr;
                    }
                    return 0;
                }

                default:
                {
                    break;
                }
            }
        }

        // 창 클래스와 창을 W 버전으로 만들었으니 기본 처리도 W 버전이다. A 버전은 WM_NCCREATE 와 WM_SETTEXT 의 UTF-16 제목을 ANSI 로 읽어 첫 글자에서 끊는다.
        return DefWindowProcW( hWnd, msg, wParam, lParam );
    }
} // namespace sw
