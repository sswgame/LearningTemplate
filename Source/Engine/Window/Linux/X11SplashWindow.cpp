#include "pch.h"

#include "Engine/Window/Linux/X11SplashWindow.h"

#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"
#endif

namespace sw
{
    X11SplashWindow::X11SplashWindow()
        : ISplashWindow{}
        , _pX11Display{ nullptr }
        , _x11Window{ 0 }
        , _x11Pixmap{ 0 }
        , _listScaledPixel{}
    {
    }

    void X11SplashWindow::buildScaledImage()
    {
        _listScaledPixel.clear();
        if ( _splashData.isValid() == false || _splashData.getPixels() == nullptr )
            return;
        if ( _width == 0 || _height == 0 )
            return;

        _listScaledPixel.assign( static_cast<size_t>( _width ) * static_cast<size_t>( _height ) * 4u, 0 );
        scaleBgraImage( _splashData.getPixels(), _splashData._width, _splashData._height,
                        _listScaledPixel.data(), _width, _height );
    }

    X11SplashWindow::~X11SplashWindow()
    {
        X11SplashWindow::dismiss();
    }

#if defined( SW_PLATFORM_LINUX )
    namespace
    {
        // 스플래시 팔레트. 창 배경과 픽스맵 배경이 **같은 값이어야** 노출 순간에 색이 튀지 않는다.
        constexpr uint64 kSplashBackgroundColor = 0x181C24;
        constexpr uint64 kSplashTextColor       = 0xAEC3E6;
        constexpr uint64 kSplashBarTrackColor   = 0x202632;
        constexpr uint64 kSplashBarFillColor    = 0x4691FF;
    } // namespace

    bool X11SplashWindow::initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
        _status = StringUtil::isNullOrEmpty( pInitialStatus ) ? "Initializing..." : pInitialStatus;
        _width  = width;
        _height = height;
        loadSplashImage();
        buildScaledImage();

        Display* pDisplay = XOpenDisplay( nullptr );
        if ( pDisplay == nullptr )
        {
            _bOpen = SW_FALSE;
            return false;
        }

        const int32 screen  = DefaultScreen( pDisplay );
        Window      root    = RootWindow( pDisplay, screen );
        const int32 screenW = DisplayWidth( pDisplay, screen );
        const int32 screenH = DisplayHeight( pDisplay, screen );
        const int32 posX    = ( screenW - static_cast<int32>( _width ) ) / 2;
        const int32 posY    = ( screenH - static_cast<int32>( _height ) ) / 2;

        XSetWindowAttributes attrs{};
        attrs.override_redirect = 1;
        attrs.background_pixel  = kSplashBackgroundColor;

        Window win = XCreateWindow(
            pDisplay, root,
            posX, posY, _width, _height,
            1, CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWBackPixel, &attrs );

        const utf8* pWinTitle = StringUtil::isNullOrEmpty( pTitle ) ? "SW Engine" : pTitle;
        XStoreName( pDisplay, win, pWinTitle );
        XSelectInput( pDisplay, win, ExposureMask | StructureNotifyMask );
        XMapRaised( pDisplay, win );

        // **`XFlush` 가 아니라 `XSync` 다.** 매핑은 요청일 뿐이고, 서버가 그것을 처리하기 전에 그리면
        // 그 그리기는 버려진다 — 첫 화면이 통째로 비는 길이다. 한 번 왕복해 매핑을 확정하고 그린다
        // (`override_redirect` 창이라 창 관리자를 기다릴 일은 없다).
        XSync( pDisplay, 0 ); // discard=0. Xlib 의 `False` 매크로는 이 저장소가 해제한다.

        _pX11Display = pDisplay;
        _x11Window   = static_cast<uint64>( win );
        _bOpen       = SW_TRUE;

