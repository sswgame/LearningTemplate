#include "pch.h"

#include "Core/Concurrency/ConcurrentQueue.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/InputKeyMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputReplay.h"
#include "Engine/Input/KeyCodes.h"
#include "Engine/Input/Utils/VirtualJoystick.h"
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
 * @brief [InputManagerTest] ConcurrentQueue 단일 스레드 Push/Pop/Drain 및 순환 인덱싱 검증
 */
SW_TEST_CASE( InputManagerTest, LockFreeInputQueue_PushPopDrain )
{
	sw::ConcurrentQueue<sw::RawInputEvent, 16> queue;
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
 * @brief [InputManagerTest] ConcurrentQueue 생산자-소비자 멀티스레드 동시성 스트레스 테스트
 */
SW_TEST_CASE( InputManagerTest, LockFreeInputQueue_MultiThreadStress )
{
	sw::ConcurrentQueue<sw::RawInputEvent, 1024> queue;
	constexpr uint32							 kTotalItems = 5000;
	std::atomic<bool>							 bProducerDone{ false };

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

/**
 * @brief [GamepadDeviceTest] GamepadButtons::fromName 및 toName 크로스플랫폼 안정성 검증
 */
SW_TEST_CASE( GamepadDeviceTest, GamepadButtonsFromNameAndToName )
{
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "A" ) == sw::GamepadButton::A );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "DPadUp" ) == sw::GamepadButton::DPadUp );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "RightTrigger" ) == sw::GamepadButton::Count );
	SW_EXPECT_TRUE( sw::GamepadButtons::fromName( "NonExistent" ) == sw::GamepadButton::Count );

	const utf8* pNameA = sw::GamepadButtons::toName( sw::GamepadButton::A );
	SW_EXPECT_TRUE( pNameA != nullptr && sw::StringUtil::equals( pNameA, "A" ) );

	const utf8* pNameStart = sw::GamepadButtons::toName( sw::GamepadButton::Start );
	SW_EXPECT_TRUE( pNameStart != nullptr && sw::StringUtil::equals( pNameStart, "Start" ) );
}

/**
 * @brief [ActionMapTest] getGlyphForAction 디바이스별 및 코드/축 조합 글리프 포맷 검증
 */
SW_TEST_CASE( ActionMapTest, GlyphResolutionWithDeviceTypeAndChords )
{
	sw::InputManager inputManager;
	SW_EXPECT_TRUE( inputManager.initialize() );

	sw::ActionMap& actionMap = inputManager.getActionMap();

	actionMap.bind( "Interact", sw::Key::E );
	actionMap.bind( "Fire", sw::MouseButton::Left );
	actionMap.bind( "Jump", sw::GamepadButton::A );
	actionMap.bindChord( "QuickSave", sw::Key::LeftControl, sw::Key::S );

	inputManager.setActiveDeviceType( sw::InputDeviceType::KeyboardMouse );
	const sw::string glyphInteract = actionMap.getGlyphForAction( "Interact" );
	SW_EXPECT_TRUE( glyphInteract.find( "E" ) != sw::string::npos );

	const sw::string glyphFire = actionMap.getGlyphForAction( "Fire" );
	SW_EXPECT_TRUE( glyphFire.find( "Left" ) != sw::string::npos );

	const sw::string glyphSave = actionMap.getGlyphForAction( "QuickSave" );
	SW_EXPECT_TRUE( glyphSave.find( "LeftControl" ) != sw::string::npos );
	SW_EXPECT_TRUE( glyphSave.find( "S" ) != sw::string::npos );

	inputManager.setActiveDeviceType( sw::InputDeviceType::GamepadXbox );
	const sw::string glyphJumpXbox = actionMap.getGlyphForAction( "Jump" );
	SW_EXPECT_TRUE( glyphJumpXbox.find( "A" ) != sw::string::npos );

	inputManager.setActiveDeviceType( sw::InputDeviceType::GamepadPlayStation );
	const sw::string glyphJumpPS = actionMap.getGlyphForAction( "Jump" );
	SW_EXPECT_TRUE( glyphJumpPS.find( "X" ) != sw::string::npos );

	inputManager.shutdown();
}

/**
 * @brief [VirtualJoystickTest] 터치/가상 조이스틱 벡터 산출, 데드존, 응답 가속 검증
 */
SW_TEST_CASE( VirtualJoystickTest, CalculateVectorAndDeadzone )
{
	const sw::float2 center{ 100.0f, 100.0f };
	const float32	 radius	  = 50.0f;
	const float32	 deadzone = 0.2f;

	const sw::float2 touchInDeadzone{ 105.0f, 100.0f };
	const sw::float2 vecDead = sw::VirtualJoystick::calculateVector( center, touchInDeadzone, radius, deadzone );
	SW_EXPECT_NEAR_EQUAL( 0.0f, vecDead._x, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, vecDead._y, 1e-4f );

	const sw::float2 touchFarRight{ 200.0f, 100.0f };
	const sw::float2 vecFar = sw::VirtualJoystick::calculateVector( center, touchFarRight, radius, deadzone );
	SW_EXPECT_NEAR_EQUAL( 1.0f, vecFar._x, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, vecFar._y, 1e-4f );

	const sw::float2 touchDiag{ 150.0f, 150.0f };
	const sw::float2 vecDiag = sw::VirtualJoystick::calculateVector( center, touchDiag, radius, 0.0f );
	SW_EXPECT_TRUE( vecDiag._x > 0.0f && vecDiag._y > 0.0f );
	const float32 len = sw::MathUtil::sqrt( vecDiag._x * vecDiag._x + vecDiag._y * vecDiag._y );
	SW_EXPECT_NEAR_EQUAL( 1.0f, len, 1e-4f );
}

/**
 * @brief [InputManagerTest] MouseLockMode 상태 전이 및 SubRect 클리핑 검증
 */
