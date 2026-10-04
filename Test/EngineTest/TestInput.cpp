#include "pch.h"

#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/InputKeyMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/Utils/VirtualJoystick.h"
#include "Engine/Input/Windows/XInputGamepadDevice.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// InputManager 와 입력 장치 — 네이티브 이벤트가 프레임 상태(눌림/떼임 엣지)로 바뀌는 경로.

namespace
{
    // 모듈 코드 정리 시험이 다는 콜백들이다. 몸통을 서로 다르게 둔다 — 같으면 링커가 접어 스텁 주소가 겹칠 수 있다.
    int32 s_inputProbeValue{ 0 };

    void onProbeActiveDeviceChanged( sw::InputGlyphStyle )
    {
        s_inputProbeValue += 1;
    }

    void onProbeGamepadConnection( uint32, bool )
    {
        s_inputProbeValue += 10;
    }

    void onProbeTextInput( sw::string_view )
    {
        s_inputProbeValue += 100;
    }

    void onProbeTextComposition( sw::string_view )
    {
        s_inputProbeValue += 1000;
    }

    /** @brief 마우스 장치 하나를 더 꽂는 자리 — 모듈이 등록한 장치처럼 vtable 이 이 실행 파일에 있다. */
    class SecondMouseDevice final : public sw::MouseDevice
    {
    };

    /** @brief 모듈이 등록한 장치 자리 — vtable 이 이 실행 파일에 있다. */
    class ProbeInputDevice final : public sw::IInputDevice
    {
    public:
        sw::InputDeviceKind getDeviceKind() const override { return sw::InputDeviceKind::Custom; }
        sw::string_view     getDeviceName() const override { return "Probe"; }
        void                poll( float32 ) override {}
        void                onFrameBegin( float32 ) override {}
        void                onFrameEnd() override {}
        void                resetState() override {}
        bool                isControlDown( uint16 ) const override { return false; }
        bool                wasControlPressed( uint16 ) const override { return false; }
        bool                wasControlReleased( uint16 ) const override { return false; }
    };

    /**
     * @brief 프레임마다 같은 총 이동(@p frameMoveX)을 @p eventsPerFrame 개의 원시 이벤트로 나눠 넣고, 프레임마다 스무딩 델타 X 를 적습니다.
     * @details 앱 루프와 같은 순서(beginFrame → 조회 → endFrame)로 돈다.
     */
    void runSmoothedMouse( float32 smoothing, float32 acceleration, float32 frameSeconds, int32 frameCount, float32 frameMoveX, int32 eventsPerFrame,
                           sw::vector<float32>& outListSmoothX )
    {
        sw::InputManager input;
        SW_ASSERT_TRUE( input.initialize() );
        input.getMouse()->setSmoothing( smoothing );
        input.getMouse()->setAcceleration( acceleration );

        outListSmoothX.clear();
        const float32 eventMoveX = frameMoveX / static_cast<float32>( eventsPerFrame );
        for ( int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            for ( int32 eventIndex = 0; eventIndex < eventsPerFrame; ++eventIndex )
            {
                SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeMouseRawDelta( eventMoveX, 0.0f ) ) );
            }
            input.beginFrame( frameSeconds );
            outListSmoothX.push_back( input.getMouse()->getSmoothDelta()._x );
            input.endFrame();
        }
        input.shutdown();
    }

    /** @brief 키보드 포커스 주인마다 받은 글자를 따로 모읍니다. */
    struct FocusTextRecorder
    {
        sw::string _game;
        sw::string _console;

        void onGameText( sw::string_view text ) { _game.append( text.data(), text.size() ); }
        void onConsoleText( sw::string_view text ) { _console.append( text.data(), text.size() ); }
    };

    void runInputFrame( sw::InputManager& input, std::initializer_list<sw::RawInputEvent> listEvent )
    {
        for ( const sw::RawInputEvent& event : listEvent )
        {
            (void)input.postRawEvent( event );
        }
        input.beginFrame( 0.016f );
    }
} // namespace
// 액션 맵 자체의 규칙은 TestInputMap.cpp, 스트레스·리플레이·경계는 TestInputRobustness.cpp.

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

    // 앱 루프와 같은 순서로 돈다: 메시지 펌프(processNativeEvent) → beginFrame → 게임플레이 조회 → endFrame.
    // processNativeEvent 직후 바로 물으면 실제 루프에서 beginFrame 이 엣지를 지우고 이벤트를 재생하며 "새로 눌림" 이 사라지는
    // 경우를 볼 수 없다.

    // 프레임 1: Space KeyDown 이벤트 수신
    sw::NativeWindowEvent downEvt{};
    downEvt._message = WM_KEYDOWN;
    downEvt._wParam  = VK_SPACE;
    input.processNativeEvent( downEvt );
    input.beginFrame( 0.016f );

    SW_EXPECT_TRUE( input.isKeyDown( sw::Key::Space ) );
    SW_EXPECT_TRUE( input.wasKeyPressed( sw::Key::Space ) );
    SW_EXPECT_FALSE( input.wasKeyReleased( sw::Key::Space ) );
    input.endFrame();

    // 프레임 2: 키 유지 중(이벤트 없음)
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( input.isKeyDown( sw::Key::Space ) );
    SW_EXPECT_FALSE( input.wasKeyPressed( sw::Key::Space ) );
    SW_EXPECT_FALSE( input.wasKeyReleased( sw::Key::Space ) );
    input.endFrame();

    // 프레임 3: Space KeyUp 이벤트 수신
    sw::NativeWindowEvent upEvt{};
    upEvt._message = WM_KEYUP;
    upEvt._wParam  = VK_SPACE;
    input.processNativeEvent( upEvt );
    input.beginFrame( 0.016f );

    SW_EXPECT_FALSE( input.isKeyDown( sw::Key::Space ) );
    SW_EXPECT_FALSE( input.wasKeyPressed( sw::Key::Space ) );
    SW_EXPECT_TRUE( input.wasKeyReleased( sw::Key::Space ) );
    input.endFrame();

    // 프레임 4: 뗀 상태 유지
    input.beginFrame( 0.016f );
    SW_EXPECT_FALSE( input.isKeyDown( sw::Key::Space ) );
    SW_EXPECT_FALSE( input.wasKeyPressed( sw::Key::Space ) );
    SW_EXPECT_FALSE( input.wasKeyReleased( sw::Key::Space ) );
    input.endFrame();

    input.shutdown();
}
#endif

