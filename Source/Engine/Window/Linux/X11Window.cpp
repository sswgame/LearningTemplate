#include "pch.h"

#include "Engine/Window/Linux/X11Window.h"

#include "Engine/Window/NativeWindowEvent.h"

namespace sw
{
    SW_LOG_CALLER( "X11Window" );

    X11Window::X11Window()
        : _pX11Display{ nullptr }
        , _x11Window{ 0 }
        , _x11WmDelete{ 0 }
        , _restoreX{ 100 }
        , _restoreY{ 100 }
        , _bRecreating{ SW_FALSE }
        , _reservedX11{ 0 }
        , _padding{ 0 }
    {
    }

    X11Window::~X11Window()
    {
        destroy();
    }

#if defined( SW_PLATFORM_LINUX )
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

        XStoreName( pDisplay, win, pTitle != nullptr ? pTitle : "" );

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

        SW_LOG_INFO( "Native X11 Window created successfully! (%#x%#)", width, height );
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

    void X11Window::showWindow( bool bShow )
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
        {
            return wa.map_state == IsViewable;
        }
        return false;
    }

    bool X11Window::recreate()
    {
        if ( _title.empty() )
            return false;

        const bool bWasVisible = isVisible();

        if ( _pX11Display != nullptr && _x11Window != 0 )
        {
            Display*          pDisplay = static_cast<Display*>( _pX11Display );
            XWindowAttributes wa{};
            if ( XGetWindowAttributes( pDisplay, _x11Window, &wa ) != 0 )
            {
                _restoreX = wa.x;
                _restoreY = wa.y;
            }
        }

        const uint32 width  = _width;
        const uint32 height = _height;
        _bRecreating        = SW_TRUE;
        destroy();
        _bShouldClose      = SW_FALSE;
        const string title = StringUtil::utf16ToUtf8( _title.c_str() );
        const bool   ok    = initializeWindow( title.c_str(), width, height );
        if ( ok && bWasVisible )
            showWindow( true );
        _bRecreating = SW_FALSE;

        _restoreX = 100;
        _restoreY = 100;
        return ok;
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
                NativeWindowEvent ev{};
                ev._pNativeWindow = reinterpret_cast<void*>( static_cast<uintptr_t>( _x11Window ) );
                ev._message       = NativeWindowEvent::kMessageX11;
                ev._wParam        = 0;
                ev._lParam        = reinterpret_cast<intptr_t>( &event );
                if ( _customHandler( ev ) )
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

    bool X11Window::recreate()
    {
        return false;
    }

    bool X11Window::processMessages()
    {
        return _bShouldClose == SW_FALSE;
    }

    void X11Window::showWindow( bool bShow )
    {
        (void)bShow;
    }

    bool X11Window::isVisible() const
    {
        return false;
    }
#endif
} // namespace sw
