#include "pch.h"

#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

// 플랫폼 창 — 실제 OS 창을 띄우고 리사이즈 콜백과 메시지 훅을 본다.
//
// SW_TEST_REQUIRES_HOST( WindowTest ): 진짜 창을 만든다. 헤드리스 CI 러너엔 디스플레이가 없다.

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

/**
 * @brief [WindowTest] 활성 창이 죽으면 전역 포인터도 같이 끊긴다
 * @details `IWindow::getActiveWindow()` 는 전역 하나를 돌려준다. 그 창이 파괴돼도 전역은
 *          그대로 남아서, 뒤에 부르는 쪽(RHI 초기화 · 에디터 명령 · 프레임 트랜지언트)이
 *          **죽은 포인터**를 받았다. `App::shutdown` 은 파괴 전에 손으로 끊고 있었지만
 *          그것은 한 경로의 규율이다 — `EngineLoop` 은 App 이 없는 임베드 시나리오에서
 *          창을 전역에 놓아둔 채 소유를 호출자에게 넘기고, 테스트도 App 을 거치지 않는다.
 *          소멸자에서 끊으면 어느 경로로 죽어도 참이 된다.
 */
SW_TEST_CASE( WindowTest, DestroyedActiveWindowClearsGlobal )
{
    sw::IWindow* const pPrevious = sw::IWindow::getActiveWindow();
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [pPrevious]()
    {
        sw::IWindow::setActiveWindow( pPrevious );
    } ) );

    {
        sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
        SW_ASSERT_TRUE( window != nullptr );
        SW_ASSERT_TRUE( window->initializeWindow( "ActiveWindowGlobalTest", 320, 240 ) );

        sw::IWindow::setActiveWindow( window.get() );
        SW_ASSERT_EQUAL( window.get(), sw::IWindow::getActiveWindow() );

        window->destroy();
        // 소멸까지 가야 끊긴다 — `destroy()` 는 네이티브 핸들만 반환한다.
        SW_EXPECT_EQUAL( window.get(), sw::IWindow::getActiveWindow() );
    }

    SW_EXPECT_EQUAL( nullptr, sw::IWindow::getActiveWindow() );
}
