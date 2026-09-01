#include "pch.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodes.h"
#include "Engine/Input/Queue/LockFreeInputQueue.h"
#include "Engine/Input/Windows/GamepadXInput.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// ------------------------------------------------------------------------------
// InputManagerTest — 입력 상태 및 키/마우스 프레임 엣지 감지 검증
// ------------------------------------------------------------------------------

SW_TEST_CASE( InputManagerTest, LifecycleAndDefaults )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	// 기본 상태 검증
	SW_EXPECT_FALSE( input.isKeyDown( sw::Key::A ) );
	SW_EXPECT_FALSE( input.wasKeyPressed( sw::Key::A ) );
	SW_EXPECT_FALSE( input.wasKeyReleased( sw::Key::A ) );

	SW_EXPECT_FALSE( input.isMouseButtonDown( sw::MouseButton::Left ) );
	SW_EXPECT_FALSE( input.wasMouseButtonPressed( sw::MouseButton::Left ) );
	SW_EXPECT_FALSE( input.wasMouseButtonReleased( sw::MouseButton::Left ) );

	int32 mx = -1, my = -1;
	input.getMousePosition( mx, my );
	SW_EXPECT_EQUAL( 0, mx );
	SW_EXPECT_EQUAL( 0, my );

	input.shutdown();
}

#if defined( SW_PLATFORM_WINDOWS )
SW_TEST_CASE( InputManagerTest, NativeEventKeyPressReleaseEdges )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	// 초기 스냅샷 맞춤
	input.endFrame();

	// 프레임 1: Space KeyDown 이벤트 수신
	sw::NativeWindowEvent downEvt{};
	downEvt._message = WM_KEYDOWN;
	downEvt._wParam	 = VK_SPACE;
	input.processNativeEvent( downEvt );

	SW_EXPECT_TRUE( input.isKeyDown( sw::Key::Space ) );
	SW_EXPECT_TRUE( input.wasKeyPressed( sw::Key::Space ) );
	SW_EXPECT_FALSE( input.wasKeyReleased( sw::Key::Space ) );

	// 프레임 1 종료: 키 눌림 상태가 prev로 복사됨
	input.endFrame();

	// 프레임 2: 키 유지 중
	SW_EXPECT_TRUE( input.isKeyDown( sw::Key::Space ) );
	SW_EXPECT_FALSE( input.wasKeyPressed( sw::Key::Space ) );
	SW_EXPECT_FALSE( input.wasKeyReleased( sw::Key::Space ) );

	// 프레임 2: Space KeyUp 이벤트 수신
	sw::NativeWindowEvent upEvt{};
	upEvt._message = WM_KEYUP;
	upEvt._wParam  = VK_SPACE;
	input.processNativeEvent( upEvt );

	SW_EXPECT_FALSE( input.isKeyDown( sw::Key::Space ) );
	SW_EXPECT_FALSE( input.wasKeyPressed( sw::Key::Space ) );
	SW_EXPECT_TRUE( input.wasKeyReleased( sw::Key::Space ) );

	// 프레임 2 종료
	input.endFrame();

	SW_EXPECT_FALSE( input.isKeyDown( sw::Key::Space ) );
	SW_EXPECT_FALSE( input.wasKeyPressed( sw::Key::Space ) );
	SW_EXPECT_FALSE( input.wasKeyReleased( sw::Key::Space ) );

	input.shutdown();
}