SW_TEST_CASE( InputManagerTest, MouseLockModeAndSubRect )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	SW_EXPECT_TRUE( input.getMouseLockMode() == sw::MouseLockMode::None );

	input.setMouseLockMode( sw::MouseLockMode::ConfinedToWindow );
	SW_EXPECT_TRUE( input.getMouseLockMode() == sw::MouseLockMode::ConfinedToWindow );

	input.setMouseLockMode( sw::MouseLockMode::LockedInCenter );
	SW_EXPECT_TRUE( input.getMouseLockMode() == sw::MouseLockMode::LockedInCenter );

	input.setMouseClipSubRect( 10, 20, 300, 400 );
	int32 subX = 0, subY = 0, subW = 0, subH = 0;
	SW_EXPECT_TRUE( input.getMouseClipSubRect( subX, subY, subW, subH ) );
	SW_EXPECT_EQUAL( 10, subX );
	SW_EXPECT_EQUAL( 20, subY );
	SW_EXPECT_EQUAL( 300, subW );
	SW_EXPECT_EQUAL( 400, subH );

	input.clearMouseClipSubRect();
	SW_EXPECT_FALSE( input.getMouseClipSubRect( subX, subY, subW, subH ) );

	input.shutdown();
}

/**
 * @brief [InputManagerTest] 수평 틸트 휠(Horizontal Wheel) 이벤트 및 델타 누적 검증
 */
SW_TEST_CASE( InputManagerTest, MouseWheelHorizontal )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouseWheelHorizontal(), 1e-4f );

	input.postRawEvent( sw::RawInputEvent::makeMouseHorizontalWheel( 1.5f ) );
	input.beginFrame( 0.016f );

	SW_EXPECT_NEAR_EQUAL( 1.5f, input.getMouseWheelHorizontal(), 1e-4f );

	input.endFrame();
	SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouseWheelHorizontal(), 1e-4f );

	input.shutdown();
}

/**
 * @brief [InputManagerTest] 시간 제한 게임패드 진동(Timed Vibration) 카운트다운 자동 차단 검증
 */
SW_TEST_CASE( InputManagerTest, TimedGamepadVibration )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::GamepadDevice* pPad = input.getGamepad( 0 );
	if ( pPad != nullptr )
	{
		pPad->playVibration( 0.8f, 0.6f, 0.1f );
		SW_EXPECT_NEAR_EQUAL( 0.8f, pPad->getLeftMotorVibration(), 1e-4f );
		SW_EXPECT_NEAR_EQUAL( 0.6f, pPad->getRightMotorVibration(), 1e-4f );

		pPad->onFrameBegin( 0.05f );
		SW_EXPECT_NEAR_EQUAL( 0.8f, pPad->getLeftMotorVibration(), 1e-4f );

		pPad->onFrameBegin( 0.06f );
		SW_EXPECT_NEAR_EQUAL( 0.0f, pPad->getLeftMotorVibration(), 1e-4f );
		SW_EXPECT_NEAR_EQUAL( 0.0f, pPad->getRightMotorVibration(), 1e-4f );
	}

	input.shutdown();
}

/**
 * @brief [InputManagerTest] 입력 뮤트(Mute) 및 상태 주입(Injection) 검증
 */
SW_TEST_CASE( InputManagerTest, InputMutingAndInjection )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	SW_EXPECT_FALSE( input.isInputMuted() );
	input.setInputMuted( true );
	SW_EXPECT_TRUE( input.isInputMuted() );

	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::A ) );
	input.beginFrame( 0.016f );
	SW_EXPECT_FALSE( input.isKeyDown( sw::Key::A ) );

	input.setInputMuted( false );

	// Snapshot 직접 주입
	sw::InputSnapshot snap{};
	snap._tickNumber = 200;
	snap._buttonMask = 0x1ULL;
	snap._moveVector = { 1.0f, 0.0f };
	input.injectSnapshot( snap );

	const sw::InputSnapshot* pInjected = input.getSnapshot( 200 );
	SW_EXPECT_TRUE( pInjected != nullptr );
	if ( pInjected != nullptr )
	{
		SW_EXPECT_EQUAL( 0x1ULL, pInjected->_buttonMask );
		SW_EXPECT_NEAR_EQUAL( 1.0f, pInjected->_moveVector._x, 1e-4f );
	}

	input.shutdown();
}

/**
 * @brief [InputKeyMapTest] 하드웨어 물리 스캔코드 매핑(AZERTY/QWERTZ 호환) 검증
 */
SW_TEST_CASE( InputKeyMapTest, PhysicalScanCodeMapping )
{
	SW_EXPECT_TRUE( sw::InputKeyMap::mapScanCodeToKey( 0x11, false ) == sw::Key::W );
	SW_EXPECT_TRUE( sw::InputKeyMap::mapScanCodeToKey( 0x1E, false ) == sw::Key::A );
	SW_EXPECT_TRUE( sw::InputKeyMap::mapScanCodeToKey( 0x1F, false ) == sw::Key::S );
	SW_EXPECT_TRUE( sw::InputKeyMap::mapScanCodeToKey( 0x20, false ) == sw::Key::D );
	SW_EXPECT_TRUE( sw::InputKeyMap::mapScanCodeToKey( 0x39, false ) == sw::Key::Space );
}

/**
 * @brief [RawInputEventTest] 수정자 마스크 및 확장 이벤트 팩토리 검증
 */
SW_TEST_CASE( RawInputEventTest, ModifierMaskAndFactories )
{
	sw::RawInputEvent keyEvt = sw::RawInputEvent::makeKeyDown( sw::Key::S, 0, false, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift );
	SW_EXPECT_TRUE( ( keyEvt._modifierMask & sw::ModifierKey::Ctrl ) != 0 );
	SW_EXPECT_TRUE( ( keyEvt._modifierMask & sw::ModifierKey::Shift ) != 0 );
	SW_EXPECT_FALSE( ( keyEvt._modifierMask & sw::ModifierKey::Alt ) != 0 );

	sw::RawInputEvent dblEvt = sw::RawInputEvent::makeMouseDoubleClick( sw::MouseButton::Left, 120, 240 );
	SW_EXPECT_TRUE( dblEvt._type == sw::RawInputEventType::MouseDoubleClick );
	SW_EXPECT_EQUAL( 120, dblEvt._payload._mouseData._x );
	SW_EXPECT_EQUAL( 240, dblEvt._payload._mouseData._y );

	sw::RawInputEvent compEvt = sw::RawInputEvent::makeTextComposition( "가나다" );
	SW_EXPECT_TRUE( compEvt._type == sw::RawInputEventType::TextComposition );
	SW_EXPECT_TRUE( sw::StringUtil::equals( compEvt._payload._textData._arrUtf8, "가나다" ) );

	sw::RawInputEvent padConnEvt = sw::RawInputEvent::makeGamepadConnection( 0, true );
	SW_EXPECT_TRUE( padConnEvt._type == sw::RawInputEventType::GamepadConnectionChanged );
	SW_EXPECT_EQUAL( 0, static_cast<int32>( padConnEvt._deviceIndex ) );
}

