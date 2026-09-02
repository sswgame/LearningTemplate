#include "pch.h"

#include "Engine/Window/Windows/Win32SplashWindow.h"

#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    namespace
    {
        struct Win32SplashWindowInternal
        {
            static constexpr const utf16* kSplashClassName = L"SWSplashWindowClass";

            static LRESULT CALLBACK splashWndProcInternal( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
            {
                switch ( msg )
                {
                    case WM_ERASEBKGND:
                        return 1; // 깜빡임 방지

                    case WM_PAINT:
                    {
                        PAINTSTRUCT ps;
                        HDC         hDC = BeginPaint( hWnd, &ps );

                        Win32SplashWindow* pSplash = reinterpret_cast<Win32SplashWindow*>( GetWindowLongPtrW( hWnd, GWLP_USERDATA ) );
                        RECT               rc;
                        GetClientRect( hWnd, &rc );

                        // 1) 배경 채우기 (다크 테마 RGB: 24, 28, 36)
                        HBRUSH hBgBrush = CreateSolidBrush( RGB( 24, 28, 36 ) );
                        FillRect( hDC, &rc, hBgBrush );
                        DeleteObject( hBgBrush );

                        // 2) 외곽 테두리 (RGB: 55, 65, 82)
                        HPEN    hBorderPen = CreatePen( PS_SOLID, 1, RGB( 55, 65, 82 ) );
                        HGDIOBJ hOldPen    = SelectObject( hDC, hBorderPen );
                        HGDIOBJ hOldBrush  = SelectObject( hDC, GetStockObject( NULL_BRUSH ) );
                        Rectangle( hDC, rc.left, rc.top, rc.right, rc.bottom );
                        SelectObject( hDC, hOldPen );
                        SelectObject( hDC, hOldBrush );
                        DeleteObject( hBorderPen );

                        // 3) 상단 액센트 라인 (RGB: 75, 140, 245)
                        HBRUSH hAccentBrush = CreateSolidBrush( RGB( 75, 140, 245 ) );
                        RECT   rcAccent     = { rc.left + 1, rc.top + 1, rc.right - 1, rc.top + 4 };
                        FillRect( hDC, &rcAccent, hAccentBrush );
                        DeleteObject( hAccentBrush );

                        SetBkMode( hDC, TRANSPARENT );

                        // 4) 메인 타이틀 (Segoe UI Bold, 20pt)
                        HFONT hTitleFont = CreateFontW(
                            -26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI" );
                        HGDIOBJ hPrevFont = SelectObject( hDC, hTitleFont );
                        SetTextColor( hDC, RGB( 240, 245, 255 ) );

                        RECT    rcTitle = { 32, 36, rc.right - 32, 75 };
                        wstring wsTitle = ( pSplash != nullptr ) ? StringUtil::utf8ToUtf16( pSplash->getTitle().c_str() ) : L"SW Engine";
                        DrawTextW( hDC, wsTitle.c_str(), -1, &rcTitle, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX );

                        // 5) 서브타이틀 (Segoe UI Regular, 10pt)
                        HFONT hSubFont = CreateFontW(
                            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI" );
                        SelectObject( hDC, hSubFont );
                        SetTextColor( hDC, RGB( 130, 150, 180 ) );

                        RECT rcSub = { 32, 75, rc.right - 32, 100 };
                        DrawTextW( hDC, L"Game & Editor Engine Template (LiveReload Enabled)", -1, &rcSub, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX );

                        // 6) 상태 진행 텍스트 (Segoe UI, 11pt)
                        SelectObject( hDC, hSubFont );
                        SetTextColor( hDC, RGB( 180, 205, 235 ) );

                        wstring wsStatus = ( pSplash != nullptr ) ? StringUtil::utf8ToUtf16( pSplash->getStatus().c_str() ) : L"Initializing Engine...";
                        RECT    rcStatus = { 32, rc.bottom - 68, rc.right - 32, rc.bottom - 44 };
                        DrawTextW( hDC, wsStatus.c_str(), -1, &rcStatus, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX );

                        // 7) 하단 프로그레스 바 영역
                        RECT   rcProgBg     = { 32, rc.bottom - 38, rc.right - 32, rc.bottom - 32 };
                        HBRUSH hProgBgBrush = CreateSolidBrush( RGB( 38, 44, 58 ) );
                        FillRect( hDC, &rcProgBg, hProgBgBrush );
                        DeleteObject( hProgBgBrush );

                        RECT   rcProgFill     = { 32, rc.bottom - 38, rc.right - 80, rc.bottom - 32 };
                        HBRUSH hProgFillBrush = CreateSolidBrush( RGB( 60, 130, 240 ) );
                        FillRect( hDC, &rcProgFill, hProgFillBrush );
                        DeleteObject( hProgFillBrush );

                        SelectObject( hDC, hPrevFont );
                        DeleteObject( hTitleFont );
                        DeleteObject( hSubFont );

                        EndPaint( hWnd, &ps );
                        return 0;
                    }

                    case WM_DESTROY:
                        return 0;

                    default:
                        break;
                }

                return DefWindowProcW( hWnd, msg, wParam, lParam );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    Win32SplashWindow::Win32SplashWindow()
        : ISplashWindow{}
        , _hWnd{ nullptr }
    {
    }

    Win32SplashWindow::~Win32SplashWindow()
    {
        dismiss();
    }

    LRESULT CALLBACK Win32SplashWindow::splashWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
    {
        return Win32SplashWindowInternal::splashWndProcInternal( hWnd, msg, wParam, lParam );
    }

    bool Win32SplashWindow::initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
        _title  = ( pTitle != nullptr && pTitle[0] != '\0' ) ? string{ pTitle } : "SW Engine";
        _status = ( pInitialStatus != nullptr && pInitialStatus[0] != '\0' ) ? string{ pInitialStatus } : "Initializing...";
        _width  = width;
        _height = height;

        HINSTANCE hInstance = GetModuleHandleW( nullptr );

        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof( WNDCLASSEXW );
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc   = splashWndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = LoadCursor( nullptr, IDC_ARROW );
        wc.lpszClassName = Win32SplashWindowInternal::kSplashClassName;

        RegisterClassExW( &wc );

        const int32 screenW = GetSystemMetrics( SM_CXSCREEN );
        const int32 screenH = GetSystemMetrics( SM_CYSCREEN );
        const int32 posX    = ( screenW - static_cast<int32>( _width ) ) / 2;
        const int32 posY    = ( screenH - static_cast<int32>( _height ) ) / 2;

        HWND hWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            Win32SplashWindowInternal::kSplashClassName,
            L"SW Engine Splash",
            WS_POPUP,
            posX,
            posY,
            static_cast<int32>( _width ),
            static_cast<int32>( _height ),
            nullptr,
            nullptr,
            hInstance,
            nullptr );

        if ( hWnd == nullptr )
            return false;

        _hWnd  = hWnd;
        _bOpen = true;

        SetWindowLongPtrW( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( this ) );

        ShowWindow( hWnd, SW_SHOWNORMAL );
        UpdateWindow( hWnd );

        MSG msg;
        while ( PeekMessageW( &msg, hWnd, 0, 0, PM_REMOVE ) )
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }

        return true;
    }

    void Win32SplashWindow::updateStatus( const utf8* pStatus )
    {
        if ( _bOpen == false )
            return;

        _status = ( pStatus != nullptr ) ? string{ pStatus } : "";

        if ( _hWnd != nullptr && IsWindow( _hWnd ) )
        {
            InvalidateRect( _hWnd, nullptr, TRUE );
            UpdateWindow( _hWnd );

            MSG msg;
            while ( PeekMessageW( &msg, _hWnd, 0, 0, PM_REMOVE ) )
            {
                TranslateMessage( &msg );
                DispatchMessageW( &msg );
            }
        }
    }

    void Win32SplashWindow::dismiss()
    {
        if ( _bOpen == false )
            return;

        if ( _hWnd != nullptr && IsWindow( _hWnd ) )
        {
            DestroyWindow( _hWnd );
            _hWnd = nullptr;
        }

        _bOpen = false;
    }
} // namespace sw

#else

namespace sw
{
    Win32SplashWindow::Win32SplashWindow()
        : ISplashWindow{}
        , _hWnd{ nullptr }
    {
    }

    Win32SplashWindow::~Win32SplashWindow() = default;

    bool             Win32SplashWindow::initialize( const utf8*, const utf8*, uint32, uint32 ) { return false; }
    void             Win32SplashWindow::updateStatus( const utf8* ) {}
    void             Win32SplashWindow::dismiss() {}
    LRESULT CALLBACK Win32SplashWindow::splashWndProc( HWND, UINT, WPARAM, LPARAM ) { return 0; }
} // namespace sw

#endif