SW_TEST_CASE( InputManagerTest, NativeEventMouseMovementAndDelta )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	// 초기 스냅샷
	input.endFrame();

	// 프레임 1: 마우스 이동 및 좌클릭 다운
	sw::NativeWindowEvent moveEvt1{};
	moveEvt1._message = WM_MOUSEMOVE;
	moveEvt1._lParam  = MAKELPARAM( 100, 200 );
	input.processNativeEvent( moveEvt1 );

	sw::NativeWindowEvent clickEvt{};
	clickEvt._message = WM_LBUTTONDOWN;
	input.processNativeEvent( clickEvt );

	int32 mx = 0, my = 0;
	input.getMousePosition( mx, my );
	SW_EXPECT_EQUAL( 100, mx );
	SW_EXPECT_EQUAL( 200, my );
	SW_EXPECT_TRUE( input.isMouseButtonDown( sw::MouseButton::Left ) );
	SW_EXPECT_TRUE( input.wasMouseButtonPressed( sw::MouseButton::Left ) );

	// 프레임 1 종료 (현재 마우스 100, 200이 prev로 기록됨)
	input.endFrame();

	// 프레임 2: 마우스 추가 이동 (150, 230) 및 버튼 뗌
	sw::NativeWindowEvent moveEvt2{};
	moveEvt2._message = WM_MOUSEMOVE;
	moveEvt2._lParam  = MAKELPARAM( 150, 230 );
	input.processNativeEvent( moveEvt2 );

	sw::NativeWindowEvent releaseEvt{};
	releaseEvt._message = WM_LBUTTONUP;
	input.processNativeEvent( releaseEvt );

	int32 dx = 0, dy = 0;
	input.getMouseDelta( dx, dy );
	SW_EXPECT_EQUAL( 50, dx );
	SW_EXPECT_EQUAL( 30, dy );

	SW_EXPECT_FALSE( input.isMouseButtonDown( sw::MouseButton::Left ) );
	SW_EXPECT_TRUE( input.wasMouseButtonReleased( sw::MouseButton::Left ) );

	input.endFrame();
	input.shutdown();
}

SW_TEST_CASE( InputManagerTest, NativeEventMouseWheelAndEdges )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );
	input.setGamepadPollingEnabled( false );

	// 초기 휠 상태
	SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouseWheelDelta(), 1e-4f );

	// 1) 휠 스크롤 위로 1틱 (+120)
	sw::NativeWindowEvent wheelUpEvt{};
	wheelUpEvt._message = WM_MOUSEWHEEL;
	wheelUpEvt._wParam	= MAKEWPARAM( 0, 120 );
	input.processNativeEvent( wheelUpEvt );

	// beginFrame 호출 시 누적 휠이 이번 프레임 델타로 전이됨
	input.beginFrame();
	SW_EXPECT_NEAR_EQUAL( 1.0f, input.getMouseWheelDelta(), 1e-4f );

	// endFrame 호출 시 델타 0으로 리셋
	input.endFrame();
	SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouseWheelDelta(), 1e-4f );

	// 2) 휠 스크롤 아래로 2틱 (-240)
	sw::NativeWindowEvent wheelDownEvt{};
	wheelDownEvt._message = WM_MOUSEWHEEL;
	wheelDownEvt._wParam  = MAKEWPARAM( 0, -240 );
	input.processNativeEvent( wheelDownEvt );

	input.beginFrame();
	SW_EXPECT_NEAR_EQUAL( -2.0f, input.getMouseWheelDelta(), 1e-4f );

	input.endFrame();
	SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouseWheelDelta(), 1e-4f );

	input.shutdown();
}

