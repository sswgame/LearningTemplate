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
    {
    }

    X11SplashWindow::~X11SplashWindow()
    {
        dismiss();
    }

#if defined( SW_PLATFORM_LINUX )
    bool X11SplashWindow::initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
        _title           = StringUtil::isNullOrEmpty( pTitle ) ? "SW Engine" : pTitle;
        _status          = StringUtil::isNullOrEmpty( pInitialStatus ) ? "Initializing..." : pInitialStatus;
        _width           = width;
        _height          = height;
        _splashImagePath = findSplashImagePath();
        if ( _splashImagePath.empty() == false )
            loadSplashImage( _splashImagePath );

        Display* pDisplay = XOpenDisplay( nullptr );
        if ( pDisplay == nullptr )
        {
            _bOpen = false;
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
        attrs.background_pixel  = 0x181C24;

        Window win = XCreateWindow(
            pDisplay, root,
            posX, posY, _width, _height,
            1, CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWBackPixel, &attrs );

        XStoreName( pDisplay, win, _title.c_str() );
        XSelectInput( pDisplay, win, ExposureMask | StructureNotifyMask );
        XMapRaised( pDisplay, win );
        XFlush( pDisplay );

        _pX11Display = pDisplay;
        _x11Window   = static_cast<uint64>( win );
        _bOpen       = true;

        updateStatus( _status.c_str() );
        return true;
    }

    void X11SplashWindow::updateStatus( const utf8* pStatus, float32 progress )
    {
        if ( _bOpen == false )
            return;

        if ( StringUtil::isNullOrEmpty( pStatus ) == false )
            _status = pStatus;
        if ( progress >= 0.0f )
            _progress = ( progress > 1.0f ) ? 1.0f : progress;

        Display* pDisplay = static_cast<Display*>( _pX11Display );
        Window   win      = static_cast<Window>( _x11Window );
        if ( pDisplay != nullptr && win != 0 )
        {
            const int32 screen = DefaultScreen( pDisplay );
            GC          gc     = DefaultGC( pDisplay, screen );

            if ( _splashImage._pPixels != nullptr && _splashImage._width > 0 && _splashImage._height > 0 )
            {
                XImage* pImage = XCreateImage(
                    pDisplay, DefaultVisual( pDisplay, screen ),
                    DefaultDepth( pDisplay, screen ), ZPixmap, 0,
                    reinterpret_cast<char*>( _splashImage._pPixels ),
                    _splashImage._width, _splashImage._height, 32, 0 );
                if ( pImage != nullptr )
                {
                    XPutImage( pDisplay, win, gc, pImage, 0, 0, 0, 0, _splashImage._width, _splashImage._height );
                    pImage->data = nullptr;
                    XDestroyImage( pImage );
                }
            }

            XSetForeground( pDisplay, gc, 0xAEC3E6 );
            XDrawString( pDisplay, win, gc, 32, static_cast<int32>( _height ) - 50, _status.c_str(), static_cast<int32>( _status.length() ) );

            const int32   totalBarWidth   = static_cast<int32>( _width ) - 64;
            const float32 clampedProgress = ( _progress < 0.0f ) ? 0.0f : ( ( _progress > 1.0f ) ? 1.0f : _progress );
            const int32   fillWidth       = static_cast<int32>( static_cast<float32>( totalBarWidth ) * clampedProgress );

            XSetForeground( pDisplay, gc, 0x202632 );
            XFillRectangle( pDisplay, win, gc, 32, static_cast<int32>( _height ) - 30, totalBarWidth, 4 );
            if ( fillWidth > 0 )
            {
                XSetForeground( pDisplay, gc, 0x4691FF );
                XFillRectangle( pDisplay, win, gc, 32, static_cast<int32>( _height ) - 30, fillWidth, 4 );
            }

            XFlush( pDisplay );
        }
    }

    void X11SplashWindow::setProgress( float32 progress )
    {
        updateStatus( nullptr, progress );
    }

    void X11SplashWindow::dismiss()
    {
        if ( _bOpen == false )
            return;

        Display* pDisplay = static_cast<Display*>( _pX11Display );
        Window   win      = static_cast<Window>( _x11Window );
        if ( pDisplay != nullptr && win != 0 )
        {
            XUnmapWindow( pDisplay, win );
            XDestroyWindow( pDisplay, win );
            XCloseDisplay( pDisplay );
            _pX11Display = nullptr;
            _x11Window   = 0;
        }

        _bOpen = false;
    }

#else

    bool X11SplashWindow::initialize( const utf8*, const utf8*, uint32, uint32 ) { return false; }
    void X11SplashWindow::updateStatus( const utf8*, float32 ) {}
    void X11SplashWindow::setProgress( float32 ) {}
    void X11SplashWindow::dismiss() {}

#endif
} // namespace sw
