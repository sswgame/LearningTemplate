#include "pch.h"

#include "Core/Concurrency/ConcurrentQueue.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/InputKeyMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Utils/VirtualJoystick.h"
#include "Engine/Input/Windows/GamepadXInput.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// InputManager 와 입력 장치 — 네이티브 이벤트가 프레임 상태(눌림/떼임 엣지)로 바뀌는 경로.
// 액션 맵 자체의 규칙은 TestActionMap.cpp, 스트레스·리플레이·경계는 TestInputRobustness.cpp.

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

    int32          mx = -1, my = -1;
    const sw::int2 vecMousePos1 = input.getMousePosition();
    mx                          = vecMousePos1._x;
    my                          = vecMousePos1._y;
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
    downEvt._wParam  = VK_SPACE;
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
#endif

#if defined( SW_PLATFORM_WINDOWS )
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

    int32          mx = 0, my = 0;
    const sw::int2 vecMousePos2 = input.getMousePosition();
    mx                          = vecMousePos2._x;
    my                          = vecMousePos2._y;
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

    int32          dx = 0, dy = 0;
    const sw::int2 vecMouseDelta3 = input.getMouseDelta();
    dx                            = vecMouseDelta3._x;
    dy                            = vecMouseDelta3._y;
    SW_EXPECT_EQUAL( 50, dx );
    SW_EXPECT_EQUAL( 30, dy );

    SW_EXPECT_FALSE( input.isMouseButtonDown( sw::MouseButton::Left ) );
    SW_EXPECT_TRUE( input.wasMouseButtonReleased( sw::MouseButton::Left ) );

    input.endFrame();
    input.shutdown();
}
#endif

#if defined( SW_PLATFORM_WINDOWS )
SW_TEST_CASE( InputManagerTest, NativeEventMouseWheelAndEdges )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    // 초기 휠 상태
    SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouseWheel(), 1e-4f );

    // 1) 휠 스크롤 위로 1틱 (+120)
    sw::NativeWindowEvent wheelUpEvt{};
    wheelUpEvt._message = WM_MOUSEWHEEL;
    wheelUpEvt._wParam  = MAKEWPARAM( 0, 120 );
    input.processNativeEvent( wheelUpEvt );

    // beginFrame 호출 시 누적 휠이 이번 프레임 델타로 전이됨
    input.beginFrame();
    SW_EXPECT_NEAR_EQUAL( 1.0f, input.getMouseWheel(), 1e-4f );

    // endFrame 호출 시 델타 0으로 리셋
    input.endFrame();
    SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouseWheel(), 1e-4f );

    // 2) 휠 스크롤 아래로 2틱 (-240)
    sw::NativeWindowEvent wheelDownEvt{};
    wheelDownEvt._message = WM_MOUSEWHEEL;
    wheelDownEvt._wParam  = MAKEWPARAM( 0, -240 );
    input.processNativeEvent( wheelDownEvt );

    input.beginFrame();
    SW_EXPECT_NEAR_EQUAL( -2.0f, input.getMouseWheel(), 1e-4f );

    input.endFrame();
    SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouseWheel(), 1e-4f );

    input.shutdown();
}
#endif

#if defined( SW_PLATFORM_WINDOWS )
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
#endif

#if defined( SW_PLATFORM_WINDOWS )
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

    sw::float2    vDiag        = actionMap.getVector2D( "Move" );
    const float32 expectedDiag = 1.0f / sw::MathUtil::sqrt( 2.0f );
    SW_EXPECT_NEAR_EQUAL( expectedDiag, vDiag._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( expectedDiag, vDiag._y, 0.001f );

    input.shutdown();
}
#endif

#if defined( SW_PLATFORM_WINDOWS )
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
#endif

#if defined( SW_PLATFORM_WINDOWS )
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
#endif

#if defined( SW_PLATFORM_WINDOWS )
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
#endif

