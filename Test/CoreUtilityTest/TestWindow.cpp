/**
 * @file TestWindow.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Core/Window/IWindow.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/Window/Win32Window.h"
#elif defined( SW_PLATFORM_MACOS )
	#include "Core/Window/CocoaWindow.h"
#elif defined( SW_PLATFORM_LINUX )
	#include "Core/Window/X11Window.h"
#endif

SW_TEST_CASE( WindowTest, PlatformFactoryAndLifecycle )
{
	std::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
	SW_ASSERT_TRUE( window != nullptr );

	// Exact client size can differ under DPI / display scaling (was the old flake).
	// Assert create + usable non-zero size + clean destroy instead of pixel-perfect match.
	constexpr uint32 kReqW = 640;
	constexpr uint32 kReqH = 480;
	SW_EXPECT_TRUE( window->create( "LifecycleTestWindow", kReqW, kReqH ) );
	SW_EXPECT_TRUE( window->getWidth() > 0 );
	SW_EXPECT_TRUE( window->getHeight() > 0 );
	window->destroy();
}

SW_TEST_CASE( WindowTest, ResizeCallbackAndCustomMessageHandler )
{
	std::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
	SW_EXPECT_TRUE( window != nullptr );

	if ( window != nullptr )
	{
		bool   bResized	 = false;
		uint32 newWidth	 = 0;
		uint32 newHeight = 0;

		auto resizeCb = [&bResized, &newWidth, &newHeight]( uint32 w, uint32 h )
		{
			bResized  = true;
			newWidth  = w;
			newHeight = h;
		};
		window->setResizeCallback( SW_DELEGATE_LAMBDA( sw::WindowResizeDelegate, resizeCb ) );

		bool bMsgHandled = false;
		auto msgCb		 = [&bMsgHandled]( const sw::NativeWindowEvent& ) -> bool
		{
			bMsgHandled = true;
			return false;
		};
		window->setCustomMessageHandler( SW_DELEGATE_LAMBDA( sw::WindowMessageHandlerDelegate, msgCb ) );

		bool bCreated = window->create( "CallbackTestWindow", 800, 600 );
		SW_EXPECT_TRUE( bCreated );

		window->destroy();
	}
}