#if defined( SW_PLATFORM_WINDOWS )
SW_TEST_CASE( InputManagerTest, NativeEventMouseMovementAndDelta )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    // 프레임 1: 마우스 이동 및 좌클릭 다운(버튼 메시지도 lParam 에 좌표를 싣는다)
    sw::NativeWindowEvent moveEvt1{};
    moveEvt1._message = WM_MOUSEMOVE;
    moveEvt1._lParam  = MAKELPARAM( 100, 200 );
    input.processNativeEvent( moveEvt1 );

    sw::NativeWindowEvent clickEvt{};
    clickEvt._message = WM_LBUTTONDOWN;
    clickEvt._lParam  = MAKELPARAM( 100, 200 );
    input.processNativeEvent( clickEvt );
    input.beginFrame( 0.016f );

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
    releaseEvt._lParam  = MAKELPARAM( 150, 230 );
    input.processNativeEvent( releaseEvt );
    input.beginFrame( 0.016f );

    // 델타는 이번 프레임에 들어온 이동의 합이다. 메시지를 받을 때 위치를 바로 바꿔 두고 beginFrame 이 같은 이동을
    // 한 번 더 재생하면 실제 루프에서는 움직이는 동안 델타가 늘 0 이다.
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
    input.beginFrame( 0.016f );

    SW_EXPECT_TRUE( input.isMouseButtonDown( sw::MouseButton::Right ) );
    SW_EXPECT_TRUE( input.wasMouseButtonPressed( sw::MouseButton::Right ) );
    SW_EXPECT_FALSE( input.wasMouseButtonReleased( sw::MouseButton::Right ) );
    input.endFrame();

    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( input.isMouseButtonDown( sw::MouseButton::Right ) );
    SW_EXPECT_FALSE( input.wasMouseButtonPressed( sw::MouseButton::Right ) );
    input.endFrame();

    // 2) 우클릭 업
    sw::NativeWindowEvent rUp{};
    rUp._message = WM_RBUTTONUP;
    input.processNativeEvent( rUp );
    input.beginFrame( 0.016f );

    SW_EXPECT_FALSE( input.isMouseButtonDown( sw::MouseButton::Right ) );
    SW_EXPECT_TRUE( input.wasMouseButtonReleased( sw::MouseButton::Right ) );
    input.endFrame();

    // 3) 휠(중간) 클릭 다운 및 업
    sw::NativeWindowEvent mDown{};
    mDown._message = WM_MBUTTONDOWN;
    input.processNativeEvent( mDown );
    input.beginFrame( 0.016f );

    SW_EXPECT_TRUE( input.isMouseButtonDown( sw::MouseButton::Middle ) );
    SW_EXPECT_TRUE( input.wasMouseButtonPressed( sw::MouseButton::Middle ) );
    input.endFrame();

    sw::NativeWindowEvent mUp{};
    mUp._message = WM_MBUTTONUP;
    input.processNativeEvent( mUp );
    input.beginFrame( 0.016f );

    SW_EXPECT_FALSE( input.isMouseButtonDown( sw::MouseButton::Middle ) );
    SW_EXPECT_TRUE( input.wasMouseButtonReleased( sw::MouseButton::Middle ) );

    input.endFrame();
    input.shutdown();
}
#endif

#if defined( SW_PLATFORM_WINDOWS )
SW_TEST_CASE( InputManagerTest, InputMapVector2DMovement )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap inputMap;
    inputMap.setInputManager( &input );
    inputMap.bindVector2D( "Move", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D, 0.1f );

    // 1) 아무 키도 안 눌렸을 때 -> (0, 0)
    sw::float2 v0 = inputMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, v0._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, v0._y, 0.0001f );

    // 2) W 키(상향) 입력 -> (0, 1)
    sw::NativeWindowEvent wDown{};
    wDown._message = WM_KEYDOWN;
    wDown._wParam  = 'W';
    input.processNativeEvent( wDown );
    input.beginFrame( 0.016f );

    sw::float2 vUp = inputMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, vUp._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, vUp._y, 0.0001f );

    // 3) W + D(우상향 대각선) 입력 -> 정규화되어 (1/sqrt(2), 1/sqrt(2)) ~= (0.7071f, 0.7071f)
    sw::NativeWindowEvent dDown{};
    dDown._message = WM_KEYDOWN;
    dDown._wParam  = 'D';
    input.processNativeEvent( dDown );
    input.beginFrame( 0.016f );

    sw::float2    vDiag        = inputMap.getVector2D( "Move" );
    const float32 expectedDiag = 1.0f / sw::MathUtil::sqrt( 2.0f );
    SW_EXPECT_NEAR_EQUAL( expectedDiag, vDiag._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( expectedDiag, vDiag._y, 0.001f );

    input.shutdown();
}
#endif