#if defined( SW_PLATFORM_WINDOWS )
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

    float32          lx = 99.0f, ly = 99.0f;
    const sw::float2 vecLeftStick1 = pad.getLeftStick();
    lx                             = vecLeftStick1._x;
    ly                             = vecLeftStick1._y;
    SW_EXPECT_NEAR_EQUAL( 0.0f, lx, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, ly, 0.001f );

    float32          rx = 99.0f, ry = 99.0f;
    const sw::float2 vecRightStick2 = pad.getRightStick();
    rx                              = vecRightStick2._x;
    ry                              = vecRightStick2._y;
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
    SW_EXPECT_EQUAL( 0u, queue.size() );

    // 1) 5개 아이템 Push
    for ( uint16 index = 0; index < 5; ++index )
    {
        SW_EXPECT_TRUE( queue.push( sw::RawInputEvent::makeKeyDown( sw::Key::A, index ) ) );
    }
    SW_EXPECT_FALSE( queue.isEmpty() );
    SW_EXPECT_EQUAL( 5u, queue.size() );

    // 2) 2개 아이템 Pop
    sw::RawInputEvent item0{};
    SW_EXPECT_TRUE( queue.pop( item0 ) );
    SW_EXPECT_TRUE( item0._type == sw::RawInputEventType::KeyDown );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( item0._payload._keyData._nativeVirtualKey ) );

    sw::RawInputEvent item1{};
    SW_EXPECT_TRUE( queue.pop( item1 ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( item1._payload._keyData._nativeVirtualKey ) );
    SW_EXPECT_EQUAL( 3u, queue.size() );

    // 3) 나머지 3개 일괄 Drain
    sw::RawInputEvent arrDrained[8]{};
    const uint32      drainedCount = queue.drain( arrDrained, 8 );
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
    constexpr uint32                             kTotalItems = 5000;
    std::atomic<bool>                            bProducerDone{ false };

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
    uint32 consumedCount     = 0;
    uint32 nextExpectedIndex = 0;
    bool   bOrderingValid    = true;

    while ( bProducerDone.load( std::memory_order_acquire ) == false || queue.isEmpty() == false )
    {
        sw::RawInputEvent arrBatch[64]{};
        const uint32      drained = queue.drain( arrBatch, 64 );
        for ( uint32 batchIndex = 0; batchIndex < drained; ++batchIndex )
        {
            const uint32 receivedIndex = arrBatch[batchIndex]._payload._keyData._nativeVirtualKey;
            if ( receivedIndex != nextExpectedIndex )
                bOrderingValid = false;
            ++nextExpectedIndex;
            ++consumedCount;
        }
        if ( drained == 0 )
            std::this_thread::yield();
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
 * @brief [VirtualJoystickTest] 터치/가상 조이스틱 벡터 산출, 데드존, 응답 가속 검증
 */
SW_TEST_CASE( VirtualJoystickTest, CalculateVectorAndDeadzone )
{
    const sw::float2 center{ 100.0f, 100.0f };
    const float32    radius   = 50.0f;
    const float32    deadzone = 0.2f;

    const sw::float2 touchInDeadzone{ 105.0f, 100.0f };
    const sw::float2 vecDead = sw::VirtualJoystick::computeVector( center, touchInDeadzone, radius, deadzone );
    SW_EXPECT_NEAR_EQUAL( 0.0f, vecDead._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, vecDead._y, 1e-4f );

    const sw::float2 touchFarRight{ 200.0f, 100.0f };
    const sw::float2 vecFar = sw::VirtualJoystick::computeVector( center, touchFarRight, radius, deadzone );
    SW_EXPECT_NEAR_EQUAL( 1.0f, vecFar._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, vecFar._y, 1e-4f );

    const sw::float2 touchDiag{ 150.0f, 150.0f };
    const sw::float2 vecDiag = sw::VirtualJoystick::computeVector( center, touchDiag, radius, 0.0f );
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

    SW_EXPECT_TRUE( input.getMouse()->getLockMode() == sw::MouseLockMode::None );

    input.setMouseLockMode( sw::MouseLockMode::ConfinedToWindow );
    SW_EXPECT_TRUE( input.getMouse()->getLockMode() == sw::MouseLockMode::ConfinedToWindow );

    input.setMouseLockMode( sw::MouseLockMode::LockedInCenter );
    SW_EXPECT_TRUE( input.getMouse()->getLockMode() == sw::MouseLockMode::LockedInCenter );

    input.setMouseClipSubRect( 10, 20, 300, 400 );
    int32 subX = 0, subY = 0, subW = 0, subH = 0;
    SW_EXPECT_TRUE( input.getMouse()->getClipSubRect( subX, subY, subW, subH ) );
    SW_EXPECT_EQUAL( 10, subX );
    SW_EXPECT_EQUAL( 20, subY );
    SW_EXPECT_EQUAL( 300, subW );
    SW_EXPECT_EQUAL( 400, subH );

    input.clearMouseClipSubRect();
    SW_EXPECT_FALSE( input.getMouse()->getClipSubRect( subX, subY, subW, subH ) );

    input.shutdown();
}

/**
 * @brief [InputManagerTest] 수평 틸트 휠(Horizontal Wheel) 이벤트 및 델타 누적 검증
 */
SW_TEST_CASE( InputManagerTest, MouseWheelHorizontal )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouse()->getMouseWheelHorizontal(), 1e-4f );

    input.postRawEvent( sw::RawInputEvent::makeMouseHorizontalWheel( 1.5f ) );
    input.beginFrame( 0.016f );

    SW_EXPECT_NEAR_EQUAL( 1.5f, input.getMouse()->getMouseWheelHorizontal(), 1e-4f );

    input.endFrame();
    SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouse()->getMouseWheelHorizontal(), 1e-4f );

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
 * @brief [InputManagerTest] 입력 뮤트(Mute) 및 스냅샷 기록 검증
 * @details 예전엔 `InputManager::injectSnapshot` 으로 히스토리에 스냅샷을 직접 밀어 넣었다. 그 함수는
 *          부르는 곳이 이 테스트뿐인 백도어였고, 정작 런타임이 쓰는 `recordSnapshot` 경로는 아무도
 *          검사하지 않았다. 이제 실제 경로로 — 버튼을 눌러 프레임을 돌리고 기록시켜 — 확인한다.
 */
