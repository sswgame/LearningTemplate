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
        // 복원 위치는 기반(`IWindow`)이 들고 절차도 기반이 돈다 — 플랫폼은 "알아서" 값만 정한다.
        clearRestorePosition();
    }

    Win32Window::~Win32Window()
    {
        Win32Window::destroy();
    }

    /**
     * @brief Win32 윈도우 클래스를 등록하고 오버랩 윈도우(WS_OVERLAPPEDWINDOW)를 생성합니다.
     */
    bool Win32Window::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        _width  = width;
        _height = height;
        _title  = StringUtil::isNullOrEmpty( pTitle ) ? L"" : StringUtil::utf8ToUtf16( pTitle );

        HINSTANCE hInstance = GetModuleHandle( nullptr );

        // CS_OWNDC: DXGI↔OpenGL 핫스왑 시 WGL GetDC/SwapBuffers의 안정성을 보장하기 위해 필수
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof( WNDCLASSEXW );
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc   = wndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = LoadCursor( nullptr, IDC_ARROW );
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
     * @brief Win32 윈도우 핸들을 파괴하고 리소스를 정리합니다.
     */
    void Win32Window::destroy()
    {
        if ( _hWnd != nullptr )
        {
            DestroyWindow( _hWnd );
            _hWnd = nullptr;
        }
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

    /**
     * @brief RHI 백엔드 핫스왑 등을 위해 이전 윈도우 좌표를 유지한 채 윈도우를 다시 생성합니다.
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
        while ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) != 0 )
        {
            if ( msg.message == WM_QUIT )
                return false;

            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        return _bShouldClose == SW_FALSE;
    }

    LRESULT CALLBACK Win32Window::wndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
    {
        Win32Window* pThis{ nullptr };
        if ( msg == WM_NCCREATE )
        {
            CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>( lParam );
            pThis                 = reinterpret_cast<Win32Window*>( pCreate->lpCreateParams );
            SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pThis ) );
            pThis->_hWnd = hWnd;
        }
        else
            pThis = reinterpret_cast<Win32Window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) );

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
                    // 0x0 짜리 WM_SIZE 로 온다 — 그 값을 `_width`/`_height` 에 적어 두면 창이
                    // 0 칸짜리가 됐다는 뜻이 되어 버린다. 그 상태에서 `recreate()`(백엔드 교체가
                    // 이 길로 온다)가 돌면 기억해 둔 0x0 으로 창을 다시 만들고, 복원해도 그 크기가
                    // 그대로 남는다. 최소화가 말하는 것은 "안 보인다" 지 "0 칸이다" 가 아니다.
                    const uint32 clientWidth  = LOWORD( lParam );
                    const uint32 clientHeight = HIWORD( lParam );
                    if ( wParam == SIZE_MINIMIZED || clientWidth == 0 || clientHeight == 0 )
                        return 0;

                    pThis->_width  = clientWidth;
                    pThis->_height = clientHeight;
                    // DPI 변경 등으로 ShowWindow/SetForegroundWindow 처리 중 OS가 GetSystemMetricsForDpi
                    // 등을 통해 SendMessageW로 같은 스레드에 재진입 WM_SIZE를 보낼 수 있다 — 재진입 가드
                    // 없이 onResize(스왑체인 리사이즈)를 중첩 호출하면 아직 재생성 중인 렌더타겟을
                    // 다시 정리/재생성하게 되어 DataRaceDetector가 레이스로 감지해 크래시한다.
                    if ( pThis->_bRecreating == SW_FALSE && pThis->_bResizing == SW_FALSE && pThis->_onResize.isBound() )
                    {
                        pThis->_bResizing = SW_TRUE;
                        pThis->_onResize( pThis->_width, pThis->_height );
                        pThis->_bResizing = SW_FALSE;
                    }
                    return 0;
                }

                case WM_CLOSE:
                {
                    if ( pThis->_bRecreating == SW_FALSE )
                        pThis->tryBeginClose();
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
                    break;
            }
        }

        return DefWindowProc( hWnd, msg, wParam, lParam );
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

    void Win32Window::applyWindowVisibility( bool )
    {
    }

    bool Win32Window::isVisible() const
    {
        return false;
    }

    LRESULT CALLBACK Win32Window::wndProc( HWND, UINT, WPARAM, LPARAM )
    {
        return 0;
    }
} // namespace sw
#endif