#if defined( SW_PLATFORM_WINDOWS )
SW_TEST_CASE( InputManagerTest, InputMapChordedActions )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap inputMap;
    inputMap.setInputManager( &input );
    inputMap.bindChord( "QuickSave", sw::Key::LeftControl, sw::Key::S );

    input.endFrame();

    // 1) 초기 상태 -> false
    SW_EXPECT_FALSE( inputMap.isChordDown( "QuickSave" ) );
    SW_EXPECT_FALSE( inputMap.wasChordTriggered( "QuickSave" ) );

    // 2) LeftControl만 눌림 -> false
    sw::NativeWindowEvent ctrlDown{};
    ctrlDown._message = WM_KEYDOWN;
    ctrlDown._wParam  = VK_LCONTROL;
    input.processNativeEvent( ctrlDown );
    input.beginFrame( 0.016f );

    SW_EXPECT_FALSE( inputMap.isChordDown( "QuickSave" ) );
    SW_EXPECT_FALSE( inputMap.wasChordTriggered( "QuickSave" ) );

    // 3) S 키 눌림 -> chord 발화!
    sw::NativeWindowEvent sDown{};
    sDown._message = WM_KEYDOWN;
    sDown._wParam  = 'S';
    input.processNativeEvent( sDown );
    input.endFrame();
    input.beginFrame( 0.016f );

    SW_EXPECT_TRUE( inputMap.isChordDown( "QuickSave" ) );
    SW_EXPECT_TRUE( inputMap.wasChordTriggered( "QuickSave" ) );

    // 4) 다음 프레임 -> isDown은 true, wasTriggered는 false
    input.endFrame();
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.isChordDown( "QuickSave" ) );
    SW_EXPECT_FALSE( inputMap.wasChordTriggered( "QuickSave" ) );

    input.shutdown();
}
#endif

#if defined( SW_PLATFORM_WINDOWS )
SW_TEST_CASE( InputManagerTest, InputMapGamepadStick2D )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap inputMap;
    inputMap.setInputManager( &input );
    inputMap.bindGamepadStick2D( "Look", sw::GamepadStick::Right, 0.15f );

    // 기본 상태 -> (0, 0)
    sw::float2 v0 = inputMap.getVector2D( "Look" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, v0._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, v0._y, 0.0001f );

    input.shutdown();
}
#endif

#if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief [InputManagerTest] GamepadButtonUtil 이름 변환 및 매핑 양방향 검증
 */
SW_TEST_CASE( InputManagerTest, GamepadButtonUtilNameMapping )
{
    // 1) 이름 -> 버튼 열거형 (대소문자 무시)
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "A" ) == sw::GamepadButton::A );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "a" ) == sw::GamepadButton::A );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "B" ) == sw::GamepadButton::B );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "X" ) == sw::GamepadButton::X );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "Y" ) == sw::GamepadButton::Y );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "DPadUp" ) == sw::GamepadButton::DPadUp );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "dpadup" ) == sw::GamepadButton::DPadUp );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "DPadDown" ) == sw::GamepadButton::DPadDown );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "DPadLeft" ) == sw::GamepadButton::DPadLeft );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "DPadRight" ) == sw::GamepadButton::DPadRight );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "Start" ) == sw::GamepadButton::Start );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "Back" ) == sw::GamepadButton::Back );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "LeftShoulder" ) == sw::GamepadButton::LeftShoulder );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "RightShoulder" ) == sw::GamepadButton::RightShoulder );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "LeftThumb" ) == sw::GamepadButton::LeftThumb );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "RightThumb" ) == sw::GamepadButton::RightThumb );

    // 알 수 없는 버튼 이름은 Count 반환
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "InvalidButton" ) == sw::GamepadButton::Count );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "" ) == sw::GamepadButton::Count );

    // 2) 버튼 열거형 -> 안정 문자열
    SW_EXPECT_STREQ( "A", sw::GamepadButtonUtil::toName( sw::GamepadButton::A ) );
    SW_EXPECT_STREQ( "B", sw::GamepadButtonUtil::toName( sw::GamepadButton::B ) );
    SW_EXPECT_STREQ( "X", sw::GamepadButtonUtil::toName( sw::GamepadButton::X ) );
    SW_EXPECT_STREQ( "Y", sw::GamepadButtonUtil::toName( sw::GamepadButton::Y ) );
    SW_EXPECT_STREQ( "DPadUp", sw::GamepadButtonUtil::toName( sw::GamepadButton::DPadUp ) );
    SW_EXPECT_STREQ( "Start", sw::GamepadButtonUtil::toName( sw::GamepadButton::Start ) );
    SW_EXPECT_STREQ( "Back", sw::GamepadButtonUtil::toName( sw::GamepadButton::Back ) );
    SW_EXPECT_STREQ( "LeftShoulder", sw::GamepadButtonUtil::toName( sw::GamepadButton::LeftShoulder ) );
    SW_EXPECT_STREQ( "RightShoulder", sw::GamepadButtonUtil::toName( sw::GamepadButton::RightShoulder ) );
    SW_EXPECT_STREQ( "LeftThumb", sw::GamepadButtonUtil::toName( sw::GamepadButton::LeftThumb ) );
    SW_EXPECT_STREQ( "RightThumb", sw::GamepadButtonUtil::toName( sw::GamepadButton::RightThumb ) );

    // Count / 범위 밖은 nullptr 반환
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::toName( sw::GamepadButton::Count ) == nullptr );
}
#endif