/**
 * @brief [ActionMapTest] MouseDelta2D FPS 룩 벡터 바인딩 검증
 */
SW_TEST_CASE( ActionMapTest, MouseDeltaLookBinding )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap& actionMap = input.getActionMap();
	actionMap.bindMouseDelta( "Look", 2.0f );

	input.postRawEvent( sw::RawInputEvent::makeMouseMove( 10, 5 ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );

	const sw::float2 lookVec = actionMap.getVector2D( "Look" );
	SW_EXPECT_TRUE( lookVec._x != 0.0f || lookVec._y != 0.0f );

	input.shutdown();
}

/**
 * @brief [ActionMapTest] 다중 수정자 복합 단축키(Shortcut) 바인딩 검증
 */
SW_TEST_CASE( ActionMapTest, MultiModifierShortcutBinding )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap& actionMap = input.getActionMap();
	actionMap.bindShortcut( "SaveAs", sw::Key::S, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift );

	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );
	SW_EXPECT_FALSE( actionMap.wasActionTriggered( "SaveAs" ) );
	input.endFrame();

	input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::S ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );
	input.endFrame();

	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::LeftControl ) );
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::LeftShift ) );
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S, 0, false, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );
	SW_EXPECT_TRUE( actionMap.wasActionTriggered( "SaveAs" ) );

	input.shutdown();
}

/**
 * @brief [ActionMapTest] AnyKey 타이틀 화면 바인딩 검증
 */
SW_TEST_CASE( ActionMapTest, AnyKeyBinding )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap& actionMap = input.getActionMap();
	actionMap.bindAnyKey( "PressAnyKeyToStart" );

	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );
	SW_EXPECT_FALSE( actionMap.wasActionTriggered( "PressAnyKeyToStart" ) );
	input.endFrame();

	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );
	SW_EXPECT_TRUE( actionMap.wasActionTriggered( "PressAnyKeyToStart" ) );

	input.shutdown();
}

/**
 * @brief [ActionMapTest] 리바인딩 충돌 해결(Swap / Override / AddSecondary) 검증
 */
SW_TEST_CASE( ActionMapTest, RebindConflictResolution )
{
	sw::ActionMap actionMap;
	actionMap.bind( "ActionA", sw::Key::F );
	actionMap.bind( "ActionB", sw::Key::G );

	const bool bSwapOk = actionMap.rebindWithResolution( "ActionB", sw::InputSlot::fromKey( sw::Key::F ), sw::ConflictResolution::Swap );
	SW_EXPECT_TRUE( bSwapOk );
	const sw::ActionBinding* pBindB = actionMap.getBinding( "ActionB", 0 );
	const sw::ActionBinding* pBindA = actionMap.getBinding( "ActionA", 0 );
	SW_EXPECT_TRUE( pBindB != nullptr && pBindB->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::F ) );
	SW_EXPECT_TRUE( pBindA != nullptr && pBindA->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::G ) );

	const bool bOverrideOk = actionMap.rebindWithResolution( "ActionA", sw::InputSlot::fromKey( sw::Key::F ), sw::ConflictResolution::Override );
	SW_EXPECT_TRUE( bOverrideOk );
	pBindA = actionMap.getBinding( "ActionA", 0 );
	pBindB = actionMap.getBinding( "ActionB", 0 );
	SW_EXPECT_TRUE( pBindA != nullptr && pBindA->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::F ) );
	SW_EXPECT_TRUE( pBindB != nullptr && pBindB->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::Unknown ) );

	const bool bAddOk = actionMap.rebindWithResolution( "ActionB", sw::InputSlot::fromKey( sw::Key::F ), sw::ConflictResolution::AddSecondary );
	SW_EXPECT_TRUE( bAddOk );
	SW_EXPECT_EQUAL( 2u, actionMap.getBindingCount( "ActionB" ) );
}

/**
 * @brief [ActionMapTest] DebugActionState 실시간 덤프 검증
 */
SW_TEST_CASE( ActionMapTest, DebugActionStatesDump )
{
	sw::ActionMap actionMap;
	actionMap.bind( "Jump", sw::Key::Space );
	actionMap.bind( "Fire", sw::MouseButton::Left );

	sw::vector<sw::DebugActionState> listState;
	actionMap.getDebugActionStates( listState );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listState.size() ) );
}

/**
 * @brief [ActionMapTest] ActionHandle 기반 Zero-Lookup O(1) 액션 상태 폴링 검증
 */
SW_TEST_CASE( ActionMapTest, ActionHandleZeroLookup )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap& actionMap = input.getActionMap();
	actionMap.bind( "Fire", sw::Key::Space );
	actionMap.bindVector2D( "Move", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D );

	const sw::ActionHandle hFire = actionMap.getActionHandle( "Fire" );
	const sw::ActionHandle hMove = actionMap.getActionHandle( "Move" );

	SW_EXPECT_TRUE( hFire.isValid() );
	SW_EXPECT_TRUE( hMove.isValid() );

	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) );
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::W ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );

	SW_EXPECT_TRUE( actionMap.wasActionTriggered( hFire ) );
	SW_EXPECT_TRUE( actionMap.isActionDown( hFire ) );
	SW_EXPECT_TRUE( actionMap.wasActionPressed( hFire ) );
	SW_EXPECT_FALSE( actionMap.wasActionReleased( hFire ) );

	const sw::float2 moveVec = actionMap.getVector2D( hMove );
	SW_EXPECT_NEAR_EQUAL( 0.0f, moveVec._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, moveVec._y, 0.001f );
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::ActionPhase::Triggered ), static_cast<uint32>( actionMap.getActionPhase( hFire ) ) );

	input.shutdown();
}