SW_TEST_CASE( InputManagerTest, InputMutingAndSnapshotRecording )
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

    // 뮤트를 풀면 같은 경로가 다시 상태를 만든다 — 마우스 버튼을 눌러 한 프레임 돌린다.
    input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( input.isMouseButtonDown( sw::MouseButton::Left ) );

    input.recordSnapshot( 200 );

    const sw::InputSnapshot* pRecorded = input.getSnapshot( 200 );
    SW_EXPECT_TRUE( pRecorded != nullptr );
    if ( pRecorded != nullptr )
    {
        // 버튼 마스크는 게임패드가 0..15, 마우스가 16.. 이다 (MouseButton::Left = 0 → 비트 16).
        SW_EXPECT_EQUAL( 1ULL << 16, pRecorded->_buttonMask );
        SW_EXPECT_EQUAL( 200u, pRecorded->_tickNumber );
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
 * @brief [InputManagerTest] 마우스 EMA 스무딩 및 지수 가속 필터 검증
 */
SW_TEST_CASE( InputManagerTest, MouseSmoothingAndAcceleration )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    input.getMouse()->setSmoothing( 0.5f );
    input.getMouse()->setAcceleration( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, input.getMouse()->getSmoothing(), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, input.getMouse()->getAcceleration(), 0.001f );

    input.postRawEvent( sw::RawInputEvent::makeMouseMove( 10, 0 ) );
    input.beginFrame( 0.016f );

    const sw::float2 vecSmoothDelta3 = input.getMouse()->getSmoothDelta();
    SW_EXPECT_TRUE( vecSmoothDelta3._x > 0.0f );

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

    const sw::float2 zeroVec = stick.computeVector( anchor );
    SW_EXPECT_NEAR_EQUAL( 0.0f, zeroVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, zeroVec._y, 0.001f );

    const sw::float2 farVec = stick.computeVector( sw::float2{ 200.0f, 100.0f } );
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

    constexpr uint32 kThreadCount     = 4;
    constexpr uint32 kEventsPerThread = 2500;

    sw::vector<std::thread> listThread;
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

    SW_EXPECT_FALSE( input.getMouse()->isPointerInside() );
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
        pGamepad->setAxis( 1, 0.85f );  // LY
        pGamepad->setAxis( 2, 0.50f );  // RX
        pGamepad->setAxis( 3, -0.60f ); // RY
        pGamepad->setAxis( 4, 0.90f );  // LT
        pGamepad->setAxis( 5, 0.40f );  // RT

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

    input.getMouse()->setSmoothing( 0.5f );
    input.getMouse()->setAcceleration( 2.0f ); // 2차 거듭제곱 가속

    // 극한의 고속 이동 (+10000 픽셀 플릭)
    input.postRawEvent( sw::RawInputEvent::makeMouseMove( 10000, 5000, 10000, 5000 ) );
    input.beginFrame( 0.016f );

    float32          smoothDx{ 0.0f };
    float32          smoothDy{ 0.0f };
    const sw::float2 vecSmoothDelta4 = input.getMouse()->getSmoothDelta();
    smoothDx                         = vecSmoothDelta4._x;
    smoothDy                         = vecSmoothDelta4._y;

    SW_EXPECT_TRUE( smoothDx > 0.0f );
    SW_EXPECT_TRUE( smoothDy > 0.0f );

    input.endFrame();
    input.shutdown();
}