#if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief [InputManagerTest] XInputGamepadDevice 기본 상태 및 스틱 데드존 검증
 */
SW_TEST_CASE( InputManagerTest, XInputGamepadDeviceDefaultStateAndStickQuery )
{
    sw::XInputGamepadDevice pad;
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
 * @brief [GamepadDeviceTest] GamepadButtonUtil::fromName 및 toName 크로스플랫폼 안정성 검증
 */
SW_TEST_CASE( GamepadDeviceTest, GamepadButtonUtilFromNameAndToName )
{
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "A" ) == sw::GamepadButton::A );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "DPadUp" ) == sw::GamepadButton::DPadUp );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "RightTrigger" ) == sw::GamepadButton::Count );
    SW_EXPECT_TRUE( sw::GamepadButtonUtil::fromName( "NonExistent" ) == sw::GamepadButton::Count );

    const utf8* pNameA = sw::GamepadButtonUtil::toName( sw::GamepadButton::A );
    SW_EXPECT_TRUE( pNameA != nullptr && sw::StringUtil::equals( pNameA, "A" ) );

    const utf8* pNameStart = sw::GamepadButtonUtil::toName( sw::GamepadButton::Start );
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
 * @details 런타임이 쓰는 `recordSnapshot` 경로로 — 버튼을 눌러 프레임을 돌리고 기록시켜 — 확인한다. 히스토리에
 *          스냅샷을 직접 밀어 넣는 백도어로는 실제 경로를 검사하지 못한다.
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

    const sw::GamepadBatteryInfo batteryInfo = input.getGamepadBatteryInfo( 0 );
    // 비연결/가상 환경에서는 Disconnected 또는 Unknown 반환
    SW_EXPECT_TRUE( batteryInfo._type == sw::GamepadBatteryType::Disconnected || batteryInfo._type == sw::GamepadBatteryType::Unknown || batteryInfo._type == sw::GamepadBatteryType::Wired || batteryInfo._type == sw::GamepadBatteryType::Alkaline );

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
 * @details `MouseDevice::poll()` 이 하는 일이 이것 하나다 — 이 케이스가 없으면 `poll` 의 스무딩 갱신을 통째로
 *          지워도 이 스위트가 전부 초록이다.
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

/**
 * @brief [MouseDeviceTest] 같은 이동을 125 Hz · 1000 Hz 이벤트로 나눠 넣어도 스무딩 델타가 같다
 * @details 스무딩과 가속은 프레임에 모인 이동에 한 번 걸린다(`MouseDevice::onEventsDispatched`). 이벤트마다 걸면 1000 Hz 마우스(프레임당
 *          이벤트 16 개)가 125 Hz(2 개)보다 훨씬 빨리 따라와 같은 설정이 마우스마다 다른 감각이 된다.
 */
SW_TEST_CASE( MouseDeviceTest, SmoothingIsIndependentOfPollingRate )
{
    constexpr float32 kFrameSeconds = 1.0f / 60.0f;
    constexpr int32   kFrameCount   = 12;

    sw::vector<float32> listSlowPoll;
    sw::vector<float32> listFastPoll;
    runSmoothedMouse( 0.8f, 1.5f, kFrameSeconds, kFrameCount, 32.0f, 2, listSlowPoll );  // 125 Hz
    runSmoothedMouse( 0.8f, 1.5f, kFrameSeconds, kFrameCount, 32.0f, 16, listFastPoll ); // 1000 Hz
    SW_ASSERT_EQUAL( listSlowPoll.size(), listFastPoll.size() );
    for ( size_t frameIndex = 0; frameIndex < listSlowPoll.size(); ++frameIndex )
    {
        SW_EXPECT_NEAR_EQUAL( listSlowPoll[frameIndex], listFastPoll[frameIndex], 0.01f );
    }

    // 가속 없이 스무딩만 걸면 첫 프레임은 1 - 0.8 = 20 % 를 따라온다(60 Hz 한 프레임이 설정값의 기준).
    sw::vector<float32> listNoAcceleration;
    runSmoothedMouse( 0.8f, 1.0f, kFrameSeconds, 1, 32.0f, 16, listNoAcceleration );
    SW_EXPECT_NEAR_EQUAL( 32.0f * 0.2f, listNoAcceleration[0], 0.01f );
}

/**
 * @brief [MouseDeviceTest] 30 fps 와 144 fps 에서 같은 시간이 지나면 스무딩이 같은 만큼 따라온다
 * @details 계수는 프레임마다 1 - exp( -dt / τ ) 다. 프레임 수로 세면 144 fps 가 30 fps 보다 거의 다섯 배 빨리 따라와, 프레임 레이트가
 *          감각을 바꾼다. 일정한 속도로 1/6 초를 움직인 뒤 "스무딩 델타 / 그 프레임의 실제 이동" 이 둘 다 1 - factor^( 1/6 초 ÷ 1/60 초 ) 여야 한다.
 */