/**
 * @brief [ActionMapTest] 2D 벡터 합성 WASD 대각선 정규화 모드(Circular vs IndependentAxes) 검증
 */
SW_TEST_CASE( ActionMapTest, DigitalNormalizationModes )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap& actionMap = input.getActionMap();
	actionMap.bindVector2D( "Move", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D );

	// 1) IndependentAxes 모드: W + D 대각선 입력 시 X=1.0, Y=1.0 유지
	actionMap.setDigitalNormalization( sw::DigitalNormalization::IndependentAxes );
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::W ) );
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::D ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );

	sw::float2 vecIndep = actionMap.getVector2D( "Move" );
	SW_EXPECT_NEAR_EQUAL( 1.0f, vecIndep._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, vecIndep._y, 0.001f );
	input.endFrame();

	// 2) Circular 모드: W + D 대각선 입력 시 단위 원(길이 1.0)으로 정규화
	actionMap.setDigitalNormalization( sw::DigitalNormalization::Circular );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );

	sw::float2	  vecCirc = actionMap.getVector2D( "Move" );
	const float32 len	  = sw::MathUtil::sqrt( vecCirc._x * vecCirc._x + vecCirc._y * vecCirc._y );
	SW_EXPECT_NEAR_EQUAL( 1.0f, len, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 0.7071f, vecCirc._x, 0.01f );
	SW_EXPECT_NEAR_EQUAL( 0.7071f, vecCirc._y, 0.01f );

	input.shutdown();
}

/**
 * @brief [ActionMapTest] 링버퍼 기반 선입력 및 커맨드 히스토리 제로 할당 래핑 검증
 */
SW_TEST_CASE( ActionMapTest, RingBufferZeroAllocation )
{
	sw::ActionMap actionMap;

	// 1) 선입력 버퍼링 16개 초과 주입 (오버플로우 링 래핑)
	for ( uint32 index = 0; index < 20; ++index )
	{
		actionMap.bufferAction( "Action_" + std::to_string( index ), 0.5f );
	}
	SW_EXPECT_TRUE( actionMap.consumeBufferedAction( "Action_19" ) );
	SW_EXPECT_FALSE( actionMap.consumeBufferedAction( "Action_0" ) ); // 0번은 래핑으로 덮어씌워짐

	// 2) 시간 경과 후 만료 테스트
	actionMap.update( 0.6f );
	SW_EXPECT_FALSE( actionMap.consumeBufferedAction( "Action_19" ) );
}

/**
 * @brief [ActionMapTest] 넘패드 표기법 기반 격투 커맨드 콤보 패턴(236P, 623P) 검증
 */
SW_TEST_CASE( ActionMapTest, CommandPatternFuzzyCombo )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap& actionMap = input.getActionMap();
	actionMap.bind( "Down", sw::Key::S );
	actionMap.bind( "DownRight", sw::Key::C );
	actionMap.bind( "Right", sw::Key::D );
	actionMap.bind( "Punch", sw::Key::J );

	// 1) 2 (Down) -> 3 (DownRight) -> 6 (Right) -> Punch (236P 파동권) 순차 입력
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S ) );
	input.beginFrame( 0.05f );
	actionMap.update( 0.05f );
	input.endFrame();

	input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::S ) );
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::C ) );
	input.beginFrame( 0.05f );
	actionMap.update( 0.05f );
	input.endFrame();

	input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::C ) );
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::D ) );
	input.beginFrame( 0.05f );
	actionMap.update( 0.05f );
	input.endFrame();

	input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::D ) );
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::J ) );
	input.beginFrame( 0.05f );
	actionMap.update( 0.05f );
	input.endFrame();

	SW_EXPECT_TRUE( actionMap.checkCommandPattern( "236Punch", 0.5f ) );
	SW_EXPECT_FALSE( actionMap.checkCommandPattern( "623Punch", 0.5f ) );

	input.shutdown();
}

/**
 * @brief [ActionMapTest] XML 유저 바인딩 전면 직렬화 및 역직렬화 검증 (모든 BindingKind)
 */
SW_TEST_CASE( ActionMapTest, SaveAndLoadAllBindingKinds )
{
	sw::ActionMap mapSave;
	mapSave.bind( "SingleKey", sw::Key::E );
	mapSave.bindAxis1DComposite( "MoveX", sw::Key::A, sw::Key::D );
	mapSave.bindVector2D( "Move2D", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D, 0.1f );
	mapSave.bindGamepadStick2D( "LookStick", sw::GamepadStick::Right, 0.2f, {}, 0, 0.95f, 1.5f );
	mapSave.bindMouseDelta( "LookMouse", 2.5f );
	mapSave.bindChord( "ChordAction", sw::Key::LeftControl, sw::Key::K );
	mapSave.bindShortcut( "ShortcutAction", sw::Key::S, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift );
	mapSave.bindAnyKey( "AnyKeyAction" );

	const sw::string savePath = "test_all_user_bindings.xml";
	SW_EXPECT_TRUE( mapSave.saveUserBindings( savePath ) );

	sw::ActionMap mapLoad;
	SW_EXPECT_TRUE( mapLoad.loadUserBindings( savePath ) );

	SW_EXPECT_TRUE( mapLoad.hasAction( "SingleKey" ) );
	SW_EXPECT_TRUE( mapLoad.hasAction( "MoveX" ) );
	SW_EXPECT_TRUE( mapLoad.hasAction( "Move2D" ) );
	SW_EXPECT_TRUE( mapLoad.hasAction( "LookStick" ) );
	SW_EXPECT_TRUE( mapLoad.hasAction( "LookMouse" ) );
	SW_EXPECT_TRUE( mapLoad.hasAction( "ChordAction" ) );
	SW_EXPECT_TRUE( mapLoad.hasAction( "ShortcutAction" ) );
	SW_EXPECT_TRUE( mapLoad.hasAction( "AnyKeyAction" ) );

	const sw::ActionBinding* pStickBind = mapLoad.getBinding( "LookStick", 0 );
	if ( pStickBind != nullptr )
	{
		SW_EXPECT_TRUE( pStickBind->_kind == sw::BindingKind::GamepadStick2D );
		SW_EXPECT_TRUE( pStickBind->_stick == sw::GamepadStick::Right );
		SW_EXPECT_NEAR_EQUAL( 0.2f, pStickBind->_deadzone, 0.001f );
		SW_EXPECT_NEAR_EQUAL( 0.95f, pStickBind->_outerDeadzone, 0.001f );
		SW_EXPECT_NEAR_EQUAL( 1.5f, pStickBind->_responseExponent, 0.001f );
	}
	else
	{
		SW_EXPECT_NOT_NULL( pStickBind );
	}

	const sw::ActionBinding* pMouseBind = mapLoad.getBinding( "LookMouse", 0 );
	if ( pMouseBind != nullptr )
	{
		SW_EXPECT_TRUE( pMouseBind->_kind == sw::BindingKind::MouseDelta2D );
		SW_EXPECT_NEAR_EQUAL( 2.5f, pMouseBind->_scale, 0.001f );
	}
	else
	{
		SW_EXPECT_NOT_NULL( pMouseBind );
	}
}

