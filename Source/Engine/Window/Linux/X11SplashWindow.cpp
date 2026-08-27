#include "pch.h"

#include "Engine/Window/Linux/X11SplashWindow.h"

#if defined( SW_PLATFORM_LINUX )
	#include <X11/Xlib.h>
	#include <X11/Xutil.h>
	#include <X11/Xatom.h>
	#include "Core/Common/X11MacroUndef.h"
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
		_title	= ( pTitle != nullptr && pTitle[0] != '\0' ) ? string{ pTitle } : "SW Engine";
		_status = ( pInitialStatus != nullptr && pInitialStatus[0] != '\0' ) ? string{ pInitialStatus } : "Initializing...";
		_width	= width;
		_height = height;

		Display* pDisplay = XOpenDisplay( nullptr );
		if ( pDisplay == nullptr )
		{
			_bOpen = false;
			return false;
		}

		const int32 screen	= DefaultScreen( pDisplay );
		Window		root	= RootWindow( pDisplay, screen );
		const int32 screenW = DisplayWidth( pDisplay, screen );
		const int32 screenH = DisplayHeight( pDisplay, screen );
		const int32 posX	= ( screenW - static_cast<int32>( _width ) ) / 2;
		const int32 posY	= ( screenH - static_cast<int32>( _height ) ) / 2;

		XSetWindowAttributes attrs{};
		attrs.override_redirect = True;
		attrs.background_pixel	= 0x181C24;

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
		_x11Window	 = static_cast<uint64>( win );
		_bOpen		 = true;

		updateStatus( _status.c_str() );
		return true;
	}

	void X11SplashWindow::updateStatus( const utf8* pStatus )
	{
		if ( _bOpen == false )
			return;

		_status = ( pStatus != nullptr ) ? string{ pStatus } : "";

		Display* pDisplay = static_cast<Display*>( _pX11Display );
		Window	 win	  = static_cast<Window>( _x11Window );
		if ( pDisplay != nullptr && win != 0 )
		{
			const int32 screen = DefaultScreen( pDisplay );
			GC			gc	   = DefaultGC( pDisplay, screen );

			XSetForeground( pDisplay, gc, 0x181C24 );
			XFillRectangle( pDisplay, win, gc, 0, 0, _width, _height );
			XSetForeground( pDisplay, gc, 0x374152 );
			XDrawRectangle( pDisplay, win, gc, 0, 0, _width - 1, _height - 1 );

			XSetForeground( pDisplay, gc, 0x4B8CF5 );
			XFillRectangle( pDisplay, win, gc, 1, 1, _width - 2, 3 );

			XSetForeground( pDisplay, gc, 0xF0F5FF );
			XDrawString( pDisplay, win, gc, 32, 50, _title.c_str(), static_cast<int32>( _title.length() ) );
			XSetForeground( pDisplay, gc, 0x8296B4 );
			XDrawString( pDisplay, win, gc, 32, 85, "Game & Editor Engine Template (LiveReload Enabled)", 50 );

			XSetForeground( pDisplay, gc, 0xB4CDEB );
			XDrawString( pDisplay, win, gc, 32, static_cast<int32>( _height ) - 50, _status.c_str(), static_cast<int32>( _status.length() ) );

			XSetForeground( pDisplay, gc, 0x262C3A );
			XFillRectangle( pDisplay, win, gc, 32, static_cast<int32>( _height ) - 38, _width - 64, 6 );
			XSetForeground( pDisplay, gc, 0x3C82F0 );
			XFillRectangle( pDisplay, win, gc, 32, static_cast<int32>( _height ) - 38, _width - 110, 6 );

			XFlush( pDisplay );
		}
	}

	void X11SplashWindow::dismiss()
	{
		if ( _bOpen == false )
			return;

		Display* pDisplay = static_cast<Display*>( _pX11Display );
		Window	 win	  = static_cast<Window>( _x11Window );
		if ( pDisplay != nullptr && win != 0 )
		{
			XUnmapWindow( pDisplay, win );
			XDestroyWindow( pDisplay, win );
			XCloseDisplay( pDisplay );
			_pX11Display = nullptr;
			_x11Window	 = 0;
		}

		_bOpen = false;
	}

#else

	bool X11SplashWindow::initialize( const utf8*, const utf8*, uint32, uint32 ) { return false; }
	void X11SplashWindow::updateStatus( const utf8* ) {}
	void X11SplashWindow::dismiss() {}

#endif
} // namespace sw