/**
 * @brief [InputManagerTest] 마우스를 멈추면 스무딩 델타가 **0 으로 돌아온다**
 * @details `MouseDevice::poll()` 이 하는 일이 이것 하나인데 **테스트가 없었다** — `poll` 의 스무딩
 *          갱신을 통째로 지워도 이 스위트가 전부 초록이었다(2026-09-19 변이로 확인).
 *
 *          없으면 어떻게 되는가: 스무딩 델타는 마지막 입력 이벤트가 넣은 값에서 **멈추지 않는다.**
 *          마우스를 놓아도 `MouseDevice::getSmoothDelta()` 가 계속 같은 값을 보고하고, 그 값으로 시점을 도는
 *          쪽은 **손을 뗐는데도 계속 돈다.** 로그에는 아무것도 남지 않는다.
 *
 * @note 스무딩과 가속을 끄고 본다 — EMA 가 걸려 있으면 0 에 점근할 뿐 정확히 0 이 되지 않아
 *       "돌아왔다" 를 단언할 수 없다. 여기서 보는 것은 필터의 모양이 아니라 **poll 이 프레임마다
 *       위치 차이를 흘려 넣는다**는 사실이다.
 */
SW_TEST_CASE( InputManagerTest, SmoothMouseDeltaReturnsToZeroWhenMouseStops )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );

    input.getMouse()->setSmoothing( 0.0f );
    input.getMouse()->setAcceleration( 1.0f );

    // 1프레임: 마우스가 x 로 10 움직인다.
    input.postRawEvent( sw::RawInputEvent::makeMouseMove( 10, 0 ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, input.getMouse()->getSmoothDelta()._x, 0.001f );

    // 2프레임: 이벤트가 없다. onFrameBegin 이 이번 프레임의 위치 차이(10)를 세고 poll 이 흘려 넣는다.
    input.beginFrame( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, input.getMouse()->getSmoothDelta()._x, 0.001f );

    // 3프레임: 여전히 이벤트가 없고 위치도 그대로다 — 위치 차이가 0 이므로 델타도 0 이어야 한다.
    input.beginFrame( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouse()->getSmoothDelta()._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouse()->getSmoothDelta()._y, 0.001f );

    input.shutdown();
}