SW_TEST_CASE( MouseDeviceTest, SmoothingFollowsElapsedTimeNotFrameCount )
{
    constexpr float32 kSmoothing     = 0.9f;
    constexpr float32 kSpeed         = 600.0f; // 픽셀/초
    const float32     expectedFollow = 1.0f - sw::MathUtil::pow( kSmoothing, 10.0f );

    sw::vector<float32> listLowRate;
    sw::vector<float32> listHighRate;
    runSmoothedMouse( kSmoothing, 1.0f, 1.0f / 30.0f, 5, kSpeed / 30.0f, 1, listLowRate );     // 1/6 초 = 30 fps 5 프레임
    runSmoothedMouse( kSmoothing, 1.0f, 1.0f / 144.0f, 24, kSpeed / 144.0f, 1, listHighRate ); // 1/6 초 = 144 fps 24 프레임
    SW_ASSERT_EQUAL( size_t( 5 ), listLowRate.size() );
    SW_ASSERT_EQUAL( size_t( 24 ), listHighRate.size() );

    const float32 lowRateFollow  = listLowRate.back() / ( kSpeed / 30.0f );
    const float32 highRateFollow = listHighRate.back() / ( kSpeed / 144.0f );
    SW_EXPECT_NEAR_EQUAL( expectedFollow, lowRateFollow, 0.001f );
    SW_EXPECT_NEAR_EQUAL( expectedFollow, highRateFollow, 0.001f );

    // 설정값의 시간 상수: τ = -(1/60 초) / ln( factor ).
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getMouse()->setSmoothing( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.024045f, input.getMouse()->getSmoothingTimeConstant(), 0.00001f );
    input.getMouse()->setSmoothing( 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, input.getMouse()->getSmoothingTimeConstant(), 0.00001f );
    input.shutdown();
}

#if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief [InputManagerTest] 포커스를 잃기 직전에 들어온 키 누름이 리셋 뒤에 되살아나지 않는다.
 * @details 한 번의 메시지 펌프 안에서 자동 반복 KeyDown(W) 이 먼저 오고 그 뒤에 WM_KILLFOCUS 가 온다(알트탭). 뗌 메시지는 다른
 *          창으로 간다. 포커스 잃음이 메시지를 받는 즉시 장치를 리셋하면 그보다 먼저 큐에 들어간 KeyDown 이 다음
 *          beginFrame 에 재생되어 W 가 눌린 채 남는다 — 캐릭터가 배경에서, 돌아온 뒤에도 계속 달린다.
 */
SW_TEST_CASE( InputManagerTest, FocusLossAfterQueuedKeyDownLeavesNoKeyStuck )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );

    sw::NativeWindowEvent wDown{};
    wDown._message = WM_KEYDOWN;
    wDown._wParam  = 'W';
    input.processNativeEvent( wDown );
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( input.isKeyDown( sw::Key::W ) );
    input.endFrame();

    sw::NativeWindowEvent wRepeat{};
    wRepeat._message = WM_KEYDOWN;
    wRepeat._wParam  = 'W';
    wRepeat._lParam  = 0x40000000; // 이전에도 눌려 있었다(자동 반복)
    input.processNativeEvent( wRepeat );

    sw::NativeWindowEvent killFocus{};
    killFocus._message = WM_KILLFOCUS;
    input.processNativeEvent( killFocus );

    input.beginFrame( 0.016f );
    SW_EXPECT_FALSE_MSG( input.isKeyDown( sw::Key::W ), "포커스를 잃은 뒤에도 W 가 눌린 채 남았습니다" );
    input.endFrame();

    input.shutdown();
}
#endif

/**
 * @brief [RawInputEventTest] 글자 페이로드는 UTF-8 글자 경계에서 자른다.
 * @details 칸은 31 바이트다. 한글은 글자당 3 바이트라 11 글자(33 바이트)면 10 글자(30 바이트)까지만 담아야 한다. 31 바이트에서
 *          그냥 자르면 마지막 글자의 앞 바이트 하나가 남아 받는 쪽(IME 조합 표시)이 깨진 UTF-8 을 받는다.
 */
SW_TEST_CASE( RawInputEventTest, TextPayloadTruncatesAtUtf8Boundary )
{
    const sw::string_view   text  = "가나다라마바사아자차카";
    const sw::RawInputEvent event = sw::RawInputEvent::makeTextComposition( text );
    const sw::string_view   payload( event._payload._textData._arrUtf8 );
    SW_EXPECT_EQUAL( 30u, static_cast<uint32>( payload.size() ) );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( event._payload._textData._arrUtf8 ) );

    // 담을 수 있는 길이는 그대로 담는다.
    const sw::RawInputEvent shortEvent = sw::RawInputEvent::makeTextInput( "abc" );
    SW_EXPECT_TRUE( sw::string_view( shortEvent._payload._textData._arrUtf8 ) == "abc" );
}

/**
 * @brief [InputManagerTest] 모듈 이미지를 내리기 전의 정리는 입력 관리자의 콜백 넷 · 모듈이 등록한 장치를 뗀다
 * @details 입력 관리자는 모듈보다 오래 산다. 모듈이 단 콜백(장치 변경 · 게임패드 연결 · 글자 입력 · 조합)과 모듈이 등록한 장치(vtable 이
 *          그 이미지)는 이미지를 내린 뒤 부르면 내려간 코드로 뛴다. 범위는 콜백 스텁 · 장치 vtable 하나씩으로 좁힌다 — 배포 구성은 엔진까지 한 실행
 *          파일이라 이미지 통째로 주면 엔진의 장치까지 내린다. 범위 밖(엔진이 단 게임패드 슬롯의 연결 콜백 · 엔진 장치)은 그대로 남아야 한다.
 */
