#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Window/GenericWindow.h"
#include "Engine/Window/Tests/FakeWindow.h"

// 수명 상태는 `GenericWindow` 의 몫이라 플랫폼 없는 창(`FakeWindow`)으로 그것만 본다.

using namespace sw;

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
    SW_EXPECT_EQUAL( uint64{ 0 }, window.getRenderPausedTickCount().get() );

    window.simulateAppInterruption( 3 );
    SW_EXPECT_TRUE( window.isRenderPaused() );
    window.advanceLifecycleTick();
    window.advanceLifecycleTick();
    SW_EXPECT_TRUE( window.isRenderPaused() );
    window.advanceLifecycleTick(); // 이 틱은 멈춘 채 세고 끝에 돌아온다 — 메인 루프는 이 틱에 프레임을 돈다
    SW_EXPECT_FALSE( window.isRenderPaused() );
    SW_EXPECT_EQUAL( uint64{ 3 }, window.getRenderPausedTickCount().get() );

    window.advanceLifecycleTick();
    SW_EXPECT_EQUAL( uint64{ 3 }, window.getRenderPausedTickCount().get() );
}

/** @brief [WindowLifecycleTest] 구독자(입력 · 오디오)는 렌더러 처리기보다 먼저 받는다 — 앱이 멈추면 소리 · 입력이 먼저 서고 표면은 렌더러가 마지막에 놓는다(감사 P-6). */
SW_TEST_CASE( WindowLifecycleTest, ListenersRunBeforeRendererCallback )
{
    FakeWindow         window;
    vector<int32>      listOrder;
    const Subscription renderer = window.bindLifecycleCallback( SW_DELEGATE_LAMBDA( WindowLifecycleDelegate, [&listOrder]( WindowLifecycleEvent )
    { listOrder.push_back( 0 ); } ) );
    const Subscription listener = window.addLifecycleListener( SW_DELEGATE_LAMBDA( WindowLifecycleDelegate, [&listOrder]( WindowLifecycleEvent )
    { listOrder.push_back( 1 ); } ) );

    window.notifyLifecycleEvent( WindowLifecycleEvent::AppSuspended );
    window.notifyLifecycleEvent( WindowLifecycleEvent::AppSuspended ); // 바뀌지 않으면 아무도 받지 않는다
    SW_REQUIRE_EQUAL( size_t{ 2 }, listOrder.size() );
    SW_EXPECT_EQUAL( 1, listOrder[0] );
    SW_EXPECT_EQUAL( 0, listOrder[1] );
}

/**
 * @brief [WindowLifecycleTest] 메시지마다 오는 크기 통보는 콜백을 바로 부르지 않고, 프레임 시작의 `applyPendingResize` 가 마지막 크기로 한 번 부른다 —
 *        끌기 한 번에 스왑체인 `waitIdle` + `resize` 가 메시지 수만큼 돌던 결함(감사 P-2). 같은 크기 · 다시 만드는 중의 통보는 적용할 것이 없다.
 */
SW_TEST_CASE( WindowLifecycleTest, ResizeIsAppliedOncePerFrameWithTheLastSize )
{
    FakeWindow         window;
    vector<uint32>     listWidth;
    uint32             lastHeight = 0;
    const Subscription resize     = window.bindResizeCallback( SW_DELEGATE_LAMBDA( WindowResizeDelegate, [&listWidth, &lastHeight]( uint32 width, uint32 height )
        {
        listWidth.push_back( width );
        lastHeight = height;
    } ) );

    window.simulateResize( 800, 600 );
    window.simulateResize( 900, 650 );
    window.simulateResize( 1000, 700 );
    SW_EXPECT_TRUE( listWidth.empty() ); // 메시지 처리 안에서는 부르지 않는다
    SW_EXPECT_TRUE( window.hasPendingResize() );
    SW_EXPECT_EQUAL( 1000u, window.getWidth() );

    window.applyPendingResize();
    window.applyPendingResize(); // 대기가 없으면 다시 부르지 않는다
    SW_REQUIRE_EQUAL( size_t{ 1 }, listWidth.size() );
    SW_EXPECT_EQUAL( 1000u, listWidth[0] );
    SW_EXPECT_EQUAL( 700u, lastHeight );
    SW_EXPECT_FALSE( window.hasPendingResize() );

    window.simulateResize( 1000, 700 ); // 같은 크기
    SW_EXPECT_FALSE( window.hasPendingResize() );
}

/**
 * @brief [WindowLifecycleTest] 모달 루프 타이머는 틱 콜백을 부르고, 콜백 안에서 다시 온 타이머(중첩 메시지 펌프)는 건너뛴다 — 끌기 동안 프레임이 멈추던 결함(감사 P-2)의
 *        틱 자리와 그 재진입 가드. 콜백이 도는 동안만 `isInModalLoopUpdate` 이다(그 안에서는 창을 다시 만들지 않는다).
 */
SW_TEST_CASE( WindowLifecycleTest, ModalLoopUpdateDoesNotReenter )
{
    FakeWindow         window;
    uint32             tickCount     = 0;
    bool               bInTickInside = false;
    const Subscription tick          = window.bindModalLoopUpdateCallback( SW_DELEGATE_LAMBDA( WindowModalLoopUpdateDelegate, [&window, &tickCount, &bInTickInside]()
             {
        ++tickCount;
        bInTickInside = window.isInModalLoopUpdate();
        window.simulateModalLoopTimer(); // 프레임 안의 메시지 펌프가 타이머를 다시 꺼낸 경우
    } ) );

    SW_EXPECT_FALSE( window.isInModalLoopUpdate() );
    window.simulateModalLoopTimer();
    SW_EXPECT_EQUAL( 1u, tickCount );
    SW_EXPECT_TRUE( bInTickInside );
    SW_EXPECT_FALSE( window.isInModalLoopUpdate() );
    window.simulateModalLoopTimer();
    SW_EXPECT_EQUAL( 2u, tickCount );
}
