#include "pch.h"

#include "Engine/Window/Linux/X11Window.h"

#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    #include "Core/Common/X11Headers.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "X11Window" );

    namespace
    {
        /** @brief 복원 위치의 "알아서" 값입니다. X11 에는 `CW_USEDEFAULT` 같은 약속이 없어 좌표를 하나 정해 둡니다. */
        constexpr int32 kDefaultRestoreX = 100;
        /** @brief 위와 같습니다(세로 좌표). */
        constexpr int32 kDefaultRestoreY = 100;
    } // namespace

    X11Window::X11Window()
        : _pX11Display{ nullptr }
        , _x11Window{ 0 }
        , _x11WmDelete{ 0 }
        , _reservedX11{ 0 }
        , _padding{ 0 }
    {
        // 복원 위치는 기반(`IWindow`)이 들고 절차도 기반이 맡는다. 플랫폼은 "알아서" 값만 정한다.
        clearRestorePosition();
    }

    X11Window::~X11Window()
    {
        X11Window::destroy();
    }

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    bool X11Window::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        _width  = width;
        _height = height;
        _title  = StringUtil::isNullOrEmpty( pTitle ) ? L"" : StringUtil::utf8ToUtf16( pTitle );

        Display* pDisplay = XOpenDisplay( nullptr );
        if ( pDisplay == nullptr )
        {
            SW_LOG_ERROR( "Failed to open X11 Display!" );
            return false;
        }

        int32  screen = DefaultScreen( pDisplay );
        Window root   = RootWindow( pDisplay, screen );
        uint64 black  = BlackPixel( pDisplay, screen );
        uint64 white  = WhitePixel( pDisplay, screen );

        Window win = XCreateSimpleWindow(
            pDisplay, root,
            _restoreX, _restoreY, width, height,
            1, black, white );

        // 제목은 UTF-8 이다. XStoreName 은 WM_NAME 을 Latin-1(STRING)으로 적어 비-ASCII 글자가 깨진다 — 창 관리자는 _NET_WM_NAME(UTF8_STRING)을
        // 먼저 읽으므로 그것을 같이 적는다(XStoreName 은 _NET_WM_NAME 을 모르는 옛 창 관리자 몫).
        const utf8* pTitleText = pTitle != nullptr ? pTitle : "";
        XStoreName( pDisplay, win, pTitleText );
        const Atom netWmName  = XInternAtom( pDisplay, "_NET_WM_NAME", 0 );
        const Atom utf8String = XInternAtom( pDisplay, "UTF8_STRING", 0 );
        XChangeProperty( pDisplay, win, netWmName, utf8String, 8, PropModeReplace, reinterpret_cast<const uint8*>( pTitleText ),
                         static_cast<int32>( StringUtil::strlen( pTitleText ) ) );

        Atom wmDeleteMessage = XInternAtom( pDisplay, "WM_DELETE_WINDOW", 0 );
        XSetWMProtocols( pDisplay, win, &wmDeleteMessage, 1 );

        XSelectInput( pDisplay, win,
                      ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask |
                          ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask );
        XFlush( pDisplay );

        _pX11Display  = pDisplay;
        _x11Window    = win;
        _x11WmDelete  = wmDeleteMessage;
        _bShouldClose = SW_FALSE;
        applyMinimumClientSize(); // 다시 만든 창(recreate)도 같은 바닥을 갖는다

        SW_LOG_INFO( "Native X11 Window created successfully! (%#×%#)", width, height );
        return true;
    }

    void X11Window::destroy()
    {
        if ( _pX11Display != nullptr )
        {
            Display* pDisplay = static_cast<Display*>( _pX11Display );
            if ( _x11Window != 0 )
            {
                XDestroyWindow( pDisplay, _x11Window );
                _x11Window = 0;
            }
            XCloseDisplay( pDisplay );
            _pX11Display = nullptr;
        }
    }

    void X11Window::applyWindowVisibility( bool bShow )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return;

        Display* pDisplay = static_cast<Display*>( _pX11Display );
        if ( bShow )
        {
            XMapWindow( pDisplay, _x11Window );
            XFlush( pDisplay );
        }
        else
        {
            XUnmapWindow( pDisplay, _x11Window );
            XFlush( pDisplay );
        }
    }

    bool X11Window::isVisible() const
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;

        Display*          pDisplay = static_cast<Display*>( _pX11Display );
        XWindowAttributes wa{};
        if ( XGetWindowAttributes( pDisplay, _x11Window, &wa ) != 0 )
            return wa.map_state == IsViewable;
        return false;
    }

    bool X11Window::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;

        Display*   pDisplay    = static_cast<Display*>( _pX11Display );
        const bool bFullscreen = mode == WindowDisplayMode::BorderlessFullscreen;
        _displayMode           = mode;

        // 창 관리자에게 상태 변경을 청한다(EWMH). 1 = _NET_WM_STATE_ADD, 0 = _NET_WM_STATE_REMOVE, l[3] = 1 은 "일반 앱" 출처다.
        XEvent event{};
        event.xclient.type         = ClientMessage;
        event.xclient.window       = static_cast<Window>( _x11Window );
        event.xclient.message_type = XInternAtom( pDisplay, "_NET_WM_STATE", 0 );
        event.xclient.format       = 32;
        event.xclient.data.l[0]    = bFullscreen ? 1 : 0;
        event.xclient.data.l[1]    = static_cast<int64>( XInternAtom( pDisplay, "_NET_WM_STATE_FULLSCREEN", 0 ) );
        event.xclient.data.l[2]    = 0;
        event.xclient.data.l[3]    = 1;
        XSendEvent( pDisplay, DefaultRootWindow( pDisplay ), 0, SubstructureRedirectMask | SubstructureNotifyMask, &event );
        if ( bFullscreen == false )
            XResizeWindow( pDisplay, static_cast<Window>( _x11Window ), width, height );
        XFlush( pDisplay );
        return true;
    }

    void X11Window::captureRestorePosition()
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return;

        Display*          pDisplay = static_cast<Display*>( _pX11Display );
        XWindowAttributes wa{};
        if ( XGetWindowAttributes( pDisplay, _x11Window, &wa ) != 0 )
        {
            _restoreX = wa.x;
            _restoreY = wa.y;
        }
    }

    void X11Window::clearRestorePosition()
    {
        // X11 에는 `CW_USEDEFAULT` 같은 값이 없다. 창 관리자에게 맡기는 관례적 시작점을 쓴다.
        _restoreX = kDefaultRestoreX;
        _restoreY = kDefaultRestoreY;
    }

    void X11Window::applyMinimumClientSize()
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return;

        Display*   pDisplay = static_cast<Display*>( _pX11Display );
        XSizeHints sizeHints{};
        sizeHints.flags      = PMinSize;
        sizeHints.min_width  = static_cast<int32>( _minClientWidth );
        sizeHints.min_height = static_cast<int32>( _minClientHeight );
        XSetWMNormalHints( pDisplay, static_cast<Window>( _x11Window ), &sizeHints );
        XFlush( pDisplay );
    }

    float32 X11Window::getContentScale() const
    {
        if ( _pX11Display == nullptr )
            return 1.0f;
        // 데스크톱의 배율은 리소스 문자열의 "Xft.dpi:\t144" 한 줄이다(xrdb · GNOME · KDE). 없으면 96 DPI 로 본다.
        const utf8* const pResource = XResourceManagerString( static_cast<Display*>( _pX11Display ) );
        if ( pResource == nullptr )
            return 1.0f;
        const utf8* const pKey = std::strstr( pResource, "Xft.dpi:" );
        if ( pKey == nullptr )
            return 1.0f;
        const float32 dpi = static_cast<float32>( std::strtod( pKey + sizeof( "Xft.dpi:" ) - 1, nullptr ) );
        return dpi > 0.0f ? dpi / kReferenceDpi : 1.0f;
    }

    bool X11Window::processMessages()
    {
        if ( _pX11Display == nullptr )
            return _bShouldClose == SW_FALSE;

        Display* pDisplay = static_cast<Display*>( _pX11Display );
        while ( XPending( pDisplay ) > 0 )
        {
            XEvent event;
            XNextEvent( pDisplay, &event );

            if ( _customHandler.isBound() )
            {
                NativeWindowEvent windowEvent{};
                windowEvent._pNativeWindow = reinterpret_cast<void*>( static_cast<uintptr_t>( _x11Window ) );
                windowEvent._message       = NativeWindowEvent::kMessageX11;
                windowEvent._wParam        = 0;
                windowEvent._lParam        = reinterpret_cast<intptr_t>( &event );
                if ( _customHandler( windowEvent ) )
                    continue;
            }

            if ( event.type == ClientMessage )
            {
                if ( event.xclient.window == static_cast<Window>( _x11Window ) &&
                     static_cast<Atom>( event.xclient.data.l[0] ) == static_cast<Atom>( _x11WmDelete ) )
                {
                    if ( _bRecreating == SW_FALSE )
                    {
                        if ( tryBeginClose() == false )
                            continue;
                        return false;
                    }
                }
            }
            else if ( event.type == ConfigureNotify )
            {
                if ( event.xconfigure.window != static_cast<Window>( _x11Window ) )
                    continue;

                uint32 newW = static_cast<uint32>( event.xconfigure.width );
                uint32 newH = static_cast<uint32>( event.xconfigure.height );
                if ( newW != _width || newH != _height )
                {
                    _width  = newW;
                    _height = newH;
                    if ( _onResize.isBound() )
                        _onResize( _width, _height );
                }
            }
        }

        return _bShouldClose == SW_FALSE;
    }
#else
    bool X11Window::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        std::ignore = pTitle;
        _width      = width;
        _height     = height;
        return true;
    }

    void X11Window::destroy()
    {
    }

    bool X11Window::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        (void)width;
        (void)height;
        _displayMode = mode;
        return false;
    }

    void X11Window::captureRestorePosition()
    {
    }

    void X11Window::clearRestorePosition()
    {
        // 이 플랫폼에서는 창을 만들지 않지만, 생성자가 부르므로 값은 채워 둔다.
        _restoreX = kDefaultRestoreX;
        _restoreY = kDefaultRestoreY;
    }

    void X11Window::applyMinimumClientSize()
    {
    }

    bool X11Window::processMessages()
    {
        return _bShouldClose == SW_FALSE;
    }

    void X11Window::applyWindowVisibility( bool bShow )
    {
        (void)bShow;
    }

    bool X11Window::isVisible() const
    {
        return false;
    }

    float32 X11Window::getContentScale() const
    {
        return 1.0f;
    }
#endif
} // namespace sw
