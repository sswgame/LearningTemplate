#include "pch.h"

#include "Engine/Window/Windows/Win32SplashWindow.h"

#include "Core/Common/StdHeaders.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include <gdiplus.h>
    #pragma comment( lib, "gdiplus.lib" )

namespace sw
{
    namespace
    {
        struct Win32SplashWindowInternal
        {
            static constexpr const utf16* kSplashClassName = L"SWSplashWindowClass";
            /// @brief 아래 상태 띠(그라디언트 · 글자 · 진행 막대)의 높이입니다. 상태가 바뀌면 이 띠만 다시 그립니다.
            static constexpr int32 kStatusBandHeight = 54;

            static LRESULT CALLBACK splashWndProcInternal( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
            {
                switch ( msg )
                {
                    case WM_ERASEBKGND:
                        return 1; // 깜빡임 방지

                    case WM_PAINT:
                    {
                        PAINTSTRUCT        ps;
                        HDC                hDC     = BeginPaint( hWnd, &ps );
                        Win32SplashWindow* pSplash = reinterpret_cast<Win32SplashWindow*>( GetWindowLongPtrW( hWnd, GWLP_USERDATA ) );
                        if ( pSplash != nullptr )
                            pSplash->paintWindow( hDC, ps.rcPaint );
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
        , _gdiplusToken{ 0 }
        , _hBackgroundDC{ nullptr }
        , _hBackgroundBitmap{ nullptr }
        , _hBackgroundPreviousBitmap{ nullptr }
        , _hStatusFont{ nullptr }
        , _backgroundWidth{ 0 }
        , _backgroundHeight{ 0 }
    {
    }

    Win32SplashWindow::~Win32SplashWindow()
    {
        Win32SplashWindow::dismiss();
    }

    LRESULT CALLBACK Win32SplashWindow::splashWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
    {
        return Win32SplashWindowInternal::splashWndProcInternal( hWnd, msg, wParam, lParam );
    }

    bool Win32SplashWindow::initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
        _status = StringUtil::isNullOrEmpty( pInitialStatus ) ? "Initializing..." : pInitialStatus;
        _width  = width;
        _height = height;

        Gdiplus::GdiplusStartupInput gdiplusStartupInput{};
        Gdiplus::GdiplusStartup( reinterpret_cast<ULONG_PTR*>( &_gdiplusToken ), &gdiplusStartupInput, nullptr );

        // 채널 뒤집기는 여기 있지 않다. `loadSplashImage()` 가 **모든 플랫폼에** BGRA 를 보장한다.
        // 이 자리에만 두었던 탓에 리눅스는 같은 보정을 받지 못하고 있었다.
        loadSplashImage();

        HINSTANCE hInstance = GetModuleHandleW( nullptr );

        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof( WNDCLASSEXW );
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc   = splashWndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = LoadCursor( nullptr, IDC_ARROW );
        wc.hbrBackground = nullptr;
        wc.lpszClassName = Win32SplashWindowInternal::kSplashClassName;

        RegisterClassExW( &wc );

        const int32 screenW = GetSystemMetrics( SM_CXSCREEN );
        const int32 screenH = GetSystemMetrics( SM_CYSCREEN );
        const int32 posX    = ( screenW - static_cast<int32>( _width ) ) / 2;
        const int32 posY    = ( screenH - static_cast<int32>( _height ) ) / 2;

        const wstring wsTitle = StringUtil::isNullOrEmpty( pTitle ) ? L"SW Engine Splash" : StringUtil::utf8ToUtf16( pTitle );

        HWND hWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            Win32SplashWindowInternal::kSplashClassName,
            wsTitle.c_str(),
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
        _bOpen = SW_TRUE;

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

    void Win32SplashWindow::updateStatus( const utf8* pStatus, float32 progress )
    {
        if ( _bOpen == SW_FALSE )
            return;

        if ( StringUtil::isNullOrEmpty( pStatus ) == false )
            _status = pStatus;
        if ( progress >= 0.0f )
            _progress = ( progress > 1.0f ) ? 1.0f : progress;

        if ( _hWnd != nullptr && IsWindow( _hWnd ) )
        {
            // 바뀌는 것은 아래 상태 띠(글자 · 진행 막대)뿐이다. 그 띠만 무효로 해 배경을 다시 늘리지 않는다.
            RECT rcClient{};
            GetClientRect( _hWnd, &rcClient );
            const RECT rcBand = { 0, rcClient.bottom - Win32SplashWindowInternal::kStatusBandHeight, rcClient.right, rcClient.bottom };
            InvalidateRect( _hWnd, &rcBand, FALSE );
            UpdateWindow( _hWnd );

            MSG msg;
            while ( PeekMessageW( &msg, _hWnd, 0, 0, PM_REMOVE ) )
            {
                TranslateMessage( &msg );
                DispatchMessageW( &msg );
            }
        }
    }

    void Win32SplashWindow::setProgress( float32 progress )
    {
        updateStatus( nullptr, progress );
    }

    void Win32SplashWindow::paintWindow( HDC hDC, const RECT& rcPaint )
    {
        const DdsImageData& splashData = getSplashImage();
        if ( splashData.isValid() == false || _hWnd == nullptr )
            return;

        RECT rc{};
        GetClientRect( _hWnd, &rc );
        const int32 width  = rc.right - rc.left;
        const int32 height = rc.bottom - rc.top;

        // 1) 배경: 창 크기로 늘린 그림을 처음 한 번 메모리 DC 에 그려 두고, 무효 영역만 복사한다(HALFTONE 늘리기가 비싸다).
        if ( _hBackgroundDC == nullptr || _backgroundWidth != width || _backgroundHeight != height )
        {
            releasePaintCache();
            _hBackgroundDC             = CreateCompatibleDC( hDC );
            _hBackgroundBitmap         = CreateCompatibleBitmap( hDC, width, height );
            _hBackgroundPreviousBitmap = SelectObject( _hBackgroundDC, _hBackgroundBitmap );
            _backgroundWidth           = width;
            _backgroundHeight          = height;

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize        = sizeof( BITMAPINFOHEADER );
            bmi.bmiHeader.biWidth       = static_cast<LONG>( splashData._width );
            bmi.bmiHeader.biHeight      = -static_cast<LONG>( splashData._height ); // 위에서 아래로(top-down)
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            SetStretchBltMode( _hBackgroundDC, HALFTONE );
            SetBrushOrgEx( _hBackgroundDC, 0, 0, nullptr ); // HALFTONE 로 바꾼 뒤에는 브러시 원점을 다시 맞추라는 것이 GDI 의 규칙이다
            StretchDIBits( _hBackgroundDC, 0, 0, width, height, 0, 0, static_cast<int32>( splashData._width ),
                           static_cast<int32>( splashData._height ), splashData.getPixels(), &bmi, DIB_RGB_COLORS, SRCCOPY );
        }
        BitBlt( hDC, rcPaint.left, rcPaint.top, rcPaint.right - rcPaint.left, rcPaint.bottom - rcPaint.top, _hBackgroundDC, rcPaint.left,
                rcPaint.top, SRCCOPY );

        // 2) 아래 상태 띠 그라디언트 오버레이. 배경을 다시 깐 위에 그리므로 겹쳐 짙어지지 않는다.
        const int32                  bandHeight = Win32SplashWindowInternal::kStatusBandHeight;
        Gdiplus::Graphics            graphics( hDC );
        Gdiplus::Rect                gradientRect( 0, rc.bottom - bandHeight, rc.right, bandHeight );
        Gdiplus::LinearGradientBrush gradientBrush( gradientRect, Gdiplus::Color( 0, 16, 20, 26 ), Gdiplus::Color( 230, 16, 20, 26 ),
                                                    Gdiplus::LinearGradientModeVertical );
        graphics.FillRectangle( &gradientBrush, gradientRect );

        // 3) 상태 진행 텍스트(Segoe UI). 글꼴은 한 번 만들어 둔다.
        if ( _hStatusFont == nullptr )
        {
            _hStatusFont = CreateFontW( -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI" );
        }
        SetBkMode( hDC, TRANSPARENT );
        HGDIOBJ hPrevFont = SelectObject( hDC, _hStatusFont );
        SetTextColor( hDC, RGB( 190, 215, 245 ) );

        const wstring wsStatus = StringUtil::utf8ToUtf16( getStatus().c_str() );
        RECT          rcStatus = { 24, rc.bottom - 36, rc.right - 24, rc.bottom - 16 };
        DrawTextW( hDC, wsStatus.c_str(), -1, &rcStatus, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX );

        // 4) 아래쪽 진행 막대 배경(어두운 차콜)
        RECT   rcProgBg     = { 0, rc.bottom - 4, rc.right, rc.bottom };
        HBRUSH hProgBgBrush = CreateSolidBrush( RGB( 20, 24, 30 ) );
        FillRect( hDC, &rcProgBg, hProgBgBrush );
        DeleteObject( hProgBgBrush );

        // 5) 실제 진행률만큼 채우는 진행 선
        const float32 progress        = getProgress();
        const float32 clampedProgress = ( progress < 0.0f ) ? 0.0f : ( ( progress > 1.0f ) ? 1.0f : progress );
        const int32   fillWidth       = static_cast<int32>( static_cast<float32>( rc.right ) * clampedProgress );
        if ( fillWidth > 0 )
        {
            RECT   rcProgFill     = { 0, rc.bottom - 4, fillWidth, rc.bottom };
            HBRUSH hProgFillBrush = CreateSolidBrush( RGB( 70, 145, 255 ) );
            FillRect( hDC, &rcProgFill, hProgFillBrush );
            DeleteObject( hProgFillBrush );
        }

        SelectObject( hDC, hPrevFont );
    }

    void Win32SplashWindow::releasePaintCache()
    {
        if ( _hBackgroundDC != nullptr )
        {
            if ( _hBackgroundPreviousBitmap != nullptr )
                SelectObject( _hBackgroundDC, _hBackgroundPreviousBitmap );
            DeleteDC( _hBackgroundDC );
        }
        if ( _hBackgroundBitmap != nullptr )
            DeleteObject( _hBackgroundBitmap );
        if ( _hStatusFont != nullptr )
            DeleteObject( _hStatusFont );
        _hBackgroundDC             = nullptr;
        _hBackgroundBitmap         = nullptr;
        _hBackgroundPreviousBitmap = nullptr;
        _hStatusFont               = nullptr;
        _backgroundWidth           = 0;
        _backgroundHeight          = 0;
    }

    void Win32SplashWindow::dismiss()
    {
        releasePaintCache();
        if ( _bOpen == SW_FALSE && _hWnd == nullptr && _gdiplusToken == 0 )
            return;

        if ( _hWnd != nullptr && IsWindow( _hWnd ) )
        {
            DestroyWindow( _hWnd );
            _hWnd = nullptr;
        }

        if ( _gdiplusToken != 0 )
        {
            Gdiplus::GdiplusShutdown( static_cast<ULONG_PTR>( _gdiplusToken ) );
            _gdiplusToken = 0;
        }

        _bOpen = SW_FALSE;
    }
} // namespace sw

#else

namespace sw
{
    Win32SplashWindow::Win32SplashWindow()
        : ISplashWindow{}
        , _hWnd{ nullptr }
        , _gdiplusToken{ 0 }
    {
    }

    Win32SplashWindow::~Win32SplashWindow() = default;

    bool             Win32SplashWindow::initialize( const utf8*, const utf8*, uint32, uint32 ) { return false; }
    void             Win32SplashWindow::updateStatus( const utf8*, float32 ) {}
    void             Win32SplashWindow::setProgress( float32 ) {}
    void             Win32SplashWindow::dismiss() {}
    LRESULT CALLBACK Win32SplashWindow::splashWndProc( HWND, UINT, WPARAM, LPARAM ) { return 0; }
} // namespace sw

#endif
