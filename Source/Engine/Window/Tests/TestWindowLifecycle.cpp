#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Window/IWindow.h"

using namespace sw;

namespace
{
    /** @brief 플랫폼이 없는 창입니다. 수명 상태는 `IWindow` 의 몫이라 그것만 봅니다. */
    class FakeWindow final : public IWindow
    {
    public:
        bool initializeWindow( const utf8* /*pTitle*/, uint32 /*width*/, uint32 /*height*/ ) override { return true; }
        void destroy() override {}
        bool processMessages() override { return true; }
        void waitForMessages( uint32 /*timeoutMilliseconds*/ ) override {}
        bool tryGetFrameExtents( WindowFrameExtents& outExtents ) const override
        {
            outExtents = WindowFrameExtents{};
            return true;
        }
        bool              canPlaceWindow() const override { return false; }
        bool              setFramePosition( int32 /*x*/, int32 /*y*/ ) override { return false; }
        NativeSurfaceKind getSurfaceKind() const override { return NativeSurfaceKind::Win32; }

    protected:
        void* getNativeHandle() const override { return nullptr; }
    };
} // namespace

/** @brief [WindowLifecycleTest] 표면을 잃거나 앱이 멈추면 돌 수 없고, 둘 다 돌아와야 다시 돕니다. */
SW_TEST_CASE( WindowLifecycleTest, PausesUntilBothSurfaceAndAppReturn )
{
    FakeWindow window;
    SW_EXPECT_FALSE( window.isRenderPaused() );

    window.notifyLifecycleEvent( WindowLifecycleEvent::AppSuspended );
    SW_EXPECT_TRUE( window.isRenderPaused() );
    window.notifyLifecycleEvent( WindowLifecycleEvent::SurfaceLost );
    window.notifyLifecycleEvent( WindowLifecycleEvent::AppResumed );
    SW_EXPECT_TRUE( window.isRenderPaused() ); // 앱은 돌아왔지만 표면이 없다
    window.notifyLifecycleEvent( WindowLifecycleEvent::SurfaceRestored );
    SW_EXPECT_FALSE( window.isRenderPaused() );
}

/** @brief [WindowLifecycleTest] 같은 이벤트가 겹쳐 와도 구독자는 한 번만 받습니다. */
SW_TEST_CASE( WindowLifecycleTest, NotifiesOnlyOnStateChange )
{
    FakeWindow                   window;
    vector<WindowLifecycleEvent> listEvent;
    const Subscription           subscription = window.bindLifecycleCallback( SW_DELEGATE_LAMBDA( WindowLifecycleDelegate, [&listEvent]( WindowLifecycleEvent event )
              { listEvent.push_back( event ); } ) );

    window.notifyLifecycleEvent( WindowLifecycleEvent::SurfaceLost );
    window.notifyLifecycleEvent( WindowLifecycleEvent::SurfaceLost );
    window.notifyLifecycleEvent( WindowLifecycleEvent::AppResumed ); // 멈춘 적이 없다
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0] == WindowLifecycleEvent::SurfaceLost );
}

/** @brief [WindowLifecycleTest] 흉내 낸 멈춤은 주어진 틱 뒤에 돌아오고, 멈춘 채 지난 틱만 셉니다. */
SW_TEST_CASE( WindowLifecycleTest, SimulatedInterruptionEndsAfterItsTicks )
{
    FakeWindow window;
    window.advanceLifecycleTick();
    SW_EXPECT_EQUAL( 0u, window.getRenderPausedTickCount() );

    window.simulateAppInterruption( 3 );
    SW_EXPECT_TRUE( window.isRenderPaused() );
    window.advanceLifecycleTick();
    window.advanceLifecycleTick();
    SW_EXPECT_TRUE( window.isRenderPaused() );
    window.advanceLifecycleTick(); // 이 틱은 멈춘 채 세고 끝에 돌아온다 — 메인 루프는 이 틱에 프레임을 돈다
    SW_EXPECT_FALSE( window.isRenderPaused() );
    SW_EXPECT_EQUAL( 3u, window.getRenderPausedTickCount() );

    window.advanceLifecycleTick();
    SW_EXPECT_EQUAL( 3u, window.getRenderPausedTickCount() );
}