/**
 * @brief [InputManagerTest] 마우스 EMA 스무딩 및 지수 가속 필터 검증
 */
SW_TEST_CASE( InputManagerTest, MouseSmoothingAndAcceleration )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	input.setMouseSmoothing( 0.5f );
	input.setMouseAcceleration( 2.0f );
	SW_EXPECT_NEAR_EQUAL( 0.5f, input.getMouseSmoothing(), 0.001f );
	SW_EXPECT_NEAR_EQUAL( 2.0f, input.getMouseAcceleration(), 0.001f );

	input.postRawEvent( sw::RawInputEvent::makeMouseMove( 10, 0 ) );
	input.beginFrame( 0.016f );

	float32 smoothDx{ 0.0f };
	float32 smoothDy{ 0.0f };
	input.getSmoothMouseDelta( smoothDx, smoothDy );
	SW_EXPECT_TRUE( smoothDx > 0.0f );

	input.shutdown();
}

/**
 * @brief [GamepadDeviceTest] 게임패드 배터리 상태 쿼리 검증
 */
SW_TEST_CASE( GamepadDeviceTest, BatteryInfoQuery )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	const sw::GamepadBatteryInfo batInfo = input.getGamepadBatteryInfo( 0 );
	// 비연결/가상 환경에서는 Disconnected 또는 Unknown 반환
	SW_EXPECT_TRUE( batInfo._type == sw::GamepadBatteryType::Disconnected || batInfo._type == sw::GamepadBatteryType::Unknown || batInfo._type == sw::GamepadBatteryType::Wired || batInfo._type == sw::GamepadBatteryType::Alkaline );

	input.shutdown();
}

/**
 * @brief [KeyboardDeviceTest] 128번 인덱스 이상의 넘패드 상위 키(Numpad8, NumpadEnter 등) 엣지 플래그 및 리셋 검증
 */
SW_TEST_CASE( KeyboardDeviceTest, NumpadHighIndexKeysFrameBeginReset )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	// 프레임 1: Numpad8(128), NumpadEnter(135) 누름
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Numpad8 ) );
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::NumpadEnter ) );
	input.beginFrame( 0.016f );

	SW_EXPECT_TRUE( input.isKeyDown( sw::Key::Numpad8 ) );
	SW_EXPECT_TRUE( input.wasKeyPressed( sw::Key::Numpad8 ) );
	SW_EXPECT_TRUE( input.isKeyDown( sw::Key::NumpadEnter ) );
	SW_EXPECT_TRUE( input.wasKeyPressed( sw::Key::NumpadEnter ) );

	input.endFrame();

	// 프레임 2: 키 유지 상태에서 wasKeyPressed가 정상적으로 해제되는지 검증
	input.beginFrame( 0.016f );

	SW_EXPECT_TRUE( input.isKeyDown( sw::Key::Numpad8 ) );
	SW_EXPECT_FALSE( input.wasKeyPressed( sw::Key::Numpad8 ) );
	SW_EXPECT_TRUE( input.isKeyDown( sw::Key::NumpadEnter ) );
	SW_EXPECT_FALSE( input.wasKeyPressed( sw::Key::NumpadEnter ) );

	input.endFrame();

	// 프레임 3: Numpad8 뗌
	input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::Numpad8 ) );
	input.beginFrame( 0.016f );

	SW_EXPECT_FALSE( input.isKeyDown( sw::Key::Numpad8 ) );
	SW_EXPECT_TRUE( input.wasKeyReleased( sw::Key::Numpad8 ) );
	SW_EXPECT_TRUE( input.isKeyDown( sw::Key::NumpadEnter ) );

	input.endFrame();
	input.shutdown();
}

/**
 * @brief [VirtualJoystickTest] 가상 조이스틱 영 분모 방어 및 정규화 산출 검증
 */
SW_TEST_CASE( VirtualJoystickTest, SafeDivisionAndClamping )
{
	const sw::float2 anchor{ 100.0f, 100.0f };
	// deadzone == outerDeadzone 영 분모 경계 조건
	const sw::VirtualJoystick stick( anchor, 50.0f, 0.5f, 0.5f );

	const sw::float2 zeroVec = stick.calculateVector( anchor );
	SW_EXPECT_NEAR_EQUAL( 0.0f, zeroVec._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, zeroVec._y, 0.001f );

	const sw::float2 farVec = stick.calculateVector( sw::float2{ 200.0f, 100.0f } );
	SW_EXPECT_NEAR_EQUAL( 1.0f, farVec._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, farVec._y, 0.001f );
}

/**
 * @brief [InputManagerTest] 다중 스레드 동시 이벤트 포스팅 및 락프리 드레인 스트레스 검증
 */
