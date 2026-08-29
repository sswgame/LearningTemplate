#include "pch.h"

#include "Engine/Window/Mac/CocoaWindow.h"

SW_LOG_CALLER( "CocoaWindow" );

#if defined( SW_PLATFORM_MACOS )
namespace
{
	sw::CocoaWindow* s_pCloseQueryWindow{ nullptr };

	signed char swCocoaWindowShouldClose( id self, SEL sel, id sender )
	{
		(void)self;
		(void)sel;
		(void)sender;
		if ( s_pCloseQueryWindow != nullptr )
			s_pCloseQueryWindow->tryBeginClose();
		return 0;
	}

	id swMakeCloseDelegate()
	{
		static Class s_cls{ nullptr };
		if ( s_cls == nullptr )
		{
			s_cls = objc_allocateClassPair( objc_getClass( "NSObject" ), "SWCocoaWindowCloseDelegate", 0 );
			if ( s_cls == nullptr )
				return nullptr;
			class_addMethod( s_cls, sel_registerName( "windowShouldClose:" ),
							 reinterpret_cast<IMP>( swCocoaWindowShouldClose ), "c@:@" );
			objc_registerClassPair( s_cls );
		}
		id alloc = ( (id ( * )( id, SEL ))objc_msgSend )( (id)s_cls, sel_registerName( "alloc" ) );
		return ( (id ( * )( id, SEL ))objc_msgSend )( alloc, sel_registerName( "init" ) );
	}
} // namespace
#endif

namespace sw
{
	CocoaWindow::CocoaWindow()
		: _pCocoaWindow{ nullptr }
		, _pCocoaApp{ nullptr }
		, _pCocoaMetalLayer{ nullptr }
		, _pCocoaDelegate{ nullptr }
	{
	}

	CocoaWindow::~CocoaWindow()
	{
		destroy();
	}

#if defined( SW_PLATFORM_MACOS )
	struct SW_CGPoint
	{
		float64 x;
		float64 y;
	};
	struct SW_CGSize
	{
		float64 width;
		float64 height;
	};
	struct SW_CGRect
	{
		SW_CGPoint origin;
		SW_CGSize  size;
	};