SW_TEST_CASE( InputManagerTest, NativeEventRightAndMiddleMouseButtons )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	// 1) 우클릭 다운
	sw::NativeWindowEvent rDown{};
	rDown._message = WM_RBUTTONDOWN;
	input.processNativeEvent( rDown );

	SW_EXPECT_TRUE( input.isMouseButtonDown( sw::MouseButton::Right ) );
	SW_EXPECT_TRUE( input.wasMouseButtonPressed( sw::MouseButton::Right ) );
	SW_EXPECT_FALSE( input.wasMouseButtonReleased( sw::MouseButton::Right ) );

	input.endFrame();

	SW_EXPECT_TRUE( input.isMouseButtonDown( sw::MouseButton::Right ) );
	SW_EXPECT_FALSE( input.wasMouseButtonPressed( sw::MouseButton::Right ) );

	// 2) 우클릭 업
	sw::NativeWindowEvent rUp{};
	rUp._message = WM_RBUTTONUP;
	input.processNativeEvent( rUp );

	SW_EXPECT_FALSE( input.isMouseButtonDown( sw::MouseButton::Right ) );
	SW_EXPECT_TRUE( input.wasMouseButtonReleased( sw::MouseButton::Right ) );

	input.endFrame();

	// 3) 휠(중간) 클릭 다운 및 업
	sw::NativeWindowEvent mDown{};
	mDown._message = WM_MBUTTONDOWN;
	input.processNativeEvent( mDown );

	SW_EXPECT_TRUE( input.isMouseButtonDown( sw::MouseButton::Middle ) );
	SW_EXPECT_TRUE( input.wasMouseButtonPressed( sw::MouseButton::Middle ) );

	input.endFrame();

	sw::NativeWindowEvent mUp{};
	mUp._message = WM_MBUTTONUP;
	input.processNativeEvent( mUp );

	SW_EXPECT_FALSE( input.isMouseButtonDown( sw::MouseButton::Middle ) );
	SW_EXPECT_TRUE( input.wasMouseButtonReleased( sw::MouseButton::Middle ) );

	input.endFrame();
	input.shutdown();
}

SW_TEST_CASE( InputManagerTest, ActionMapVector2DMovement )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap actionMap;
	actionMap.setInputManager( &input );
	actionMap.bindVector2D( "Move", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D, 0.1f );

	// 1) 아무 키도 안 눌렸을 때 -> (0, 0)
	sw::float2 v0 = actionMap.getVector2D( "Move" );
	SW_EXPECT_NEAR_EQUAL( 0.0f, v0._x, 0.0001f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, v0._y, 0.0001f );

	// 2) W 키(상향) 입력 -> (0, 1)
	sw::NativeWindowEvent wDown{};
	wDown._message = WM_KEYDOWN;
	wDown._wParam  = 'W';
	input.processNativeEvent( wDown );

	sw::float2 vUp = actionMap.getVector2D( "Move" );
	SW_EXPECT_NEAR_EQUAL( 0.0f, vUp._x, 0.0001f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, vUp._y, 0.0001f );

	// 3) W + D(우상향 대각선) 입력 -> 정규화되어 (1/sqrt(2), 1/sqrt(2)) ~= (0.7071f, 0.7071f)
	sw::NativeWindowEvent dDown{};
	dDown._message = WM_KEYDOWN;
	dDown._wParam  = 'D';
	input.processNativeEvent( dDown );

	sw::float2	  vDiag		   = actionMap.getVector2D( "Move" );
	const float32 expectedDiag = 1.0f / sw::MathUtil::sqrt( 2.0f );
	SW_EXPECT_NEAR_EQUAL( expectedDiag, vDiag._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( expectedDiag, vDiag._y, 0.001f );

	input.shutdown();
}

SW_TEST_CASE( InputManagerTest, ActionMapChordedActions )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap actionMap;
	actionMap.setInputManager( &input );
	actionMap.bindChord( "QuickSave", sw::Key::LeftControl, sw::Key::S );

	input.endFrame();

	// 1) 초기 상태 -> false
	SW_EXPECT_FALSE( actionMap.isChordDown( "QuickSave" ) );
	SW_EXPECT_FALSE( actionMap.wasChordTriggered( "QuickSave" ) );

	// 2) LeftControl만 눌림 -> false
	sw::NativeWindowEvent ctrlDown{};
	ctrlDown._message = WM_KEYDOWN;
	ctrlDown._wParam  = VK_LCONTROL;
	input.processNativeEvent( ctrlDown );

	SW_EXPECT_FALSE( actionMap.isChordDown( "QuickSave" ) );
	SW_EXPECT_FALSE( actionMap.wasChordTriggered( "QuickSave" ) );

	// 3) S 키 눌림 -> chord 발화!
	sw::NativeWindowEvent sDown{};
	sDown._message = WM_KEYDOWN;
	sDown._wParam  = 'S';
	input.processNativeEvent( sDown );

	SW_EXPECT_TRUE( actionMap.isChordDown( "QuickSave" ) );
	SW_EXPECT_TRUE( actionMap.wasChordTriggered( "QuickSave" ) );

	// 4) 다음 프레임 -> isDown은 true, wasTriggered는 false
	input.endFrame();
	SW_EXPECT_TRUE( actionMap.isChordDown( "QuickSave" ) );
	SW_EXPECT_FALSE( actionMap.wasChordTriggered( "QuickSave" ) );

	input.shutdown();
}