        updateStatus( _status.c_str() );
        return true;
    }

    void X11SplashWindow::updateStatus( const utf8* pStatus, float32 progress )
    {
        if ( _bOpen == SW_FALSE )
            return;

        if ( StringUtil::isNullOrEmpty( pStatus ) == false )
            _status = pStatus;
        if ( progress >= 0.0f )
            _progress = ( progress > 1.0f ) ? 1.0f : progress;

        Display* pDisplay = static_cast<Display*>( _pX11Display );
        Window   win      = static_cast<Window>( _x11Window );
        if ( pDisplay == nullptr || win == 0 )
            return;

        const int32 screen = DefaultScreen( pDisplay );
        GC          gc     = DefaultGC( pDisplay, screen );

        // **창에 직접 그리지 않고 픽스맵에 그려 그것을 창 배경으로 건다.**
        //
        // 스플래시에는 이벤트 루프가 없다 — `updateStatus` 가 불릴 때만 그린다. 그런데 X11 은
        // 창이 노출될 때마다(`Expose`) **배경색으로 지우고** 우리에게 다시 그리라고 알린다. 그 알림을
        // 받아 줄 루프가 없으니 그리는 족족 지워졌고, 남는 것은 `background_pixel` 뿐이었다 —
        // 리눅스에서 "뒷배경만 보인다" 던 것이 이것이다. (호출은 전부 정상이었다: 이미지도 읽히고
        // `XPutImage` 도 불렸다. 그린 뒤에 지워진 것이라 로그로는 보이지 않았다.)
        //
        // 배경 픽스맵으로 걸어 두면 **다시 그리는 일을 서버가 한다.** 이벤트 루프 없이도 노출·가림·
        // 이동을 견딘다. Win32 가 `WM_PAINT` 로 하는 일을 여기서는 서버에게 맡기는 셈이다.
        Pixmap pixmap = static_cast<Pixmap>( _x11Pixmap );
        if ( pixmap == 0 )
        {
            pixmap     = XCreatePixmap( pDisplay, win, _width, _height, static_cast<uint32>( DefaultDepth( pDisplay, screen ) ) );
            _x11Pixmap = static_cast<uint64>( pixmap );
        }
        if ( pixmap == 0 )
            return;

        XSetForeground( pDisplay, gc, kSplashBackgroundColor );
        XFillRectangle( pDisplay, pixmap, gc, 0, 0, _width, _height );

        // 창 크기로 줄여 둔 것을 찍는다. `XPutImage` 는 늘리거나 줄이지 못하므로 원본을 그대로
        // 넘기면 창보다 큰 이미지는 좌상단만 보인다(1376×768 원본 · 480×280 창).
        if ( _listScaledPixel.empty() == false )
        {
            XImage* pImage = XCreateImage(
                pDisplay, DefaultVisual( pDisplay, screen ),
                static_cast<uint32>( DefaultDepth( pDisplay, screen ) ), ZPixmap, 0,
                reinterpret_cast<utf8*>( _listScaledPixel.data() ),
                _width, _height, 32, 0 );
            if ( pImage != nullptr )
            {
                XPutImage( pDisplay, pixmap, gc, pImage, 0, 0, 0, 0, _width, _height );
                pImage->data = nullptr;
                XDestroyImage( pImage );
            }
        }

        XSetForeground( pDisplay, gc, kSplashTextColor );
        XDrawString( pDisplay, pixmap, gc, 32, static_cast<int32>( _height ) - 50, _status.c_str(), static_cast<int32>( _status.length() ) );

        const int32   totalBarWidth   = static_cast<int32>( _width ) - 64;
        const float32 clampedProgress = ( _progress < 0.0f ) ? 0.0f : ( ( _progress > 1.0f ) ? 1.0f : _progress );
        const int32   fillWidth       = static_cast<int32>( static_cast<float32>( totalBarWidth ) * clampedProgress );

        XSetForeground( pDisplay, gc, kSplashBarTrackColor );
        XFillRectangle( pDisplay, pixmap, gc, 32, static_cast<int32>( _height ) - 30, static_cast<uint32>( totalBarWidth ), 4 );
        if ( fillWidth > 0 )
        {
            XSetForeground( pDisplay, gc, kSplashBarFillColor );
            XFillRectangle( pDisplay, pixmap, gc, 32, static_cast<int32>( _height ) - 30, static_cast<uint32>( fillWidth ), 4 );
        }

        // 배경으로 걸고 창을 지우면 서버가 그 픽스맵으로 칠한다. 이후의 노출도 서버가 알아서 한다.
        XSetWindowBackgroundPixmap( pDisplay, win, pixmap );
        XClearWindow( pDisplay, win );
        XFlush( pDisplay );
    }

    void X11SplashWindow::setProgress( float32 progress )
    {
        updateStatus( nullptr, progress );
    }

    void X11SplashWindow::dismiss()
    {
        if ( _bOpen == SW_FALSE )
            return;

        Display* pDisplay = static_cast<Display*>( _pX11Display );
        Window   win      = static_cast<Window>( _x11Window );
        if ( pDisplay != nullptr && win != 0 )
        {
            if ( _x11Pixmap != 0 )
            {
                XFreePixmap( pDisplay, static_cast<Pixmap>( _x11Pixmap ) );
                _x11Pixmap = 0;
            }
            XUnmapWindow( pDisplay, win );
            XDestroyWindow( pDisplay, win );
            XCloseDisplay( pDisplay );
            _pX11Display = nullptr;
            _x11Window   = 0;
        }

        _bOpen = SW_FALSE;
    }

#else

    bool X11SplashWindow::initialize( const utf8*, const utf8*, uint32, uint32 ) { return false; }
    void X11SplashWindow::updateStatus( const utf8*, float32 ) {}
    void X11SplashWindow::setProgress( float32 ) {}
    void X11SplashWindow::dismiss() {}

#endif
} // namespace sw