SW_TEST_CASE( InputManagerTest, ConcurrentMultiThreadEventPostingStress )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	constexpr uint32 kThreadCount	  = 4;
	constexpr uint32 kEventsPerThread = 2500;

	std::vector<std::thread> listThread;
	listThread.reserve( kThreadCount );

	for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
	{
		listThread.emplace_back(
			[&input, threadIndex]()
		{
			for ( uint32 eventIndex = 0; eventIndex < kEventsPerThread; ++eventIndex )
			{
				const sw::Key key = static_cast<sw::Key>( ( ( eventIndex + threadIndex ) % 100 ) + 1 );
				input.postRawEvent( sw::RawInputEvent::makeKeyDown( key ) );
			}
		} );
	}

	for ( auto& workerThread : listThread )
	{
		if ( workerThread.joinable() )
			workerThread.join();
	}

	for ( uint32 frameIndex = 0; frameIndex < 10; ++frameIndex )
	{
		input.beginFrame( 0.016f );
		input.endFrame();
	}

	SW_EXPECT_FALSE( input.isPointerInside() );
	input.shutdown();
}

/**
 * @brief [ActionMapTest] 1,000개 대량 액션 생성 및 맵 재구성 시 세대 토큰 무효화 스트레스 검증
 */
SW_TEST_CASE( ActionMapTest, GenerationalHandleStressAndMassiveActions )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap actionMap;
	actionMap.setInputManager( &input );

	constexpr uint32			  kActionCount = 1000;
	std::vector<sw::ActionHandle> listHandle;
	listHandle.reserve( kActionCount );

	for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
	{
		utf8 arrName[32]{};
		snprintf( arrName, sizeof( arrName ), "Action_A_%u", actionIndex );

		actionMap.bind( arrName, sw::InputSlot::fromKey( sw::Key::A ), sw::ActionTrigger::Pressed );
		const sw::ActionHandle handle = actionMap.getActionHandle( arrName );
		SW_EXPECT_TRUE( handle.isValid() );
		listHandle.push_back( handle );
	}

	// 맵 전체 초기화
	actionMap.clear();

	// 구버전 핸들은 모두 무효화되어야 함
	for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
	{
		SW_EXPECT_FALSE( actionMap.wasActionTriggered( listHandle[actionIndex] ) );
		SW_EXPECT_FALSE( actionMap.isActionDown( listHandle[actionIndex] ) );
	}

	// 새로운 이름의 액션 1,000개 재생성
	for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
	{
		utf8 arrName[32]{};
		snprintf( arrName, sizeof( arrName ), "Action_B_%u", actionIndex );
		actionMap.bind( arrName, sw::InputSlot::fromKey( sw::Key::B ), sw::ActionTrigger::Pressed );
	}

	// 구버전 핸들은 새 액션 슬롯과 인덱스가 겹쳐도 세대 불일치로 절대 트리거되지 않아야 함
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::B ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );

	for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
	{
		SW_EXPECT_FALSE( actionMap.wasActionTriggered( listHandle[actionIndex] ) );
		SW_EXPECT_FALSE( actionMap.isActionDown( listHandle[actionIndex] ) );
	}

	input.endFrame();
	input.shutdown();
}

/**
 * @brief [GamepadDeviceTest] 6대 아날로그 축 및 가상 트리거(100/101) 라우팅 검증
 */
SW_TEST_CASE( GamepadDeviceTest, AllAxesAndTriggerControlIndexRouting )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::GamepadDevice* pGamepad = input.getGamepad( 0 );
	if ( pGamepad != nullptr )
	{
		pGamepad->setAxis( 0, -0.75f ); // LX
		pGamepad->setAxis( 1, 0.85f );	// LY
		pGamepad->setAxis( 2, 0.50f );	// RX
		pGamepad->setAxis( 3, -0.60f ); // RY
		pGamepad->setAxis( 4, 0.90f );	// LT
		pGamepad->setAxis( 5, 0.40f );	// RT

		SW_EXPECT_NEAR_EQUAL( 0.90f, pGamepad->getControlValue( 100 ), 0.001f );  // LT
		SW_EXPECT_NEAR_EQUAL( 0.40f, pGamepad->getControlValue( 101 ), 0.001f );  // RT
		SW_EXPECT_NEAR_EQUAL( -0.75f, pGamepad->getControlValue( 102 ), 0.001f ); // LX
		SW_EXPECT_NEAR_EQUAL( 0.85f, pGamepad->getControlValue( 103 ), 0.001f );  // LY
		SW_EXPECT_NEAR_EQUAL( 0.50f, pGamepad->getControlValue( 104 ), 0.001f );  // RX
		SW_EXPECT_NEAR_EQUAL( -0.60f, pGamepad->getControlValue( 105 ), 0.001f ); // RY

		SW_EXPECT_TRUE( pGamepad->isControlDown( 100 ) );  // LT (0.90 >= default deadzone 0.5)
		SW_EXPECT_FALSE( pGamepad->isControlDown( 101 ) ); // RT (0.40 < default deadzone 0.5)
	}

	input.shutdown();
}

/**
 * @brief [KeyboardDeviceTest] 동일 프레임 내 초고속 KeyDown -> KeyUp -> KeyDown 엣지 전이 무결성 검증
 */
SW_TEST_CASE( KeyboardDeviceTest, RapidUpDownSameFrameEdgeCases )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	// 동일 프레임에 동일 키가 눌렸다 떼어지고 다시 눌림
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Z ) );
	input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::Z ) );
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Z ) );

	input.beginFrame( 0.016f );

	SW_EXPECT_TRUE( input.isKeyDown( sw::Key::Z ) );
	SW_EXPECT_TRUE( input.wasKeyPressed( sw::Key::Z ) );
	SW_EXPECT_TRUE( input.wasKeyReleased( sw::Key::Z ) );

	input.endFrame();
	input.shutdown();
}

/**
 * @brief [MouseDeviceTest] 초고속 플릭 극한 델타 및 비선형 가속 곡선 검증
 */
