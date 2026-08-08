/**
 * @file X11Window.cpp
 * @brief X11 윈도우 구현
 */
#include "X11Window.h"
#include "NativeWindowEvent.h"
#include "Core/Utility/String/StringUtil.h"

namespace sw
{
	X11Window::X11Window() = default;
	X11Window::~X11Window()
	{
		destroy();
	}

#if defined( SW_PLATFORM_LINUX )
	bool X11Window::create( const utf8* title, uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;
		_title	= StringUtil::isNullOrEmpty( title ) ? L"" : StringUtil::utf8ToUtf16( title );

		Display* display = XOpenDisplay( nullptr );
		if ( display == nullptr )
		{
			SW_LOG_ERROR( "[X11Window] Failed to open X11 Display!" );
			return false;
		}

		int			  screen = DefaultScreen( display );
		Window		  root	 = RootWindow( display, screen );
		unsigned long black	 = BlackPixel( display, screen );
		unsigned long white	 = WhitePixel( display, screen );

		Window win = XCreateSimpleWindow(
			display, root,
			100, 100, width, height,
			1, black, white );

		XStoreName( display, win, title != nullptr ? title : "" );

		Atom wmDeleteMessage = XInternAtom( display, "WM_DELETE_WINDOW", False );
		XSetWMProtocols( display, win, &wmDeleteMessage, 1 );

		XSelectInput( display, win,
					  ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask |
						  ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask );
		XMapWindow( display, win );
		XFlush( display );

		_x11Display	  = display;
		_x11Window	  = static_cast<uint64>( win );
		_x11WmDelete  = static_cast<uint64>( wmDeleteMessage );
		_bShouldClose = false;

		SW_LOG_INFO( "[X11Window] Native X11 Window created successfully! (%#x%#)", width, height );
		return true;
	}

	void X11Window::destroy()
	{
		if ( _x11Display != nullptr && _x11Window != 0 )
		{
			Display* display = static_cast<Display*>( _x11Display );
			Window	 win	 = static_cast<Window>( _x11Window );
			XUnmapWindow( display, win );
			XDestroyWindow( display, win );
			XCloseDisplay( display );
			_x11Display = nullptr;
			_x11Window	= 0;
		}
	}

	bool X11Window::processMessages()
	{
		if ( _x11Display == nullptr )
			return _bShouldClose == false;

		Display* display = static_cast<Display*>( _x11Display );
		while ( XPending( display ) > 0 )
		{
			XEvent event;
			XNextEvent( display, &event );

			if ( _customHandler.isBound() == true )
			{
				NativeWindowEvent ev{};
				ev.nativeWindow = reinterpret_cast<void*>( static_cast<uintptr_t>( _x11Window ) );
				ev.message		= NativeWindowEvent::kMessageX11;
				ev.wParam		= 0;
				ev.lParam		= reinterpret_cast<intptr_t>( &event );
				if ( _customHandler( ev ) == true )
					continue;
			}

			if ( event.type == ClientMessage )
			{
				// Only the main window's close request exits the app.
				// ImGui multi-viewport windows share this Display and also send WM_DELETE_WINDOW.
				if ( event.xclient.window == static_cast<Window>( _x11Window )
					 && static_cast<Atom>( event.xclient.data.l[0] ) == static_cast<Atom>( _x11WmDelete ) )
				{
					_bShouldClose = true;
					return false;
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
					_width	= newW;
					_height = newH;
					if ( _onResize.isBound() == true )
						_onResize( _width, _height );
				}
			}
		}

		return _bShouldClose == false;
	}
#else
	bool X11Window::create( const utf8*, uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;
		return true;
	}

	void X11Window::destroy() {}

	bool X11Window::processMessages()
	{
		return _bShouldClose == false;
	}
#endif
} // namespace sw