SW_TEST_CASE( InputManagerTest, ActionMapGamepadStick2D )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap actionMap;
	actionMap.setInputManager( &input );
	actionMap.bindGamepadStick2D( "Look", sw::GamepadStick::Right, 0.15f );

	// 기본 상태 -> (0, 0)
	sw::float2 v0 = actionMap.getVector2D( "Look" );
	SW_EXPECT_NEAR_EQUAL( 0.0f, v0._x, 0.0001f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, v0._y, 0.0001f );

	input.shutdown();
}

/**
 * @brief [InputManagerTest] GamepadButtons 이름 변환 및 매핑 양방향 검증
 */
SW_TEST_CASE( InputManagerTest, GamepadButtonsNameMapping )
{
	// 1) 이름 -> 버튼 열거형 (대소문자 무시)
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "A" ) == sw::GamepadButton::A );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "a" ) == sw::GamepadButton::A );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "B" ) == sw::GamepadButton::B );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "X" ) == sw::GamepadButton::X );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "Y" ) == sw::GamepadButton::Y );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "DPadUp" ) == sw::GamepadButton::DPadUp );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "dpadup" ) == sw::GamepadButton::DPadUp );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "DPadDown" ) == sw::GamepadButton::DPadDown );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "DPadLeft" ) == sw::GamepadButton::DPadLeft );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "DPadRight" ) == sw::GamepadButton::DPadRight );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "Start" ) == sw::GamepadButton::Start );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "Back" ) == sw::GamepadButton::Back );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "LeftShoulder" ) == sw::GamepadButton::LeftShoulder );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "RightShoulder" ) == sw::GamepadButton::RightShoulder );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "LeftThumb" ) == sw::GamepadButton::LeftThumb );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "RightThumb" ) == sw::GamepadButton::RightThumb );

	// 알 수 없는 버튼 이름은 Count 반환
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "InvalidButton" ) == sw::GamepadButton::Count );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "" ) == sw::GamepadButton::Count );

	// 2) 버튼 열거형 -> 안정 문자열
	SW_EXPECT_STREQ( "A", sw::GamepadButtons::toName( sw::GamepadButton::A ) );
	SW_EXPECT_STREQ( "B", sw::GamepadButtons::toName( sw::GamepadButton::B ) );
	SW_EXPECT_STREQ( "X", sw::GamepadButtons::toName( sw::GamepadButton::X ) );
	SW_EXPECT_STREQ( "Y", sw::GamepadButtons::toName( sw::GamepadButton::Y ) );
	SW_EXPECT_STREQ( "DPadUp", sw::GamepadButtons::toName( sw::GamepadButton::DPadUp ) );
	SW_EXPECT_STREQ( "Start", sw::GamepadButtons::toName( sw::GamepadButton::Start ) );
	SW_EXPECT_STREQ( "Back", sw::GamepadButtons::toName( sw::GamepadButton::Back ) );
	SW_EXPECT_STREQ( "LeftShoulder", sw::GamepadButtons::toName( sw::GamepadButton::LeftShoulder ) );
	SW_EXPECT_STREQ( "RightShoulder", sw::GamepadButtons::toName( sw::GamepadButton::RightShoulder ) );
	SW_EXPECT_STREQ( "LeftThumb", sw::GamepadButtons::toName( sw::GamepadButton::LeftThumb ) );
	SW_EXPECT_STREQ( "RightThumb", sw::GamepadButtons::toName( sw::GamepadButton::RightThumb ) );

	// Count / 범위 밖은 nullptr 반환
	SW_EXPECT_TRUE( sw::GamepadButtons::toName( sw::GamepadButton::Count ) == nullptr );
}

