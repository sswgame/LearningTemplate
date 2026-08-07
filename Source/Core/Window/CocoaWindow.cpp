/**
 * @file CocoaWindow.cpp
 * @brief Cocoa 윈도우 구현
 */
#include "CocoaWindow.h"

namespace sw
{
	CocoaWindow::CocoaWindow() = default;
	CocoaWindow::~CocoaWindow()
	{
		destroy();
	}

#if defined( SW_PLATFORM_MACOS )
	#include <objc/message.h>
	#include <objc/runtime.h>

	struct SW_CGPoint
	{
		double x;
		double y;
	};
	struct SW_CGSize
	{
		double width;
		double height;
	};
	struct SW_CGRect
	{
		SW_CGPoint origin;
		SW_CGSize  size;
	};

	bool CocoaWindow::create( const utf16* title, uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;

		id appClass = (id)objc_getClass( "NSApplication" );
		if ( appClass == nullptr )
			return false;

		SEL sharedAppSel = sel_registerName( "sharedApplication" );
		id	app			 = ( (id ( * )( id, SEL ))objc_msgSend )( appClass, sharedAppSel );
		if ( app == nullptr )
			return false;

		SEL setActPolicySel = sel_registerName( "setActivationPolicy:" );
		( (void ( * )( id, SEL, int64 ))objc_msgSend )( app, setActPolicySel, 0 );

		id windowClass = (id)objc_getClass( "NSWindow" );
		id windowAlloc = ( (id ( * )( id, SEL ))objc_msgSend )( windowClass, sel_registerName( "alloc" ) );

		SW_CGRect frameRect = {
			{					   100.0,						 100.0},
			{static_cast<double>( width ), static_cast<double>( height )}
		};
		uint64 styleMask = 1 | 2 | 4 | 8;
		uint64 backing	 = 2;

		SEL initSel	  = sel_registerName( "initWithContentRect:styleMask:backing:defer:" );
		id	windowObj = ( (id ( * )( id, SEL, SW_CGRect, uint64, uint64, bool ))objc_msgSend )(
			 windowAlloc, initSel, frameRect, styleMask, backing, false );

		if ( windowObj == nullptr )
			return false;

		std::wstring wTitle( title );
		std::string	 utf8Title( wTitle.begin(), wTitle.end() );

		id	strClass	   = (id)objc_getClass( "NSString" );
		SEL strWithUtf8Sel = sel_registerName( "stringWithUTF8String:" );
		id	nsTitleStr	   = ( (id ( * )( id, SEL, const char* ))objc_msgSend )( strClass, strWithUtf8Sel, utf8Title.c_str() );

		SEL setTitleSel = sel_registerName( "setTitle:" );
		( (void ( * )( id, SEL, id ))objc_msgSend )( windowObj, setTitleSel, nsTitleStr );

		SEL makeKeySel = sel_registerName( "makeKeyAndOrderFront:" );
		( (void ( * )( id, SEL, id ))objc_msgSend )( windowObj, makeKeySel, nullptr );

		SEL activateSel = sel_registerName( "activateIgnoringOtherApps:" );
		( (void ( * )( id, SEL, bool ))objc_msgSend )( app, activateSel, true );

		id contentView	   = ( (id ( * )( id, SEL ))objc_msgSend )( windowObj, sel_registerName( "contentView" ) );
		id metalLayerClass = (id)objc_getClass( "CAMetalLayer" );
		id metalLayer	   = ( (id ( * )( id, SEL ))objc_msgSend )( metalLayerClass, sel_registerName( "layer" ) );
		( (void ( * )( id, SEL, id ))objc_msgSend )( contentView, sel_registerName( "setLayer:" ), metalLayer );
		( (void ( * )( id, SEL, bool ))objc_msgSend )( contentView, sel_registerName( "setWantsLayer:" ), true );

		_cocoaWindow	 = windowObj;
		_cocoaApp		 = app;
		_cocoaMetalLayer = metalLayer;
		_bShouldClose	 = false;

		SW_LOG_INFO( "[CocoaWindow macOS] Native Cocoa Window created successfully! (%#x%#)", width, height );
		return true;
	}

	void CocoaWindow::destroy()
	{
		if ( _cocoaWindow != nullptr )
		{
			SEL closeSel = sel_registerName( "close" );
			( (void ( * )( id, SEL ))objc_msgSend )( (id)_cocoaWindow, closeSel );
			_cocoaWindow = nullptr;
		}
		_cocoaApp = nullptr;
	}

	bool CocoaWindow::processMessages()
	{
		if ( _bShouldClose || _cocoaApp == nullptr )
			return false;

		id poolClass = (id)objc_getClass( "NSAutoreleasePool" );
		id poolAlloc = ( (id ( * )( id, SEL ))objc_msgSend )( poolClass, sel_registerName( "alloc" ) );
		id pool		 = ( (id ( * )( id, SEL ))objc_msgSend )( poolAlloc, sel_registerName( "init" ) );

		id dateClass   = (id)objc_getClass( "NSDate" );
		id distantPast = ( (id ( * )( id, SEL ))objc_msgSend )( dateClass, sel_registerName( "distantPast" ) );

		id strClass = (id)objc_getClass( "NSString" );
		id modeStr	= ( (id ( * )( id, SEL, const char* ))objc_msgSend )( strClass, sel_registerName( "stringWithUTF8String:" ), "kCFRunLoopDefaultMode" );

		SEL nextEventSel = sel_registerName( "nextEventMatchingMask:untilDate:inMode:dequeue:" );
		id	event		 = ( (id ( * )( id, SEL, uint64, id, id, bool ))objc_msgSend )(
			(id)_cocoaApp, nextEventSel, ~0ULL, distantPast, modeStr, true );

		if ( event != nullptr )
		{
			SEL sendEventSel = sel_registerName( "sendEvent:" );
			( (void ( * )( id, SEL, id ))objc_msgSend )( (id)_cocoaApp, sendEventSel, event );

			SEL updateWinSel = sel_registerName( "updateWindows" );
			( (void ( * )( id, SEL ))objc_msgSend )( (id)_cocoaApp, updateWinSel );
		}

		SEL drainSel = sel_registerName( "drain" );
		( (void ( * )( id, SEL ))objc_msgSend )( pool, drainSel );

		return !_bShouldClose;
	}
#else
	bool CocoaWindow::create( const utf16*, uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;
		return true;
	}

	void CocoaWindow::destroy() {}

	bool CocoaWindow::processMessages()
	{
		return !_bShouldClose;
	}
#endif
} // namespace sw
