/**
 * @file TestEvent.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Core/Utility/Delegate/Delegate.h"
#include "Core/Utility/Event/EventDispatcher.h"

static int g_LastResizeWidth  = 0;
static int g_LastResizeHeight = 0;

static void onWindowResize( const sw::WindowResizeEvent& e )
{
	g_LastResizeWidth  = e._width;
	g_LastResizeHeight = e._height;
}

SW_TEST_CASE( Utility_Event, DispatcherPushAndDispatch )
{
	g_LastResizeWidth  = 0;
	g_LastResizeHeight = 0;

	sw::EventDispatcher								   dispatcher;
	sw::Delegate<void( const sw::WindowResizeEvent& )> del = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowResizeEvent& )>, onWindowResize );
	dispatcher.subscribe<sw::WindowResizeEvent>( del );

	sw::WindowResizeEvent event;
	event._width  = 1920;
	event._height = 1080;
	dispatcher.push( event );

	SW_EXPECT_EQUAL( 0, g_LastResizeWidth );

	dispatcher.publish( event );

	SW_EXPECT_EQUAL( 1920, g_LastResizeWidth );
	SW_EXPECT_EQUAL( 1080, g_LastResizeHeight );

	dispatcher.unsubscribe<sw::WindowResizeEvent>( del );
	dispatcher.clear();
}

static bool g_bWindowClosed	   = false;
static bool g_bWindowActivated = false;

static void onWindowClose( const sw::WindowCloseEvent& )
{
	g_bWindowClosed = true;
}

static void onWindowActivate( const sw::WindowActivateEvent& e )
{
	g_bWindowActivated = e._bIsActivate;
}

SW_TEST_CASE( Utility_Event, DispatcherCloseAndActivateEvents )
{
	g_bWindowClosed	   = false;
	g_bWindowActivated = false;

	sw::EventDispatcher dispatcher;
	auto				closeDel	= SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowCloseEvent& )>, onWindowClose );
	auto				activateDel = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowActivateEvent& )>, onWindowActivate );

	dispatcher.subscribe<sw::WindowCloseEvent>( closeDel );
	dispatcher.subscribe<sw::WindowActivateEvent>( activateDel );

	sw::WindowCloseEvent	closeEvt;
	sw::WindowActivateEvent activateEvt;
	activateEvt._bIsActivate = true;

	dispatcher.publish( closeEvt );
	dispatcher.publish( activateEvt );

	SW_EXPECT_TRUE( g_bWindowClosed );
	SW_EXPECT_TRUE( g_bWindowActivated );

	SW_EXPECT_EQUAL( static_cast<uint32>( sw::EventType::WindowClose ), static_cast<uint32>( closeEvt.getEventType() ) );
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::EventType::WindowActivate ), static_cast<uint32>( activateEvt.getEventType() ) );

	dispatcher.clear();
}

SW_TEST_CASE( Utility_Event, DeferredEventQueueTest )
{
	g_LastResizeWidth  = 0;
	g_LastResizeHeight = 0;

	sw::EventDispatcher								   dispatcher;
	sw::Delegate<void( const sw::WindowResizeEvent& )> resizeDel = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowResizeEvent& )>, onWindowResize );
	dispatcher.subscribe<sw::WindowResizeEvent>( resizeDel );

	sw::WindowResizeEvent event;
	event._width  = 2560;
	event._height = 1440;

	dispatcher.publish( event );
	SW_EXPECT_EQUAL( 2560, g_LastResizeWidth );
	SW_EXPECT_EQUAL( 1440, g_LastResizeHeight );

	dispatcher.unsubscribe<sw::WindowResizeEvent>( resizeDel );
	dispatcher.clear();
}

SW_TEST_CASE( Utility_Event, EventDispatcherChannelFiltering )
{
	sw::EventDispatcher dispatcher;

	static int32 uiChannelReceived	  = 0;
	static int32 audioChannelReceived = 0;

	sw::hashed_string uiChannel( "UI_Channel" );
	sw::hashed_string audioChannel( "Audio_Channel" );

	auto lambdaUI = []( const sw::WindowResizeEvent& e )
	{ uiChannelReceived = e._width; };
	sw::Delegate<void( const sw::WindowResizeEvent& )> uiDel = SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::WindowResizeEvent& )>, lambdaUI );

	auto lambdaAudio = []( const sw::WindowResizeEvent& e )
	{ audioChannelReceived = e._height; };
	sw::Delegate<void( const sw::WindowResizeEvent& )> audioDel = SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::WindowResizeEvent& )>, lambdaAudio );

	dispatcher.subscribe<sw::WindowResizeEvent>( uiChannel, uiDel );
	dispatcher.subscribe<sw::WindowResizeEvent>( audioChannel, audioDel );

	sw::WindowResizeEvent event;
	event._width  = 1280;
	event._height = 720;

	dispatcher.publish<sw::WindowResizeEvent>( uiChannel, event );
	SW_EXPECT_EQUAL( 1280, uiChannelReceived );
	SW_EXPECT_EQUAL( 0, audioChannelReceived );

	dispatcher.publish<sw::WindowResizeEvent>( audioChannel, event );
	SW_EXPECT_EQUAL( 720, audioChannelReceived );

	dispatcher.clear();
}