SW_TEST_CASE( InputManagerTest, ReleaseModuleCodeDropsTheCallbacksAndDevicesOfTheImage )
{
    const uint32     listenerCountBefore = sw::IModuleUnloadListener::getListenerCount();
    sw::InputManager input;
    SW_EXPECT_EQUAL( listenerCountBefore + 1, sw::IModuleUnloadListener::getListenerCount() );
    SW_ASSERT_TRUE( input.initialize() );
    SW_ASSERT_NOT_NULL( input.getKeyboard() );

    const sw::InputManager::ActiveDeviceChangedDelegate onActive      = SW_DELEGATE_FUNCTION( sw::InputManager::ActiveDeviceChangedDelegate, onProbeActiveDeviceChanged );
    const sw::InputManager::GamepadConnectionDelegate   onConnection  = SW_DELEGATE_FUNCTION( sw::InputManager::GamepadConnectionDelegate, onProbeGamepadConnection );
    const sw::InputManager::TextInputDelegate           onText        = SW_DELEGATE_FUNCTION( sw::InputManager::TextInputDelegate, onProbeTextInput );
    const sw::InputManager::TextInputDelegate           onComposition = SW_DELEGATE_FUNCTION( sw::InputManager::TextInputDelegate, onProbeTextComposition );
    input.setActiveDeviceChangedCallback( onActive );
    input.setGamepadConnectionCallback( onConnection );
    input.setTextInputCallback( onText );
    input.setTextCompositionCallback( onComposition );
    sw::unique_ptr<ProbeInputDevice> pProbe    = sw::make_unique<ProbeInputDevice>();
    ProbeInputDevice*                pProbeRaw = pProbe.get();
    const uint8*                     pVtable   = static_cast<const uint8*>( sw::IModuleUnloadListener::findVtableAddress( static_cast<const sw::IInputDevice*>( pProbeRaw ) ) );
    input.registerDevice( std::move( pProbe ) );
    SW_EXPECT_TRUE( input.getDevice( sw::InputDeviceKind::Custom ) == pProbeRaw );

    bool        bKeepImageMapped{ false };
    const void* arrCode[] = { onActive.getCodeAddress(), onConnection.getCodeAddress(), onText.getCodeAddress(), onComposition.getCodeAddress() };
    for ( const void* pCode : arrCode )
    {
        const uint8* pStub = static_cast<const uint8*>( pCode );
        SW_EXPECT_EQUAL( 1u, input.onModuleUnloading( pStub, pStub + 1, bKeepImageMapped ) );
    }
    SW_EXPECT_EQUAL( 1u, input.onModuleUnloading( pVtable, pVtable + 1, bKeepImageMapped ) );
    SW_EXPECT_FALSE( bKeepImageMapped );
    SW_EXPECT_TRUE( input.getDevice( sw::InputDeviceKind::Custom ) == nullptr );
    SW_EXPECT_TRUE( input.getKeyboard() != nullptr && input.getMouse() != nullptr ); // 엔진 장치는 그대로다

    // 뗀 뒤에는 아무것도 불리지 않는다. 같은 범위의 두 번째 훑기는 뗄 것이 없다.
    s_inputProbeValue = 0;
    input.onTextInput( "a" );
    input.onTextComposition( "b" );
    input.setActiveGlyphStyle( sw::InputGlyphStyle::GamepadXbox );
    SW_EXPECT_EQUAL( 0, s_inputProbeValue );
    for ( const void* pCode : arrCode )
    {
        const uint8* pStub = static_cast<const uint8*>( pCode );
        SW_EXPECT_EQUAL( 0u, input.onModuleUnloading( pStub, pStub + 1, bKeepImageMapped ) );
    }
    input.shutdown();
}

#if defined( SW_PLATFORM_WINDOWS )
namespace
{
    sw::string s_receivedText; ///< 글자 입력 콜백이 받은 글자를 받은 순서대로 이은 것

    void onRecordTextInput( sw::string_view text )
    {
        s_receivedText.append( text.data(), text.size() );
    }
} // namespace

/**
 * @brief [InputManagerTest] WM_CHAR 의 글자가 다음 프레임에 `InputManager::setTextInputCallback` 콜백으로 UTF-8 로 온다
 * @details 글자 입력의 창구는 이것 하나다(언리얼 `FSlateApplication::OnKeyChar` 자리). 메시지 펌프가 원시 이벤트로 넣고 `beginFrame` 이 콜백을 부른다.
 *          제어 문자(백스페이스 등)는 글자가 아니므로 오지 않는다.
 */
SW_TEST_CASE( InputManagerTest, WmCharReachesTheTextInputCallback )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.setTextInputCallback( SW_DELEGATE_FUNCTION( sw::InputManager::TextInputDelegate, onRecordTextInput ) );
    s_receivedText.clear();

    sw::NativeWindowEvent charEvent{};
    charEvent._message = WM_CHAR;
    charEvent._wParam  = 0xAC00; // '가'
    input.processNativeEvent( charEvent );
    SW_EXPECT_TRUE( s_receivedText.empty() ); // 프레임이 시작해야 적용된다
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( s_receivedText == "\xEA\xB0\x80" );
    input.endFrame();

    s_receivedText.clear();
    charEvent._wParam = 0x08; // 백스페이스
    input.processNativeEvent( charEvent );
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( s_receivedText.empty() );
    input.endFrame();

    input.setTextInputCallback( {} );
    input.shutdown();
}

