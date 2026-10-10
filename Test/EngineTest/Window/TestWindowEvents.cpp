#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "Engine/Window/WindowEvents.h"

#include "TestFramework/TestFramework.h"

namespace
{
    int32 s_receivedWidth{ 0 };
    int32 s_closeCount{ 0 };
    bool  s_bActivateReceived{ false };

    /** @brief 받은 크기 이벤트의 너비를 기록합니다. */
    void onResize( const sw::WindowResizeEvent& event )
    {
        s_receivedWidth = event._width;
    }

    /** @brief 받은 닫기 이벤트 수를 셉니다. */
    void onClose( const sw::WindowCloseEvent& )
    {
        ++s_closeCount;
    }

    /** @brief 받은 활성 이벤트의 값을 기록합니다. */
    void onActivate( const sw::WindowActivateEvent& event )
    {
        s_bActivateReceived = ( event._bIsActivate == SW_TRUE );
    }
} // namespace

// ------------------------------------------------------------------------------
// WindowEventTest — 창 이벤트 값 타입(Engine/Window/WindowEvents.h). 디스패처 자체는 CoreTest 의 EventTest 가 본다
// ------------------------------------------------------------------------------
/**
 * @brief [WindowEventTest] 창 이벤트는 엔진 예약 ID 를 싣고, 기본 생성은 크기 0 · 플래그 꺼짐이며, 디스패처로 구독자에게 간다
 */
SW_TEST_CASE( WindowEventTest, EventsCarryReservedIDsAndReachSubscribers )
{
    sw::WindowResizeEvent   resizeEvent;
    sw::WindowCloseEvent    closeEvent;
    sw::WindowActivateEvent activateEvent;

    SW_EXPECT_EQUAL( sw::kEventWindowResize, resizeEvent.getEventType() );
    SW_EXPECT_EQUAL( sw::kEventWindowClose, closeEvent.getEventType() );
    SW_EXPECT_EQUAL( sw::kEventWindowActivate, activateEvent.getEventType() );

    SW_EXPECT_EQUAL( 0, resizeEvent._width );
    SW_EXPECT_EQUAL( 0, resizeEvent._height );
    SW_EXPECT_TRUE( resizeEvent._bIsResizing == SW_FALSE );
    SW_EXPECT_TRUE( resizeEvent._bIsMaximized == SW_FALSE );
    SW_EXPECT_TRUE( resizeEvent._bIsMinimized == SW_FALSE );
    SW_EXPECT_TRUE( activateEvent._bIsActivate == SW_FALSE );

    s_receivedWidth     = 0;
    s_closeCount        = 0;
    s_bActivateReceived = false;

    using ResizeDelegate   = sw::Delegate<void( const sw::WindowResizeEvent& )>;
    using CloseDelegate    = sw::Delegate<void( const sw::WindowCloseEvent& )>;
    using ActivateDelegate = sw::Delegate<void( const sw::WindowActivateEvent& )>;

    sw::EventDispatcher dispatcher;
    dispatcher.subscribe<sw::WindowResizeEvent>( SW_DELEGATE_FUNCTION( ResizeDelegate, onResize ) );
    dispatcher.subscribe<sw::WindowCloseEvent>( SW_DELEGATE_FUNCTION( CloseDelegate, onClose ) );
    dispatcher.subscribe<sw::WindowActivateEvent>( SW_DELEGATE_FUNCTION( ActivateDelegate, onActivate ) );

    resizeEvent._width         = 1920;
    resizeEvent._height        = 1080;
    activateEvent._bIsActivate = SW_TRUE;
    dispatcher.publish( resizeEvent );
    dispatcher.publish( closeEvent );
    dispatcher.publish( activateEvent );

    SW_EXPECT_EQUAL( 1920, s_receivedWidth );
    SW_EXPECT_EQUAL( 1, s_closeCount );
    SW_EXPECT_TRUE( s_bActivateReceived );

    dispatcher.clear();
}