/**
 * @brief [InputManagerTest] GamepadXInput 기본 상태 및 스틱 데드존 검증
 */
SW_TEST_CASE( InputManagerTest, GamepadXInputDefaultStateAndStickQuery )
{
	sw::GamepadXInput pad;
	pad.poll( 0.016f ); // 연결되지 않은 슬롯 폴링

	for ( size_t btnIndex = 0; btnIndex < static_cast<size_t>( sw::GamepadButton::Count ); ++btnIndex )
	{
		const sw::GamepadButton btn = static_cast<sw::GamepadButton>( btnIndex );
		SW_EXPECT_FALSE( pad.isButtonDown( btn ) );
		SW_EXPECT_FALSE( pad.wasButtonPressed( btn ) );
		SW_EXPECT_FALSE( pad.wasButtonReleased( btn ) );
	}

	float32 lx = 99.0f, ly = 99.0f;
	pad.getLeftStick( lx, ly );
	SW_EXPECT_NEAR_EQUAL( 0.0f, lx, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, ly, 0.001f );

	float32 rx = 99.0f, ry = 99.0f;
	pad.getRightStick( rx, ry );
	SW_EXPECT_NEAR_EQUAL( 0.0f, rx, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, ry, 0.001f );
}
#endif

/**
 * @brief [InputManagerTest] LockFreeInputQueue 단일 스레드 Push/Pop/Drain 및 순환 인덱싱 검증
 */
SW_TEST_CASE( InputManagerTest, LockFreeInputQueue_PushPopDrain )
{
	sw::LockFreeInputQueue<sw::RawInputEvent, 16> queue;
	SW_EXPECT_TRUE( queue.isEmpty() );
	SW_EXPECT_EQUAL( 0u, queue.getCount() );

	// 1) 5개 아이템 Push
	for ( uint16 index = 0; index < 5; ++index )
	{
		SW_EXPECT_TRUE( queue.push( sw::RawInputEvent::makeKeyDown( sw::Key::A, index ) ) );
	}
	SW_EXPECT_FALSE( queue.isEmpty() );
	SW_EXPECT_EQUAL( 5u, queue.getCount() );

	// 2) 2개 아이템 Pop
	sw::RawInputEvent item0{};
	SW_EXPECT_TRUE( queue.pop( item0 ) );
	SW_EXPECT_TRUE( item0._type == sw::RawInputEventType::KeyDown );
	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( item0._payload._keyData._nativeVirtualKey ) );

	sw::RawInputEvent item1{};
	SW_EXPECT_TRUE( queue.pop( item1 ) );
	SW_EXPECT_EQUAL( 1u, static_cast<uint32>( item1._payload._keyData._nativeVirtualKey ) );
	SW_EXPECT_EQUAL( 3u, queue.getCount() );

	// 3) 나머지 3개 일괄 Drain
	sw::RawInputEvent arrDrained[8]{};
	const uint32	  drainedCount = queue.drain( arrDrained, 8 );
	SW_EXPECT_EQUAL( 3u, drainedCount );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( arrDrained[0]._payload._keyData._nativeVirtualKey ) );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( arrDrained[1]._payload._keyData._nativeVirtualKey ) );
	SW_EXPECT_EQUAL( 4u, static_cast<uint32>( arrDrained[2]._payload._keyData._nativeVirtualKey ) );
	SW_EXPECT_TRUE( queue.isEmpty() );
}

/**
 * @brief [InputManagerTest] LockFreeInputQueue 생산자-소비자 멀티스레드 동시성 스트레스 테스트
 */