	bool CocoaWindow::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;
		_title	= StringUtil::isNullOrEmpty( pTitle ) ? L"" : StringUtil::utf8ToUtf16( pTitle );

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
			{						100.0,						  100.0},
			{static_cast<float64>( width ), static_cast<float64>( height )}
		  };
		uint64 styleMask = 1 | 2 | 4 | 8;
		uint64 backing	 = 2;

		SEL initSel	  = sel_registerName( "initWithContentRect:styleMask:backing:defer:" );
		id	windowObj = ( (id ( * )( id, SEL, SW_CGRect, uint64, uint64, bool ))objc_msgSend )(
			 windowAlloc, initSel, frameRect, styleMask, backing, false );

		if ( windowObj == nullptr )
			return false;

		const utf8* pUtf8Title = pTitle != nullptr ? pTitle : "";

		id	strClass	   = (id)objc_getClass( "NSString" );
		SEL strWithUtf8Sel = sel_registerName( "stringWithUTF8String:" );
		id	nsTitleStr	   = ( (id ( * )( id, SEL, const utf8* ))objc_msgSend )( strClass, strWithUtf8Sel, pUtf8Title );

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

		_pCocoaWindow	  = windowObj;
		_pCocoaApp		  = app;
		_pCocoaMetalLayer = metalLayer;
		_bShouldClose	  = false;
		s_pCloseQueryWindow = this;

		SEL setReleasedSel = sel_registerName( "setReleasedWhenClosed:" );
		( (void ( * )( id, SEL, bool ))objc_msgSend )( windowObj, setReleasedSel, false );

		id closeDelegate = swMakeCloseDelegate();
		if ( closeDelegate != nullptr )
		{
			SEL setDelegateSel = sel_registerName( "setDelegate:" );
			( (void ( * )( id, SEL, id ))objc_msgSend )( windowObj, setDelegateSel, closeDelegate );
			_pCocoaDelegate = closeDelegate;
		}

		SW_LOG_INFO( "Native Cocoa Window created successfully! (%#x%#)", width, height );
		return true;
	}

	void CocoaWindow::destroy()
	{
		if ( _pCocoaWindow != nullptr )
		{
			SEL setDelegateSel = sel_registerName( "setDelegate:" );
			( (void ( * )( id, SEL, id ))objc_msgSend )( (id)_pCocoaWindow, setDelegateSel, nullptr );
			SEL closeSel = sel_registerName( "close" );
			( (void ( * )( id, SEL ))objc_msgSend )( (id)_pCocoaWindow, closeSel );
			_pCocoaWindow = nullptr;
		}
		if ( _pCocoaDelegate != nullptr )
		{
			SEL releaseSel = sel_registerName( "release" );
			( (void ( * )( id, SEL ))objc_msgSend )( (id)_pCocoaDelegate, releaseSel );
			_pCocoaDelegate = nullptr;
		}
		if ( s_pCloseQueryWindow == this )
			s_pCloseQueryWindow = nullptr;
		_pCocoaApp		  = nullptr;
		_pCocoaMetalLayer = nullptr;
	}

	bool CocoaWindow::recreate()
	{
		return false;
	}

	bool CocoaWindow::processMessages()
	{
		if ( _bShouldClose || _pCocoaApp == nullptr )
			return false;

		id poolClass = (id)objc_getClass( "NSAutoreleasePool" );
		id poolAlloc = ( (id ( * )( id, SEL ))objc_msgSend )( poolClass, sel_registerName( "alloc" ) );
		id pool		 = ( (id ( * )( id, SEL ))objc_msgSend )( poolAlloc, sel_registerName( "init" ) );

		id dateClass   = (id)objc_getClass( "NSDate" );
		id distantPast = ( (id ( * )( id, SEL ))objc_msgSend )( dateClass, sel_registerName( "distantPast" ) );

		id strClass = (id)objc_getClass( "NSString" );
		id modeStr	= ( (id ( * )( id, SEL, const utf8* ))objc_msgSend )( strClass, sel_registerName( "stringWithUTF8String:" ), "kCFRunLoopDefaultMode" );

		SEL nextEventSel = sel_registerName( "nextEventMatchingMask:untilDate:inMode:dequeue:" );
		id	event		 = ( (id ( * )( id, SEL, uint64, id, id, bool ))objc_msgSend )(
			(id)_pCocoaApp, nextEventSel, ~0ULL, distantPast, modeStr, true );

		if ( event != nullptr )
		{
			SEL sendEventSel = sel_registerName( "sendEvent:" );
			( (void ( * )( id, SEL, id ))objc_msgSend )( (id)_pCocoaApp, sendEventSel, event );

			SEL updateWinSel = sel_registerName( "updateWindows" );
			( (void ( * )( id, SEL ))objc_msgSend )( (id)_pCocoaApp, updateWinSel );
		}

		SEL drainSel = sel_registerName( "drain" );
		( (void ( * )( id, SEL ))objc_msgSend )( pool, drainSel );

		return _bShouldClose == false;
	}

	void CocoaWindow::showWindow( bool bShow )
	{
		if ( _pCocoaWindow != nullptr )
		{
			if ( bShow )
			{
				SEL makeKeySel = sel_registerName( "makeKeyAndOrderFront:" );
				( (void ( * )( id, SEL, id ))objc_msgSend )( (id)_pCocoaWindow, makeKeySel, nullptr );

				if ( _pCocoaApp != nullptr )
				{
					SEL activateSel = sel_registerName( "activateIgnoringOtherApps:" );
					( (void ( * )( id, SEL, bool ))objc_msgSend )( (id)_pCocoaApp, activateSel, true );
				}
			}
			else
			{
				SEL orderOutSel = sel_registerName( "orderOut:" );
				( (void ( * )( id, SEL, id ))objc_msgSend )( (id)_pCocoaWindow, orderOutSel, nullptr );
			}
		}
	}

	bool CocoaWindow::isVisible() const
	{
		if ( _pCocoaWindow != nullptr )
		{
			SEL isVisibleSel = sel_registerName( "isVisible" );
			return ( (bool ( * )( id, SEL ))objc_msgSend )( (id)_pCocoaWindow, isVisibleSel );
		}
		return false;
	}
#else
	bool CocoaWindow::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
	{
		std::ignore = pTitle;
		_width		= width;
		_height		= height;
		return true;
	}

	void CocoaWindow::destroy()
	{
	}

	bool CocoaWindow::recreate()
	{
		return false;
	}

	bool CocoaWindow::processMessages()
	{
		return _bShouldClose == false;
	}

	void CocoaWindow::showWindow( bool bShow )
	{
		(void)bShow;
	}

	bool CocoaWindow::isVisible() const
	{
		return false;
	}
#endif
} // namespace sw
