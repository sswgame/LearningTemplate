#include "pch.h"

#include "Engine/Window/Linux/X11SplashWindow.h"

#include "Core/String/StringUtil.h"

#include "Engine/Window/Linux/X11Monitors.h"
#include "Engine/Window/WindowMonitor.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Foundation/PlatformOSHeaders.h"
    #include "Core/Foundation/X11Headers.h"

namespace sw
{
    X11SplashWindow::X11SplashWindow()
        : SplashWindowBase{}
        , _splashThread{}
    {
    }

    X11SplashWindow::~X11SplashWindow()
    {
        X11SplashWindow::dismiss();
    }

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
        createScaledImage(); // 이미지는 부른 쪽이 먼저 읽어 두었다(`SplashWindow::initialize`) — 없으면 빈 스플래시다

        // 스플래시 스레드와 주 창이 각자의 연결을 동시에 쓴다 — 첫 Xlib 호출 전에 스레드 초기화를 한다(스플래시가 기동에서 가장 먼저 X 를 연다).
        (void)XInitThreads();
        const string                  title = StringUtil::isNullOrEmpty( pTitle ) ? string( "SW Engine" ) : string( pTitle );
        LinuxSplashThread::PaintState initialState;
        initialState._status   = _status;
        initialState._progress = _progress;
        const bool bOpened     = _splashThread.start( [this, title]( LinuxSplashThread& splashThread )
        { runEventLoop( splashThread, title ); }, initialState );
        _bOpen                 = bOpened ? SW_TRUE : SW_FALSE;
        return bOpened;
    }

    void X11SplashWindow::runEventLoop( LinuxSplashThread& splashThread, const string& title )
    {
        Display* pDisplay = XOpenDisplay( nullptr );
        if ( pDisplay == nullptr )
        {
            splashThread.markFailed();
            return;
        }

        const int32 screen = DefaultScreen( pDisplay );
        Window      root   = RootWindow( pDisplay, screen );
        // 주 모니터의 가운데다 - X screen 은 모니터를 모두 이은 크기라 그 가운데는 모니터 경계에 걸린다.
        X11Monitors monitors;
        monitors.initialize( pDisplay );
        vector<WindowMonitor> listMonitor;
        monitors.read( 1.0f, listMonitor );
        monitors.shutdown();
        const WindowMonitor* const pPrimary = WindowMonitor::findPrimary( listMonitor );
        const WindowMonitorRect    area     = pPrimary != nullptr ? pPrimary->_area : WindowMonitorRect{
                                                                                   int2{ 0, 0 },
                                                                                   int2{ DisplayWidth( pDisplay, screen ), DisplayHeight( pDisplay, screen ) }
        };
        const int32                posX     = area._position._x + ( area._size._x - static_cast<int32>( _width ) ) / 2;
        const int32                posY     = area._position._y + ( area._size._y - static_cast<int32>( _height ) ) / 2;

        // **`override_redirect` 를 쓰지 않는다.** 그 플래그는 "창 관리자는 이 창에 손대지 말라" 는
        // 뜻이고, 메뉴나 툴팁처럼 **이미 떠 있는 창에 딸린 것**을 위한 것이다. 스플래시는 그 앱의
        // 첫 창이라 딸릴 곳이 없다. XWayland(WSLg)에서는 그런 창이 화면에 나타나지 않는다
        // (서버 안에는 멀쩡히 있어 `XGetImage` 로 되읽으면 다 들어 있지만, 사용자 화면에는 아무것도 뜨지 않는다).
        //
        // 표준 방법은 **창 종류를 스플래시라고 알려 주는 것**이다(EWMH `_NET_WM_WINDOW_TYPE_SPLASH`).
        // 그러면 창 관리자가 장식 없이, 가운데에, 위로 띄운다. 우리가 하려던 바로 그 일을
        // 창 관리자와 **싸우지 않고** 얻는다.
        XSetWindowAttributes attrs{};
        attrs.background_pixel = kSplashBackgroundColor;

        Window win = XCreateWindow(
            pDisplay, root,
            posX, posY, _width, _height,
            0, CopyFromParent, InputOutput, CopyFromParent,
            CWBackPixel, &attrs );

        XStoreName( pDisplay, win, title.c_str() );

        const Atom windowTypeAtom = XInternAtom( pDisplay, "_NET_WM_WINDOW_TYPE", 0 );
        const Atom splashTypeAtom = XInternAtom( pDisplay, "_NET_WM_WINDOW_TYPE_SPLASH", 0 );
        if ( windowTypeAtom != 0 && splashTypeAtom != 0 ) // Xlib 의 `None` 매크로는 이 저장소가 해제한다.
        {
            XChangeProperty( pDisplay, win, windowTypeAtom, XA_ATOM, 32, PropModeReplace,
                             reinterpret_cast<const uint8*>( &splashTypeAtom ), 1 );
        }

        // **`PPosition` 이 아니라 `USPosition` 이다.** `PPosition` 은 "프로그램이 정한 위치" 라
        // 창 관리자가 자기 배치 규칙으로 덮어써도 되는 힌트라, 가운데를 요청해도 좌상단에 놓여
        // 메인 창과 겹칠 수 있다. `USPosition` 은 "사용자가 정한 위치" 라
        // 창 관리자가 존중해야 한다. 매핑 뒤에 한 번 더 옮겨 확실히 한다.
        XSizeHints sizeHints{};
        sizeHints.flags      = USPosition | USSize | PMinSize | PMaxSize;
        sizeHints.x          = posX;
        sizeHints.y          = posY;
        sizeHints.width      = static_cast<int32>( _width );
        sizeHints.height     = static_cast<int32>( _height );
        sizeHints.min_width  = static_cast<int32>( _width );
        sizeHints.min_height = static_cast<int32>( _height );
        sizeHints.max_width  = static_cast<int32>( _width );
        sizeHints.max_height = static_cast<int32>( _height );
        XSetWMNormalHints( pDisplay, win, &sizeHints );

        XSelectInput( pDisplay, win, ExposureMask | StructureNotifyMask );
        XMapRaised( pDisplay, win );

        // 매핑하면서 창 관리자가 다시 놓을 수 있으므로 그 뒤에 한 번 더 옮긴다.
        XMoveWindow( pDisplay, win, posX, posY );

        // **`XFlush` 가 아니라 `XSync` 다.** 매핑은 요청일 뿐이라, 서버가 처리하기 전에 그리면 그
        // 그리기는 버려진다. 한 번 왕복해 두면 이후의 그리기가 확실히 창에 닿는다.
        XSync( pDisplay, 0 ); // discard=0. Xlib 의 `False` 매크로는 이 저장소가 해제한다.
        splashThread.markReady();

        // 이벤트는 비우기만 한다 — 노출 · 가림은 배경 픽스맵으로 서버가 다시 칠한다(`drawStatus`). 그리기는 기동 스레드가 상태를 적을 때다.
        uint64 pixmap = 0;
        for ( ;; )
        {
            while ( XPending( pDisplay ) > 0 )
            {
                XEvent event{};
                XNextEvent( pDisplay, &event );
            }
            XFlush( pDisplay );
            LinuxSplashThread::PaintState state;
            const uint8                   wakeMask = splashThread.waitForEvents( ConnectionNumber( pDisplay ), state );
            if ( ( wakeMask & ( static_cast<uint8>( LinuxSplashWakeReason::Quit ) | static_cast<uint8>( LinuxSplashWakeReason::ConnectionLost ) ) ) != 0 )
                break;
            if ( ( wakeMask & static_cast<uint8>( LinuxSplashWakeReason::Redraw ) ) != 0 )
                drawStatus( pDisplay, static_cast<uint64>( win ), pixmap, state );
        }

        if ( pixmap != 0 )
            XFreePixmap( pDisplay, static_cast<Pixmap>( pixmap ) );
        XUnmapWindow( pDisplay, win );
        XDestroyWindow( pDisplay, win );
        XCloseDisplay( pDisplay );
    }

    void X11SplashWindow::drawStatus( void* pX11Display, uint64 x11Window, uint64& inoutX11Pixmap, const LinuxSplashThread::PaintState& state ) const
    {
        Display*    pDisplay = static_cast<Display*>( pX11Display );
        const auto  win      = static_cast<Window>( x11Window );
        const int32 screen   = DefaultScreen( pDisplay );
        GC          gc       = DefaultGC( pDisplay, screen );

        // **창에 직접 그리지 않고 픽스맵에 그려 그것을 창 배경으로 설정한다.**
        //
        // X11 은 창이 노출될 때마다(`Expose`) **배경색으로 지우고** 다시 그리라고 알린다. 창에 직접 그리면 그 알림마다 다시 그려야 하는데,
        // 배경 픽스맵으로 설정해 두면 **다시 그리는 일을 서버가 한다.** 노출, 가림, 이동을 스레드가 그리지 않고 견딘다.
        // Win32 가 `WM_PAINT` 로 하는 일을 여기서는 서버에게 맡기는 셈이다.
        Pixmap pixmap = static_cast<Pixmap>( inoutX11Pixmap );
        if ( pixmap == 0 )
        {
            pixmap         = XCreatePixmap( pDisplay, win, _width, _height, static_cast<uint32>( DefaultDepth( pDisplay, screen ) ) );
            inoutX11Pixmap = static_cast<uint64>( pixmap );
        }
        if ( pixmap == 0 )
            return;

        XSetForeground( pDisplay, gc, kSplashBackgroundColor );
        XFillRectangle( pDisplay, pixmap, gc, 0, 0, _width, _height );

        // 창 크기로 줄여 둔 것을 찍는다. `XPutImage` 는 늘리거나 줄이지 못하므로 원본을 그대로
        // 넘기면 창보다 큰 이미지는 좌상단만 보인다(1376×768 원본, 480×280 창). 픽셀은 시작 전에 정해져 바뀌지 않는다.
        if ( _listScaledPixel.empty() == false )
        {
            XImage* pImage = XCreateImage(
                pDisplay, DefaultVisual( pDisplay, screen ),
                static_cast<uint32>( DefaultDepth( pDisplay, screen ) ), ZPixmap, 0,
                const_cast<utf8*>( reinterpret_cast<const utf8*>( _listScaledPixel.data() ) ),
                _width, _height, 32, 0 );
            if ( pImage != nullptr )
            {
                XPutImage( pDisplay, pixmap, gc, pImage, 0, 0, 0, 0, _width, _height );
                pImage->data = nullptr;
                XDestroyImage( pImage );
            }
        }

        XSetForeground( pDisplay, gc, kSplashTextColor );
        XDrawString( pDisplay, pixmap, gc, 32, static_cast<int32>( _height ) - 50, state._status.c_str(), static_cast<int32>( state._status.length() ) );

        const int32   totalBarWidth   = static_cast<int32>( _width ) - 64;
        const float32 clampedProgress = ( state._progress < 0.0f ) ? 0.0f : ( ( state._progress > 1.0f ) ? 1.0f : state._progress );
        const int32   fillWidth       = static_cast<int32>( static_cast<float32>( totalBarWidth ) * clampedProgress );

        XSetForeground( pDisplay, gc, kSplashBarTrackColor );
        XFillRectangle( pDisplay, pixmap, gc, 32, static_cast<int32>( _height ) - 30, static_cast<uint32>( totalBarWidth ), 4 );
        if ( fillWidth > 0 )
        {
            XSetForeground( pDisplay, gc, kSplashBarFillColor );
            XFillRectangle( pDisplay, pixmap, gc, 32, static_cast<int32>( _height ) - 30, static_cast<uint32>( fillWidth ), 4 );
        }

        // 배경으로 설정하고 창을 지우면 서버가 그 픽스맵으로 칠한다. 이후의 노출도 서버가 알아서 처리한다.
        XSetWindowBackgroundPixmap( pDisplay, win, pixmap );
        XClearWindow( pDisplay, win );
        XFlush( pDisplay );
    }

    void X11SplashWindow::updateStatus( const utf8* pStatus, float32 progress )
    {
        if ( _bOpen == SW_FALSE )
            return;

        if ( StringUtil::isNullOrEmpty( pStatus ) == false )
            _status = pStatus;
        if ( progress >= 0.0f )
            _progress = ( progress > 1.0f ) ? 1.0f : progress;

        LinuxSplashThread::PaintState state;
        state._status   = _status;
        state._progress = _progress;
        _splashThread.requestRedraw( state ); // 그리기는 스플래시 스레드가 한다
    }

    void X11SplashWindow::setProgress( float32 progress )
    {
        updateStatus( nullptr, progress );
    }

    void X11SplashWindow::dismiss()
    {
        _splashThread.stop(); // 스레드가 픽스맵 · 창 · 연결을 놓고 끝난다
        _bOpen = SW_FALSE;
    }
} // namespace sw
#endif
