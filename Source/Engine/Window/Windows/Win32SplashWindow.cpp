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
            static constexpr const wchar_t* kSplashClassName = L"SWSplashWindowClass";

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

                        if ( pSplash != nullptr && pSplash->getSplashImage().isValid() )
                        {
                            const auto& splashData = pSplash->getSplashImage();

                            BITMAPINFO bmi{};
                            bmi.bmiHeader.biSize        = sizeof( BITMAPINFOHEADER );
                            bmi.bmiHeader.biWidth       = static_cast<LONG>( splashData._width );
                            bmi.bmiHeader.biHeight      = -static_cast<LONG>( splashData._height ); // Top-down
                            bmi.bmiHeader.biPlanes      = 1;
                            bmi.bmiHeader.biBitCount    = 32;
                            bmi.bmiHeader.biCompression = BI_RGB;

                            SetStretchBltMode( hDC, HALFTONE );
                            StretchDIBits(
                                hDC,
                                0, 0, rc.right - rc.left, rc.bottom - rc.top,
                                0, 0, static_cast<int32>( splashData._width ), static_cast<int32>( splashData._height ),
                                splashData.getPixels(),
                                &bmi,
                                DIB_RGB_COLORS,
                                SRCCOPY );

                            // 하단 상태 텍스트 영역 그라디언트 오버레이
                            Gdiplus::Graphics            graphics( hDC );
                            Gdiplus::Rect                gradientRect( 0, rc.bottom - 54, rc.right, 54 );
                            Gdiplus::LinearGradientBrush gradientBrush(
                                gradientRect,
                                Gdiplus::Color( 0, 16, 20, 26 ),
                                Gdiplus::Color( 230, 16, 20, 26 ),
                                Gdiplus::LinearGradientModeVertical );
                            graphics.FillRectangle( &gradientBrush, gradientRect );

                            // 상태 진행 텍스트 (Segoe UI, 11pt)
                            SetBkMode( hDC, TRANSPARENT );
                            HFONT hSubFont = CreateFontW(
                                -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI" );
                            HGDIOBJ hPrevFont = SelectObject( hDC, hSubFont );
                            SetTextColor( hDC, RGB( 190, 215, 245 ) );

                            wstring wsStatus = StringUtil::utf8ToUtf16( pSplash->getStatus().c_str() );
                            RECT    rcStatus = { 24, rc.bottom - 36, rc.right - 24, rc.bottom - 16 };
                            DrawTextW( hDC, wsStatus.c_str(), -1, &rcStatus, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX );

                            // 하단 프로그레스 바 배경 (어두운 반투명/차콜)
                            RECT   rcProgBg     = { 0, rc.bottom - 4, rc.right, rc.bottom };
                            HBRUSH hProgBgBrush = CreateSolidBrush( RGB( 20, 24, 30 ) );
                            FillRect( hDC, &rcProgBg, hProgBgBrush );
                            DeleteObject( hProgBgBrush );

                            // 실제 진행률에 따른 프로그레스 라인
                            const float32 progress        = ( pSplash != nullptr ) ? pSplash->getProgress() : 0.0f;
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
                            DeleteObject( hSubFont );
                        }

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
        _status = StringUtil::isNullOrEmpty( pInitialStatus ) ? "Initializing..." : pInitialStatus;
        _width  = width;
        _height = height;

        Gdiplus::GdiplusStartupInput gdiplusStartupInput{};
        Gdiplus::GdiplusStartup( reinterpret_cast<ULONG_PTR*>( &_gdiplusToken ), &gdiplusStartupInput, nullptr );

        if ( loadSplashImage() && _splashData.getPixels() != nullptr )
        {
            if ( _splashData._bIsBgra == SW_FALSE )
            {
                const int32 totalPixels = static_cast<int32>( _splashData._width * _splashData._height );
                for ( int32 index = 0; index < totalPixels; ++index )
                {
                    uint8* pPixel = _splashData.getPixels() + ( index * 4 );
                    std::swap( pPixel[0], pPixel[2] );
                }
                _splashData._bIsBgra = SW_TRUE;
            }
        }

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

    void Win32SplashWindow::setProgress( float32 progress )
    {
        updateStatus( nullptr, progress );
    }

    void Win32SplashWindow::dismiss()
    {
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