/**
 * @brief [InputManagerTest] 서로게이트 쌍으로 온 WM_CHAR 두 개는 UTF-8 4 바이트 한 글자가 되고, 짝 없는 서로게이트는 U+FFFD 한 글자가 된다
 * @details Windows 는 BMP 밖 글자(이모지 · 확장 한자)를 높은 · 낮은 서로게이트 WM_CHAR 두 개로 보낸다. 반쪽마다 따로 인코딩하면 3 바이트 둘 —
 *          UTF-8 이 아닌 글(CESU-8)이 된다. 짝 없는 반쪽은 버리지 않고 U+FFFD 로 바꾼다(`StringUtil::utf16ToUtf8` 과 같은 규칙).
 */
SW_TEST_CASE( InputManagerTest, WmCharSurrogatePairBecomesOneUtf8Character )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.setTextInputCallback( SW_DELEGATE_FUNCTION( sw::InputManager::TextInputDelegate, onRecordTextInput ) );

    const auto sendChars = [&input]( std::initializer_list<WPARAM> listCodeUnit ) -> sw::string
    {
        s_receivedText.clear();
        sw::NativeWindowEvent charEvent{};
        charEvent._message = WM_CHAR;
        for ( const WPARAM codeUnit : listCodeUnit )
        {
            charEvent._wParam = codeUnit;
            input.processNativeEvent( charEvent );
        }
        input.beginFrame( 0.016f );
        input.endFrame();
        return s_receivedText;
    };

    SW_EXPECT_TRUE( sendChars( { 0xD83D, 0xDE00 } ) == "\xF0\x9F\x98\x80" ); // U+1F600
    SW_EXPECT_TRUE( sendChars( { 0xD840, 0xDC0B } ) == "\xF0\xA0\x80\x8B" ); // U+2000B(확장 한자)
    SW_EXPECT_TRUE( sendChars( { 0xDE00 } ) == "\xEF\xBF\xBD" );             // 높은 반쪽 없는 낮은 반쪽
    SW_EXPECT_TRUE( sendChars( { 0xD83D, 'a' } ) == "\xEF\xBF\xBD"
                                                    "a" ); // 낮은 반쪽 대신 다른 글자
    SW_EXPECT_TRUE( sendChars( { 0xD83D, 0xD83D, 0xDE00 } ) == "\xEF\xBF\xBD"
                                                               "\xF0\x9F\x98\x80" );

    // 쌍이 프레임 경계에 걸려도 한 글자다 — 반쪽은 큐가 아니라 메시지를 받는 쪽이 들고 있다.
    SW_EXPECT_TRUE( sendChars( { 0xD83D } ).empty() );
    SW_EXPECT_TRUE( sendChars( { 0xDE00 } ) == "\xF0\x9F\x98\x80" );

    input.setTextInputCallback( {} );
    input.shutdown();
}
#endif

/**
 * @brief [InputManagerTest] 장치를 등록에서 내리면 목록에서 빠지고, 대표 장치였으면 남은 같은 종류의 장치가 대표가 된다
 * @details 모듈이 꽂은 장치를 그 모듈이 스스로 내리는 창구다(유니티 `InputSystem.RemoveDevice`). 모듈 이미지를 내릴 때의 정리
 *          (`onModuleUnloading`)도 같은 함수를 지난다.
 */
