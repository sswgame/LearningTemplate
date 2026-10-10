#include "pch.h"

#include "Engine/Window/Windows/Win32Window.h"

#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    SW_LOG_CALLER( "Win32Window" );

    Win32Window::Win32Window()
        : _hWnd{ nullptr }
        , _bResizing{ SW_FALSE }
        , _reservedWin32{ 0 }
        , _padding{ 0 }
    {
        // 복원 위치는 기반(`IWindow`)이 들고 절차도 기반이 맡는다. 플랫폼은 "알아서" 값만 정한다.
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
        _title  = StringUtil::isNullOrEmpty( pTitle ) ? L"" : StringUtil::utf8ToUtf16( pTitle );

        HINSTANCE hInstance = GetModuleHandleW( nullptr );

        // CS_OWNDC: DXGI↔OpenGL 핫스왑 때 WGL GetDC/SwapBuffers 가 안정적으로 돌려면 꼭 필요하다
        WNDCLASSEXW wc{};
        wc.cbSize      = sizeof( WNDCLASSEXW );
        wc.style       = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = wndProc;
        wc.hInstance   = hInstance;
        // 실행 파일(App.exe)의 아이콘 리소스. hInstance 는 프로세스 exe 라 Engine.dll 안에서도 App.exe 리소스를 찾는다 — 없으면 nullptr(기본 아이콘).
        wc.hIcon   = LoadIconW( hInstance, MAKEINTRESOURCEW( constant::window::kAppIconResourceID ) );
        wc.hIconSm = wc.hIcon;
        // IDC_* 는 TCHAR 매크로(A 판 포인터 모양의 정수 자원 id)라 W 판에 맞게 넘긴다.
        wc.hCursor       = LoadCursorW( nullptr, reinterpret_cast<LPCWSTR>( IDC_ARROW ) );
        wc.lpszClassName = L"SWEngineWindowClass_OWNDC";

        RegisterClassExW( &wc );

        RECT rc = { 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) };
        AdjustWindowRect( &rc, WS_OVERLAPPEDWINDOW, FALSE );

        _hWnd = CreateWindowExW(
            0,
            L"SWEngineWindowClass_OWNDC",
            _title.c_str(),
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

        SW_LOG_INFO( "Native Win32 Window created successfully! (%#×%#)", width, height );
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
            SetWindowTextW( _hWnd, _title.c_str() );
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
        return dpi > 0 ? static_cast<float32>( dpi ) / kReferenceDpi : 1.0f;
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
        _displayMode                = mode;
        if ( mode == WindowDisplayMode::BorderlessFullscreen )
        {
            const RECT& monitorRect = monitorInfo.rcMonitor;
            SetWindowLongPtrW( _hWnd, GWL_STYLE, WS_POPUP | visibleStyle );
            SetWindowPos( _hWnd, HWND_TOP, monitorRect.left, monitorRect.top, monitorRect.right - monitorRect.left, monitorRect.bottom - monitorRect.top,
                          SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE );
            SW_LOG_INFO( "Display mode: borderless fullscreen (%#x%#)", monitorRect.right - monitorRect.left, monitorRect.bottom - monitorRect.top );
            return true;
        }

        // 창 모드는 요청한 클라이언트 크기로, 그 모니터의 작업 영역 가운데에 놓는다.
        RECT frameRect = { 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) };
        AdjustWindowRect( &frameRect, WS_OVERLAPPEDWINDOW, FALSE );
        const LONG  frameWidth  = frameRect.right - frameRect.left;
        const LONG  frameHeight = frameRect.bottom - frameRect.top;
        const RECT& workRect    = monitorInfo.rcWork;
        const LONG  frameX      = workRect.left + ( ( workRect.right - workRect.left ) - frameWidth ) / 2;
        const LONG  frameY      = workRect.top + ( ( workRect.bottom - workRect.top ) - frameHeight ) / 2;
        SetWindowLongPtrW( _hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | visibleStyle );
        SetWindowPos( _hWnd, nullptr, frameX < workRect.left ? workRect.left : frameX, frameY < workRect.top ? workRect.top : frameY, frameWidth, frameHeight,
                      SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE );
        SW_LOG_INFO( "Display mode: windowed (%#x%#)", width, height );
        return true;
    }

    /**
     * @brief 다시 만들기 직전의 창 위치를 `GetWindowRect` 로 `_restoreX` · `_restoreY` 에 담습니다.
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
            if ( pThis->_customHandler.isBound() )
            {
                NativeWindowEvent event{};
                event._pNativeWindow = hWnd;
                event._message       = msg;
                event._wParam        = wParam;
                event._lParam        = lParam;
                if ( pThis->_customHandler( event ) )
                    return true;
            }

            switch ( msg )
            {
                case WM_SIZE:
                {
                    // **최소화한 크기를 창 크기로 기억하지 않는다.** 최소화는 클라이언트 영역
                    // 0x0 짜리 WM_SIZE 로 온다. 그 값을 `_width`/`_height` 에 적어 두면 창이
                    // 0 칸짜리가 됐다는 뜻이 되어 버린다. 그 상태에서 `recreate()`(백엔드 교체가
                    // 이 길로 온다)가 돌면 기억해 둔 0x0 으로 창을 다시 만들고, 복원해도 그 크기가
                    // 그대로 남는다. 최소화가 말하는 것은 "안 보인다" 지 "0 칸이다" 가 아니다.
                    const uint32 clientWidth  = LOWORD( lParam );
                    const uint32 clientHeight = HIWORD( lParam );
                    if ( wParam == SIZE_MINIMIZED || clientWidth == 0 || clientHeight == 0 )
                        return 0;

                    pThis->_width  = clientWidth;
                    pThis->_height = clientHeight;
                    // DPI 변경 등으로 ShowWindow/SetForegroundWindow 를 처리하는 중에 OS 가 GetSystemMetricsForDpi
                    // 등을 거쳐 SendMessageW 로 같은 스레드에 WM_SIZE 를 재진입시킬 수 있다. 재진입 가드
                    // 없이 onResize(스왑체인 리사이즈)를 중첩 호출하면 아직 다시 만드는 중인 렌더 타깃을
                    // 다시 정리 · 생성하게 되어 DataRaceDetector 가 레이스로 감지해 크래시한다.
                    if ( pThis->_bRecreating == SW_FALSE && pThis->_bResizing == SW_FALSE && pThis->_onResize.isBound() )
                    {
                        // 재진입한 WM_SIZE 는 위에서 크기만 적고 콜백을 건너뛴다. 그래서 콜백이 돌아온 뒤 크기가 바뀌었으면 **마지막 크기로 한 번
                        // 더** 부른다 — 바깥 호출이 넘긴 옛 크기로 끝나면 스왑체인이 다음 WM_SIZE 까지 옛 크기로 남는다(DPI 변경의
                        // SetWindowPos 가 바로 그 재진입이다). 끝없이 돌지 않게 횟수를 막는다.
                        pThis->_bResizing    = SW_TRUE;
                        uint32 resizedWidth  = 0;
                        uint32 resizedHeight = 0;
                        for ( int32 round = 0; round < 4 && ( pThis->_width != resizedWidth || pThis->_height != resizedHeight ); ++round )
                        {
                            resizedWidth  = pThis->_width;
                            resizedHeight = pThis->_height;
                            pThis->_onResize( resizedWidth, resizedHeight );
                        }
                        pThis->_bResizing = SW_FALSE;
                    }
                    return 0;
                }

                case WM_DPICHANGED:
                {
                    // 모니터별 DPI 인식(PerMonitorV2)에서는 OS 가 창 크기를 바꿔 주지 않는다. 권장 사각형으로 옮긴다 — 그 뒤의 WM_SIZE 가
                    // 스왑체인을 맞춘다. 이 처리가 없으면 배율이 다른 모니터로 옮길 때 창이 실제 크기의 절반 · 두 배가 된다.
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
                    // 창 모드에서 사용자가 줄일 수 있는 바닥(`setMinimumClientSize`)이다. 클라이언트 크기에 테두리 · 제목 줄을 더해 창 크기로 넘긴다
                    // (`setDisplayMode` 와 같은 `AdjustWindowRect`). SetWindowPos 도 DefWindowProcW 의 WM_WINDOWPOSCHANGING 이 이 값으로 자른다.
                    // 이 메시지는 WM_NCCREATE 보다 먼저 오는 첫 메시지다 — 그때는 pThis 가 없어 기본 처리로 간다.
                    if ( pThis->_displayMode != WindowDisplayMode::Windowed || pThis->_minClientWidth == 0 || pThis->_minClientHeight == 0 )
                        break;
                    RECT frameRect = { 0, 0, static_cast<LONG>( pThis->_minClientWidth ), static_cast<LONG>( pThis->_minClientHeight ) };
                    AdjustWindowRect( &frameRect, WS_OVERLAPPEDWINDOW, FALSE );
                    MINMAXINFO* pInfo       = reinterpret_cast<MINMAXINFO*>( lParam );
                    pInfo->ptMinTrackSize.x = frameRect.right - frameRect.left;
                    pInfo->ptMinTrackSize.y = frameRect.bottom - frameRect.top;
                    return 0;
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

        // 창 클래스 · 창을 W 판으로 만들었으니 기본 처리도 W 판이다. A 판은 WM_NCCREATE · WM_SETTEXT 의 UTF-16 제목을 ANSI 로 읽어 첫 글자에서 끊는다.
        return DefWindowProcW( hWnd, msg, wParam, lParam );
    }
} // namespace sw
#else
namespace sw
{
    Win32Window::Win32Window()
        : _hWnd{ nullptr }
        , _bResizing{ SW_FALSE }
        , _reservedWin32{ 0 }
        , _padding{ 0 }
    {
    }

    Win32Window::~Win32Window() = default;

    bool Win32Window::initializeWindow( const utf8*, uint32 width, uint32 height )
    {
        _width  = width;
        _height = height;
        _hWnd   = nullptr;
        return true;
    }

    void Win32Window::destroy()
    {
        _hWnd = nullptr;
    }

    bool Win32Window::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        (void)width;
        (void)height;
        _displayMode = mode;
        return false;
    }

    void Win32Window::captureRestorePosition()
    {
    }

    void Win32Window::clearRestorePosition()
    {
        // 이 플랫폼에서는 창을 만들지 않지만, 생성자가 부르므로 값은 채워 둔다.
        _restoreX = 0;
        _restoreY = 0;
    }

    bool Win32Window::processMessages()
    {
        return _bShouldClose == SW_FALSE;
    }

    void Win32Window::applyTitle()
    {
    }

    void Win32Window::applyWindowVisibility( bool )
    {
    }

    bool Win32Window::isVisible() const
    {
        return false;
    }

    float32 Win32Window::getContentScale() const
    {
        return 1.0f;
    }

    LRESULT CALLBACK Win32Window::wndProc( HWND, UINT, WPARAM, LPARAM )
    {
        return 0;
    }
} // namespace sw
#endif