SW_TEST_CASE( MouseDeviceTest, ExtremeDeltaAndNonLinearAcceleration )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	input.setMouseSmoothing( 0.5f );
	input.setMouseAcceleration( 2.0f ); // 2차 거듭제곱 가속

	// 극한의 고속 이동 (+10000 픽셀 플릭)
	input.postRawEvent( sw::RawInputEvent::makeMouseMove( 10000, 5000, 10000, 5000 ) );
	input.beginFrame( 0.016f );

	float32 smoothDx{ 0.0f };
	float32 smoothDy{ 0.0f };
	input.getSmoothMouseDelta( smoothDx, smoothDy );

	SW_EXPECT_TRUE( smoothDx > 0.0f );
	SW_EXPECT_TRUE( smoothDy > 0.0f );

	input.endFrame();
	input.shutdown();
}

/**
 * @brief [ActionMapTest] 커맨드 콤보 파서(236P) 링버퍼 고속 입력 및 시퀀스 매칭 검증
 */
SW_TEST_CASE( ActionMapTest, ComboParserRingBufferOverflowStress )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::ActionMap actionMap;
	actionMap.setInputManager( &input );

	// 커맨드 구성 바인딩 (Down, DownRight, Right, P)
	actionMap.bind( "Down", sw::Key::S, sw::ActionTrigger::Pressed );
	actionMap.bind( "DownRight", sw::Key::C, sw::ActionTrigger::Pressed );
	actionMap.bind( "Right", sw::Key::D, sw::ActionTrigger::Pressed );
	actionMap.bind( "P", sw::Key::J, sw::ActionTrigger::Pressed );

	// 1단계: Down 입력
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );
	input.endFrame();

	// 2단계: DownRight 입력
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::C ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );
	input.endFrame();

	// 3단계: Right 입력
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::D ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );
	input.endFrame();

	// 4단계: P 입력
	input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::J ) );
	input.beginFrame( 0.016f );
	actionMap.update( 0.016f );
	input.endFrame();

	// 콤보 패턴 매칭 검증 (236P)
	SW_EXPECT_TRUE( actionMap.checkCommandPattern( "236P", 0.5f ) );

	input.shutdown();
}

/**
 * @brief [InputReplayTest] 입력 녹화, 프레임 스크러빙 및 결정론적 재생 검증
 */
SW_TEST_CASE( InputReplayTest, RecordingAndPlaybackWorkflow )
{
	sw::InputReplay replay;
	SW_EXPECT_FALSE( replay.isRecording() );
	SW_EXPECT_FALSE( replay.isPlaying() );

	replay.startRecording( "TestReplaySession" );
	SW_EXPECT_TRUE( replay.isRecording() );
	SW_EXPECT_EQUAL( "TestReplaySession", replay.getReplayName() );

	// 3프레임 녹화
	for ( uint32 frameIndex = 0; frameIndex < 3; ++frameIndex )
	{
		sw::InputSnapshot snapshot{};
		snapshot._tickNumber = frameIndex;
		snapshot._buttonMask = 1ull << frameIndex;

		sw::vector<sw::RawInputEvent> listEvent;
		listEvent.push_back( sw::RawInputEvent::makeKeyDown( sw::Key::A ) );

		replay.recordFrame( frameIndex, 0.016f, snapshot, listEvent );
	}

	replay.stopRecording();
	SW_EXPECT_FALSE( replay.isRecording() );
	SW_EXPECT_EQUAL( 3u, replay.getFrameCount() );
	SW_EXPECT_NEAR_EQUAL( 0.048f, replay.getTotalDuration(), 0.001f );

	// 재생 모드 진입
	replay.play();
	SW_EXPECT_TRUE( replay.isPlaying() );
	SW_EXPECT_FALSE( replay.isPaused() );
	SW_EXPECT_EQUAL( 0u, replay.getCurrentFrameIndex() );

	const sw::InputReplayFrame* pFrame0 = replay.getCurrentFrame();
	SW_EXPECT_TRUE( pFrame0 != nullptr );
	if ( pFrame0 != nullptr )
	{
		SW_EXPECT_EQUAL( 0u, pFrame0->_tickNumber );
		SW_EXPECT_EQUAL( 1ull, pFrame0->_snapshot._buttonMask );
		SW_EXPECT_EQUAL( 1u, static_cast<uint32>( pFrame0->_listRawEvent.size() ) );
	}

	// 일시정지 및 재개
	replay.pause();
	SW_EXPECT_TRUE( replay.isPaused() );
	replay.resume();
	SW_EXPECT_FALSE( replay.isPaused() );

	replay.stop();
	SW_EXPECT_FALSE( replay.isPlaying() );
}

/**
 * @brief [InputReplayTest] 1프레임 전진/후진 스텝 실행 검증
 */
SW_TEST_CASE( InputReplayTest, StepForwardAndBackward )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	sw::InputReplay replay;
	replay.startRecording( "StepSession" );

	for ( uint32 frameIndex = 0; frameIndex < 5; ++frameIndex )
	{
		sw::InputSnapshot snapshot{};
		snapshot._tickNumber = frameIndex;
		sw::vector<sw::RawInputEvent> listEvent;
		listEvent.push_back( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) );
		replay.recordFrame( frameIndex, 0.016f, snapshot, listEvent );
	}
	replay.stopRecording();

	SW_EXPECT_EQUAL( 0u, replay.getCurrentFrameIndex() );

	// 1프레임씩 전진
	replay.stepForward( &input );
	SW_EXPECT_EQUAL( 1u, replay.getCurrentFrameIndex() );

	replay.stepForward( &input );
	SW_EXPECT_EQUAL( 2u, replay.getCurrentFrameIndex() );

	// 1프레임 후진
	replay.stepBackward( &input );
	SW_EXPECT_EQUAL( 1u, replay.getCurrentFrameIndex() );

	input.shutdown();
}

/**
 * @brief [InputEdgeCaseTest] 게임패드 아날로그 트리거(LT/RT) 엣지 전이 감지 정밀 검증
 */