SW_TEST_CASE( InputManagerTest, UnregisterDeviceFallsBackToTheRemainingDeviceOfThatKind )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::MouseDevice* pEngineMouse = input.getMouse();
    SW_ASSERT_NOT_NULL( pEngineMouse );

    sw::unique_ptr<SecondMouseDevice> pSecond    = sw::make_unique<SecondMouseDevice>();
    SecondMouseDevice*                pSecondRaw = pSecond.get();
    input.registerDevice( std::move( pSecond ) );
    SW_EXPECT_TRUE( input.getMouse() == pEngineMouse ); // 먼저 온 장치가 대표다

    input.unregisterDevice( pEngineMouse );
    SW_EXPECT_TRUE( input.getMouse() == pSecondRaw );
    SW_EXPECT_TRUE( input.getDevice( sw::InputDeviceKind::Mouse ) == pSecondRaw );

    // 대표가 바뀐 뒤에도 마우스 이벤트가 그 장치에 닿는다.
    input.postRawEvent( sw::RawInputEvent::makeMouseMove( 30, 40 ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_EQUAL( 30, pSecondRaw->getPositionX() );
    input.endFrame();

    input.unregisterDevice( nullptr ); // 아무것도 하지 않는다
    input.unregisterDevice( pSecondRaw );
    SW_EXPECT_TRUE( input.getMouse() == nullptr );
    SW_EXPECT_TRUE( input.getDevice( sw::InputDeviceKind::Mouse ) == nullptr );
    input.beginFrame( 0.016f ); // 마우스가 없어도 프레임이 돈다
    input.endFrame();
    input.shutdown();
}

/**
 * @brief [InputManagerTest] 키보드 포커스가 게임이 아니면 게임 쪽 키 조회 · 통합 InputMap 의 키 바인딩이 "안 눌림" 이고, 포커스를 무시하는 맵 · 장치는 그대로 본다
 * @details 개발 콘솔이 열린 동안 게임 코드의 `isKeyDown` 직접 조회까지 막는 장치다 — InputMap 레이어만으로는 그 조회를 막지 못한다.
 *          포커스를 넘기기 전부터 눌려 있던 키는 돌아오면 다시 보이고, 넘긴 동안 눌린 키는 뗄 때까지 가린다(눌림 · 뗌 둘 다).
 */
SW_TEST_CASE( InputManagerTest, KeyboardFocusHidesKeysFromGame )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputMap& gameMap = input.getInputMap();
    gameMap.bind( "Forward", sw::Key::W, sw::ActionTrigger::Down );
    gameMap.bind( "Jump", sw::Key::Space, sw::ActionTrigger::Pressed );
    gameMap.bindAnyKey( "AnyKey" );
    sw::InputMap shellMap;
    shellMap.setInputManager( &input );
    shellMap.setKeyboardFocusIgnored( true );
    shellMap.bind( "ShellForward", sw::Key::W, sw::ActionTrigger::Down );

    runInputFrame( input, { sw::RawInputEvent::makeKeyDown( sw::Key::W ) } );
    shellMap.update( 0.016f );
    SW_EXPECT_TRUE( input.isKeyDown( sw::Key::W ) );
    SW_EXPECT_TRUE( gameMap.isActionDown( "Forward" ) );
    input.endFrame();

    input.setKeyboardFocus( sw::InputKeyboardFocus::DevConsole );
    runInputFrame( input, { sw::RawInputEvent::makeKeyDown( sw::Key::Space ) } );
    shellMap.update( 0.016f );
    SW_EXPECT_FALSE( input.isKeyDown( sw::Key::W ) );
    SW_EXPECT_FALSE( input.isKeyDown( sw::Key::Space ) );
    SW_EXPECT_FALSE( input.wasKeyPressed( sw::Key::Space ) );
    SW_EXPECT_FALSE( input.wasAnyInputPressed() );
    SW_EXPECT_FALSE( gameMap.isActionDown( "Forward" ) );
    SW_EXPECT_FALSE( gameMap.wasActionTriggered( "Jump" ) );
    SW_EXPECT_FALSE( gameMap.wasActionTriggered( "AnyKey" ) );
    SW_EXPECT_TRUE( shellMap.isActionDown( "ShellForward" ) );          // 셸 맵은 포커스를 무시한다
    SW_EXPECT_TRUE( input.getKeyboard()->isKeyDown( sw::Key::Space ) ); // 장치 상태는 계속 갱신된다
    input.endFrame();

    input.setKeyboardFocus( sw::InputKeyboardFocus::Game );
    runInputFrame( input, {} );
    SW_EXPECT_TRUE( input.isKeyDown( sw::Key::W ) );      // 넘기기 전부터 눌려 있던 키
    SW_EXPECT_FALSE( input.isKeyDown( sw::Key::Space ) ); // 넘긴 동안 눌린 키는 뗄 때까지 가린다
    SW_EXPECT_TRUE( gameMap.isActionDown( "Forward" ) );
    SW_EXPECT_FALSE( gameMap.wasActionTriggered( "Jump" ) );
    input.endFrame();

    runInputFrame( input, { sw::RawInputEvent::makeKeyUp( sw::Key::Space ) } );
    SW_EXPECT_FALSE( input.wasKeyReleased( sw::Key::Space ) );
    input.endFrame();

    runInputFrame( input, { sw::RawInputEvent::makeKeyDown( sw::Key::Space ) } ); // 돌아온 뒤 새로 누르면 게임 것이다
    SW_EXPECT_TRUE( input.wasKeyPressed( sw::Key::Space ) );
    SW_EXPECT_TRUE( gameMap.wasActionTriggered( "Jump" ) );
    input.endFrame();
    input.shutdown();
}

/**
 * @brief [InputManagerTest] 글자 입력은 키보드 포커스를 가진 쪽의 콜백에만 간다
 */
SW_TEST_CASE( InputManagerTest, TextInputGoesToFocusOwner )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    FocusTextRecorder recorder;
    input.setTextInputCallback( SW_DELEGATE_METHOD( sw::InputManager::TextInputDelegate, &FocusTextRecorder::onGameText, &recorder ) );
    input.setTextInputCallback( SW_DELEGATE_METHOD( sw::InputManager::TextInputDelegate, &FocusTextRecorder::onConsoleText, &recorder ), sw::InputKeyboardFocus::DevConsole );

    runInputFrame( input, { sw::RawInputEvent::makeTextInput( "a" ) } );
    input.endFrame();
    input.setKeyboardFocus( sw::InputKeyboardFocus::DevConsole );
    runInputFrame( input, { sw::RawInputEvent::makeTextInput( "b" ) } );
    input.endFrame();
    input.setKeyboardFocus( sw::InputKeyboardFocus::Game );
    runInputFrame( input, { sw::RawInputEvent::makeTextInput( "c" ) } );
    input.endFrame();

    SW_EXPECT_STREQ( "ac", recorder._game.c_str() );
    SW_EXPECT_STREQ( "b", recorder._console.c_str() );

    input.setTextInputCallback( {} );
    input.setTextInputCallback( {}, sw::InputKeyboardFocus::DevConsole );
    input.shutdown();
}
