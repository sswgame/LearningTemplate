#include "pch.h"

#include "Engine/Window/Linux/X11Window.h"
#include "Engine/Window/NativeWindowEvent.h"

namespace sw
{
	X11Window::X11Window()
		: _pX11Display{ nullptr }
		, _x11Window{ 0 }
		, _x11WmDelete{ 0 }
		, _bRecreating{ false }
		, _restoreX{ 100 }
		, _restoreY{ 100 }
	{
	}

	X11Window::~X11Window()
	{
		destroy();
	}

#if defined( SW_PLATFORM_LINUX )
	bool X11Window::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;
		_title	= StringUtil::isNullOrEmpty( pTitle ) ? L"" : StringUtil::utf8ToUtf16( pTitle );

		Display* pDisplay = XOpenDisplay( nullptr );
		if ( pDisplay == nullptr )
		{
			SW_LOG_ERROR( "[X11Window] Failed to open X11 Display!" );
			return false;
		}

		int32  screen = DefaultScreen( pDisplay );
		Window root	  = RootWindow( pDisplay, screen );
		uint64 black  = BlackPixel( pDisplay, screen );
		uint64 white  = WhitePixel( pDisplay, screen );

		Window win = XCreateSimpleWindow(
			pDisplay, root,
			_restoreX, _restoreY, width, height,
			1, black, white );

		XStoreName( pDisplay, win, pTitle != nullptr ? pTitle : "" );

		Atom wmDeleteMessage = XInternAtom( pDisplay, "WM_DELETE_WINDOW", False );
		XSetWMProtocols( pDisplay, win, &wmDeleteMessage, 1 );

		XSelectInput( pDisplay, win,
					  ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask |
						  ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask );
		XMapWindow( pDisplay, win );
		XFlush( pDisplay );

		_pX11Display  = pDisplay;
		_x11Window	  = static_cast<uint64>( win );
		_x11WmDelete  = static_cast<uint64>( wmDeleteMessage );
		_bShouldClose = false;

		SW_LOG_INFO( "[X11Window] Native X11 Window created successfully! (%#x%#)", width, height );
		return true;
	}

	void X11Window::destroy()
	{
		if ( _pX11Display != nullptr && _x11Window != 0 )
		{
			Display* pDisplay = static_cast<Display*>( _pX11Display );
			Window	 win	  = static_cast<Window>( _x11Window );
			XUnmapWindow( pDisplay, win );
			XDestroyWindow( pDisplay, win );
			XCloseDisplay( pDisplay );
			_pX11Display = nullptr;
			_x11Window	 = 0;
		}
	}

	bool X11Window::recreate()
	{
		if ( _title.empty() )
			return false;

		if ( _pX11Display != nullptr && _x11Window != 0 )
		{
			Display*		  pDisplay = static_cast<Display*>( _pX11Display );
			Window			  win	   = static_cast<Window>( _x11Window );
			XWindowAttributes attrs{};
			if ( XGetWindowAttributes( pDisplay, win, &attrs ) != 0 )
			{
				_restoreX = attrs.x;
				_restoreY = attrs.y;
			}
		}

		const uint32 width	= _width;
		const uint32 height = _height;
		_bRecreating		= true;
		destroy();
		_bRecreating	   = false;
		_bShouldClose	   = false;
		const string title = StringUtil::utf16ToUtf8( _title.c_str() );
		const bool	 ok	   = initializeWindow( title.c_str(), width, height );
		_restoreX		   = 100;
		_restoreY		   = 100;
		return ok;
	}

	bool X11Window::processMessages()
	{
		if ( _pX11Display == nullptr )
			return _bShouldClose == false;

		Display* pDisplay = static_cast<Display*>( _pX11Display );
		while ( XPending( pDisplay ) > 0 )
		{
			XEvent event;
			XNextEvent( pDisplay, &event );

			if ( _customHandler.isBound() )
			{
				NativeWindowEvent ev{};
				ev._pNativeWindow = reinterpret_cast<void*>( static_cast<uintptr_t>( _x11Window ) );
				ev._message		  = NativeWindowEvent::kMessageX11;
				ev._wParam		  = 0;
				ev._lParam		  = reinterpret_cast<intptr_t>( &event );
				if ( _customHandler( ev ) )
					continue;
			}

			if ( event.type == ClientMessage )
			{
				if ( event.xclient.window == static_cast<Window>( _x11Window ) &&
					 static_cast<Atom>( event.xclient.data.l[0] ) == static_cast<Atom>( _x11WmDelete ) )
				{
					if ( _bRecreating == false )
					{
						_bShouldClose = true;
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
					_width	= newW;
					_height = newH;
					if ( _onResize.isBound() )
						_onResize( _width, _height );
				}
			}
		}

		return _bShouldClose == false;
	}
#else
	bool X11Window::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
	{
		std::ignore = pTitle;
		_width		= width;
		_height		= height;
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
		return _bShouldClose == false;
	}
#endif
} // namespace sw