SW_TEST_CASE( InputEdgeCaseTest, GamepadTriggerEdgeDetection )
{
	struct TestGamepadDevice : public sw::GamepadDevice
	{
		using sw::GamepadDevice::GamepadDevice;
		void poll( [[maybe_unused]] float32 deltaTime ) override {}
	};

	TestGamepadDevice pad( 0 );

	// 초기 상태
	pad.onFrameBegin( 0.016f );
	SW_EXPECT_FALSE( pad.isControlDown( 100 ) );
	SW_EXPECT_FALSE( pad.wasControlPressed( 100 ) );
	SW_EXPECT_FALSE( pad.wasControlReleased( 100 ) );
	pad.onFrameEnd();

	// 프레임 1: 트리거 0.8f 인입 (상승 엣지)
	pad.onFrameBegin( 0.016f );
	pad.setAxis( 4, 0.8f ); // Left Trigger
	SW_EXPECT_TRUE( pad.isControlDown( 100 ) );
	SW_EXPECT_TRUE( pad.wasControlPressed( 100 ) );
	SW_EXPECT_FALSE( pad.wasControlReleased( 100 ) );
	pad.onFrameEnd();

	// 프레임 2: 계속 0.8f 유지 (누르고 있음 -> wasControlPressed는 false여야 함)
	pad.onFrameBegin( 0.016f );
	SW_EXPECT_TRUE( pad.isControlDown( 100 ) );
	SW_EXPECT_FALSE( pad.wasControlPressed( 100 ) );
	SW_EXPECT_FALSE( pad.wasControlReleased( 100 ) );
	pad.onFrameEnd();

	// 프레임 3: 트리거 0.0f로 해제 (하강 엣지)
	pad.onFrameBegin( 0.016f );
	pad.setAxis( 4, 0.0f );
	SW_EXPECT_FALSE( pad.isControlDown( 100 ) );
	SW_EXPECT_FALSE( pad.wasControlPressed( 100 ) );
	SW_EXPECT_TRUE( pad.wasControlReleased( 100 ) );
	pad.onFrameEnd();

	// 프레임 4: 뗀 상태 유지 (wasControlReleased는 false여야 함)
	pad.onFrameBegin( 0.016f );
	SW_EXPECT_FALSE( pad.isControlDown( 100 ) );
	SW_EXPECT_FALSE( pad.wasControlPressed( 100 ) );
	SW_EXPECT_FALSE( pad.wasControlReleased( 100 ) );
	pad.onFrameEnd();
}

/**
 * @brief [InputEdgeCaseTest] 리플레이 경계 조건(범위 초과 시킹, 0프레임 후진, 끝 프레임 전진, 손상된 헤더) 검증
 */
SW_TEST_CASE( InputEdgeCaseTest, ReplayBoundarySeekingAndCorruptedData )
{
	sw::InputReplay replay;

	// 빈 리플레이 상태 안전성 검증
	replay.play();
	SW_EXPECT_FALSE( replay.isPlaying() );
	replay.stepForward( nullptr );
	replay.stepBackward( nullptr );
	replay.seek( 100 );
	SW_EXPECT_EQUAL( 0u, replay.getCurrentFrameIndex() );
	SW_EXPECT_TRUE( replay.getCurrentFrame() == nullptr );

	// 프레임 3개 기록
	replay.startRecording( "BoundaryTest" );
	for ( uint32 frameIndex = 0; frameIndex < 3; ++frameIndex )
	{
		sw::InputSnapshot snapshot{};
		snapshot._tickNumber = frameIndex;
		sw::vector<sw::RawInputEvent> listEvent;
		replay.recordFrame( frameIndex, 0.016f, snapshot, listEvent );
	}
	replay.stopRecording();

	// 범위 초과 시킹 검증 (2로 클램핑)
	replay.seek( 99999 );
	SW_EXPECT_EQUAL( 2u, replay.getCurrentFrameIndex() );

	// 0번 프레임에서 stepBackward 시 언더플로 방어
	replay.seek( 0 );
	replay.stepBackward( nullptr );
	SW_EXPECT_EQUAL( 0u, replay.getCurrentFrameIndex() );

	// 손상된 파일 로드 시도
	SW_EXPECT_FALSE( replay.loadFromFile( "non_existent_file.swreplay" ) );
}

/**
 * @brief [InputStressTest] 멀티스레드 대량 원시 이벤트 동시 인입 락프리 스트레스 검증
 */
SW_TEST_CASE( InputStressTest, MultiThreadedRawEventConcurrentBurst )
{
	sw::InputManager input;
	SW_EXPECT_TRUE( input.initialize() );

	constexpr uint32 kThreadCount	  = 4;
	constexpr uint32 kEventsPerThread = 2500;

	sw::vector<std::thread> listThread;
	listThread.reserve( kThreadCount );

	for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
	{
		listThread.emplace_back( [&input]()
		{
			for ( uint32 eventIndex = 0; eventIndex < kEventsPerThread; ++eventIndex )
			{
				const sw::Key key = static_cast<sw::Key>( ( eventIndex % 26 ) + static_cast<uint32>( sw::Key::A ) );
				input.postRawEvent( sw::RawInputEvent::makeKeyDown( key ) );
				input.postRawEvent( sw::RawInputEvent::makeKeyUp( key ) );
			}
		} );
	}

	for ( std::thread& t : listThread )
	{
		if ( t.joinable() )
			t.join();
	}

	// 모든 인입된 이벤트 드레인 및 상태 정합성 검증
	input.beginFrame( 0.016f );
	input.endFrame();

	input.shutdown();
}

/**
 * @brief [InputStressTest] 대량 액션 등록 및 키 충돌 탐색/해결 스트레스 검증
 */
SW_TEST_CASE( InputStressTest, ActionMapBulkConflictResolutionStress )
{
	sw::ActionMap actionMap;

	constexpr uint32 kActionCount = 100;
	for ( uint32 index = 0; index < kActionCount; ++index )
	{
		const sw::string actionName = "StressAction_" + sw::string( std::to_string( index ).c_str() );
		actionMap.bind( actionName, sw::Key::Space );
	}

	// 100개 액션 간 충돌 해결 (Override 전략)
	for ( uint32 index = 1; index < kActionCount; ++index )
	{
		const sw::string actionName = "StressAction_" + sw::string( std::to_string( index ).c_str() );
		actionMap.rebindWithResolution( actionName, sw::InputSlot::fromKey( sw::Key::Escape ), sw::ConflictResolution::Override );
	}

	SW_EXPECT_TRUE( actionMap.hasAction( "StressAction_0" ) );
}