SW_TEST_CASE( InputManagerTest, LockFreeInputQueue_MultiThreadStress )
{
	sw::LockFreeInputQueue<sw::RawInputEvent, 1024> queue;
	constexpr uint32								kTotalItems = 5000;
	std::atomic<bool>								bProducerDone{ false };

	// Producer Thread (OS Message Pump 시뮬레이션)
	std::thread producerThread(
		[&]()
	{
		for ( uint32 index = 0; index < kTotalItems; ++index )
		{
			sw::RawInputEvent evt = sw::RawInputEvent::makeKeyDown( sw::Key::Space, static_cast<uint16>( index ) );
			while ( queue.push( evt ) == false )
			{
				std::this_thread::yield();
			}
		}
		bProducerDone.store( true, std::memory_order_release );
	} );

	// Consumer Thread (메인 엔진 루프 시뮬레이션)
	uint32 consumedCount	 = 0;
	uint32 nextExpectedIndex = 0;
	bool   bOrderingValid	 = true;

	while ( bProducerDone.load( std::memory_order_acquire ) == false || queue.isEmpty() == false )
	{
		sw::RawInputEvent arrBatch[64]{};
		const uint32	  drained = queue.drain( arrBatch, 64 );
		for ( uint32 batchIndex = 0; batchIndex < drained; ++batchIndex )
		{
			const uint32 receivedIndex = arrBatch[batchIndex]._payload._keyData._nativeVirtualKey;
			if ( receivedIndex != nextExpectedIndex )
			{
				bOrderingValid = false;
			}
			++nextExpectedIndex;
			++consumedCount;
		}
		if ( drained == 0 )
		{
			std::this_thread::yield();
		}
	}

	producerThread.join();

	SW_EXPECT_TRUE( bOrderingValid );
	SW_EXPECT_EQUAL( kTotalItems, consumedCount );
	SW_EXPECT_TRUE( queue.isEmpty() );
}

/**
 * @brief [InputManagerTest] InputManager 비동기 postRawEvent 인입 및 beginFrame 드레인 동기화 검증
 */
SW_TEST_CASE( InputManagerTest, InputManager_AsyncPostAndBeginFrameDrain )
{
	sw::InputManager inputManager;
	SW_EXPECT_TRUE( inputManager.initialize() );

	// 1) 비동기 스레드에서 원시 이벤트 인입 (WM_KEYDOWN)
	inputManager.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::F ) );
	SW_EXPECT_EQUAL( 1u, inputManager.getPendingRawEventCount() );

	// beginFrame 호출 전에는 아직 디바이스에 반영되지 않음
	SW_EXPECT_FALSE( inputManager.isKeyDown( sw::Key::F ) );

	// 2) 메인 스레드 beginFrame() 호출 -> 큐 드레인 및 상태 반영
	inputManager.beginFrame( 0.016f );
	SW_EXPECT_EQUAL( 0u, inputManager.getPendingRawEventCount() );
	SW_EXPECT_TRUE( inputManager.isKeyDown( sw::Key::F ) );

	// 3) 비동기 KeyUp 인입
	inputManager.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::F ) );
	inputManager.beginFrame( 0.016f );
	SW_EXPECT_FALSE( inputManager.isKeyDown( sw::Key::F ) );

	inputManager.shutdown();
}

/**
 * @brief [ActionMapTest] default.input.xml 리소스 로드 및 레이어/액션/코드 바인딩 무결성 검증
 */
SW_TEST_CASE( ActionMapTest, LoadFromDefaultInputXmlResource )
{
	sw::InputManager inputManager;
	SW_EXPECT_TRUE( inputManager.initialize() );

	sw::ActionMap actionMap;
	actionMap.setInputManager( &inputManager );
	SW_EXPECT_TRUE( actionMap.loadFromResource( "engine/input/default.input.xml" ) );

	SW_EXPECT_TRUE( actionMap.hasLayer( "Title" ) );
	SW_EXPECT_TRUE( actionMap.hasLayer( "Gameplay" ) );
	SW_EXPECT_TRUE( actionMap.hasLayer( "Debug" ) );

	SW_EXPECT_TRUE( actionMap.hasAction( "Confirm" ) );
	SW_EXPECT_TRUE( actionMap.hasAction( "Continue" ) );
	SW_EXPECT_TRUE( actionMap.hasAction( "Cancel" ) );
	SW_EXPECT_TRUE( actionMap.hasAction( "ReloadEditor" ) );

	inputManager.shutdown();
}
