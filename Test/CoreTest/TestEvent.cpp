#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "TestFramework/TestFramework.h"

static int32 s_LastResizeWidth{ 0 };
static int32 s_LastResizeHeight{ 0 };

/** @brief 리사이즈 이벤트에서 너비·높이를 기록합니다. */
static void onWindowResize( const sw::WindowResizeEvent& e )
{
	s_LastResizeWidth  = e._width;
	s_LastResizeHeight = e._height;
}

// ------------------------------------------------------------------------------
// 1) Engine_Event — 디스패치·채널 필터
// ------------------------------------------------------------------------------
/**
 * @brief [Engine_Event] 디스패처 push 와 dispatch
 */
SW_TEST_CASE( Engine_Event, DispatcherPushAndDispatch )
{
	s_LastResizeWidth  = 0;
	s_LastResizeHeight = 0;

	sw::EventDispatcher								   dispatcher;
	sw::Delegate<void( const sw::WindowResizeEvent& )> del = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowResizeEvent& )>, onWindowResize );
	dispatcher.subscribe<sw::WindowResizeEvent>( del );

	sw::WindowResizeEvent event;
	event._width  = 1920;
	event._height = 1080;
	dispatcher.push( event );

	SW_EXPECT_EQUAL( 0, s_LastResizeWidth );

	dispatcher.publish( event );

	SW_EXPECT_EQUAL( 1920, s_LastResizeWidth );
	SW_EXPECT_EQUAL( 1080, s_LastResizeHeight );

	dispatcher.unsubscribe<sw::WindowResizeEvent>( del );
	dispatcher.clear();
}

static bool s_bWindowClosed{ false };
static bool s_bWindowActivated{ false };

/** @brief 창 닫기 이벤트를 기록합니다. */
static void onWindowClose( const sw::WindowCloseEvent& )
{
	s_bWindowClosed = true;
}

/** @brief 창 활성화 여부를 기록합니다. */
static void onWindowActivate( const sw::WindowActivateEvent& e )
{
	s_bWindowActivated = e._bIsActivate;
}

/**
 * @brief [Engine_Event] 닫기·활성화 이벤트
 */
SW_TEST_CASE( Engine_Event, DispatcherCloseAndActivateEvents )
{
	s_bWindowClosed	   = false;
	s_bWindowActivated = false;

	sw::EventDispatcher									 dispatcher;
	sw::Delegate<void( const sw::WindowCloseEvent& )>	 closeDel	 = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowCloseEvent& )>, onWindowClose );
	sw::Delegate<void( const sw::WindowActivateEvent& )> activateDel = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowActivateEvent& )>, onWindowActivate );

	dispatcher.subscribe<sw::WindowCloseEvent>( closeDel );
	dispatcher.subscribe<sw::WindowActivateEvent>( activateDel );

	sw::WindowCloseEvent	closeEvt;
	sw::WindowActivateEvent activateEvt;
	activateEvt._bIsActivate = true;

	dispatcher.publish( closeEvt );
	dispatcher.publish( activateEvt );

	SW_EXPECT_TRUE( s_bWindowClosed );
	SW_EXPECT_TRUE( s_bWindowActivated );

	SW_EXPECT_EQUAL( sw::kEventWindowClose, closeEvt.getEventType() );
	SW_EXPECT_EQUAL( sw::kEventWindowActivate, activateEvt.getEventType() );

	dispatcher.clear();
}

/**
 * @brief [Engine_Event] 지연 이벤트 큐
 */
SW_TEST_CASE( Engine_Event, DeferredEventQueueTest )
{
	s_LastResizeWidth  = 0;
	s_LastResizeHeight = 0;

	sw::EventDispatcher								   dispatcher;
	sw::Delegate<void( const sw::WindowResizeEvent& )> resizeDel = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowResizeEvent& )>, onWindowResize );
	dispatcher.subscribe<sw::WindowResizeEvent>( resizeDel );

	sw::WindowResizeEvent event;
	event._width  = 2560;
	event._height = 1440;

	dispatcher.publish( event );
	SW_EXPECT_EQUAL( 2560, s_LastResizeWidth );
	SW_EXPECT_EQUAL( 1440, s_LastResizeHeight );

	dispatcher.unsubscribe<sw::WindowResizeEvent>( resizeDel );
	dispatcher.clear();
}

/**
 * @brief [Engine_Event] 채널 필터링
 */
SW_TEST_CASE( Engine_Event, EventDispatcherChannelFiltering )
{
	sw::EventDispatcher dispatcher;

	static int32 s_uiChannelReceived{ 0 };
	static int32 s_audioChannelReceived{ 0 };

	sw::hashed_string uiChannel( "UI_Channel" );
	sw::hashed_string audioChannel( "Audio_Channel" );

	sw::Delegate<void( const sw::WindowResizeEvent& )> uiDel = SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::WindowResizeEvent& )>, []( const sw::WindowResizeEvent& e )
	{ s_uiChannelReceived = e._width; } );

	sw::Delegate<void( const sw::WindowResizeEvent& )> audioDel = SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::WindowResizeEvent& )>, []( const sw::WindowResizeEvent& e )
	{ s_audioChannelReceived = e._height; } );

	dispatcher.subscribe<sw::WindowResizeEvent>( uiChannel, uiDel );
	dispatcher.subscribe<sw::WindowResizeEvent>( audioChannel, audioDel );

	sw::WindowResizeEvent event;
	event._width  = 1280;
	event._height = 720;

	dispatcher.publish<sw::WindowResizeEvent>( uiChannel, event );
	SW_EXPECT_EQUAL( 1280, s_uiChannelReceived );
	SW_EXPECT_EQUAL( 0, s_audioChannelReceived );

	dispatcher.publish<sw::WindowResizeEvent>( audioChannel, event );
	SW_EXPECT_EQUAL( 720, s_audioChannelReceived );

	dispatcher.clear();
}

/**
 * @brief [Engine_Event] 64KB 프레임 할당자를 초과하는 대량 이벤트 큐잉 시 오버플로우 메모리 처리 및 0-유실 검증
 */
SW_TEST_CASE( Engine_Event, FrameAllocatorOverflowFallback )
{
	sw::EventDispatcher dispatcher;

	int32 receivedCount{ 0 };
	int32 lastReceivedIndex{ -1 };

	sw::Delegate<void( const sw::WindowResizeEvent& )> del = SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::WindowResizeEvent& )>, [&]( const sw::WindowResizeEvent& e )
	{
		++receivedCount;
		SW_EXPECT_EQUAL( lastReceivedIndex + 1, e._width );
		lastReceivedIndex = e._width;
	} );

	dispatcher.subscribe<sw::WindowResizeEvent>( del );

	// Push 3000 events (> 90 KB, exceeding default 64KB linear arena)
	constexpr int32 kTotalEvents = 3000;
	for ( int32 index = 0; index < kTotalEvents; ++index )
	{
		sw::WindowResizeEvent evt;
		evt._width	= index;
		evt._height = index * 2;
		dispatcher.push( evt );
	}

	SW_EXPECT_EQUAL( 0, receivedCount );

	dispatcher.processEvents();

	SW_EXPECT_EQUAL( kTotalEvents, receivedCount );
	SW_EXPECT_EQUAL( kTotalEvents - 1, lastReceivedIndex );

	dispatcher.clear();
}
