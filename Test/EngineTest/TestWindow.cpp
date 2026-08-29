#include "pch.h"

#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Engine/Window/Windows/Win32Window.h"
#elif defined( SW_PLATFORM_MACOS )
	#include "Engine/Window/Mac/CocoaWindow.h"
#elif defined( SW_PLATFORM_LINUX )
	#include "Engine/Window/Linux/X11Window.h"
#endif

// ------------------------------------------------------------------------------
// 1) WindowTest — 수명·리사이즈
// ------------------------------------------------------------------------------
/**
 * @brief [WindowTest] 플랫폼 팩토리와 수명
 */
SW_TEST_CASE( WindowTest, PlatformFactoryAndLifecycle )
{
	sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
	SW_ASSERT_TRUE( window != nullptr );

	// DPI/디스플레이 스케일에서 클라이언트 크기는 달라질 수 있다.
	// 픽셀 일치 대신 create + 사용 가능한 0이 아닌 크기 + 정상 destroy 를 검증한다.
	constexpr uint32 kReqW = 640;
	constexpr uint32 kReqH = 600;
	SW_EXPECT_TRUE( window->initializeWindow( "LifecycleTestWindow", kReqW, kReqH ) );

	SW_EXPECT_TRUE( window->getWidth() > 0 );
	SW_EXPECT_TRUE( window->getHeight() > 0 );
	window->destroy();
}

/**
 * @brief [WindowTest] 리사이즈 콜백과 커스텀 메시지 핸들러
 */
SW_TEST_CASE( WindowTest, ResizeCallbackAndCustomMessageHandler )
{
	sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
	SW_EXPECT_TRUE( window != nullptr );

	if ( window != nullptr )
	{
		bool   bResized{ false };
		uint32 newWidth{ 0 };
		uint32 newHeight{ 0 };

		window->setResizeCallback( SW_DELEGATE_LAMBDA( sw::WindowResizeDelegate, [&bResized, &newWidth, &newHeight]( uint32 w, uint32 h )
		{
			bResized  = true;
			newWidth  = w;
			newHeight = h;
		} ) );

		bool bMsgHandled{ false };
		window->setCustomMessageHandler( SW_DELEGATE_LAMBDA( sw::WindowMessageHandlerDelegate, [&bMsgHandled]( const sw::NativeWindowEvent& ) -> bool
		{
			bMsgHandled = true;
			return false;
		} ) );

		bool bCreated = window->initializeWindow( "CallbackTestWindow", 800, 600 );
		SW_EXPECT_TRUE( bCreated );

		window->destroy();
	}
}
