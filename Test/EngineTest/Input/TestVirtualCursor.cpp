#include "pch.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Virtual/VirtualCursor.h"

#include "TestFramework/TestFramework.h"

// 가상 커서 — 패드 스틱 · 버튼이 마우스 이동 · 버튼 이벤트가 되는 규칙. 창이 없어도 `emitFrame` 만으로 본다.
namespace
{
    struct TestVirtualCursorInternal
    {
        static constexpr sw::InputViewRect kClientSize{
            sw::int2{  0,   0},
            sw::int2{800, 600}
        };

        static sw::VirtualCursor makeCursor()
        {
            sw::VirtualCursor         cursor;
            sw::VirtualCursorSettings settings;
            settings._speed    = 1000.0f;
            settings._deadzone = 0.2f;
            settings._exponent = 1.0f;
            cursor.setSettings( settings );
            cursor.setEnabled( true );
            return cursor;
        }
    };
} // namespace

/**
 * @brief [VirtualCursorTest] 스틱을 밀면 속도 × 프레임 시간만큼 커서가 움직이고, 위가 화면 위(-y)이며, 데드존 안은 움직이지 않는다
 */
SW_TEST_CASE( VirtualCursorTest, StickMovesTheCursorByVelocityTimesFrameTime )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::GamepadDevice* pPad = input.getGamepad( 0 );
    if ( pPad == nullptr )
    {
        input.shutdown();
        return; // 이 플랫폼에는 게임패드 장치가 없다
    }
    pPad->setVirtualSession( true );
    pPad->setVirtualConnected( true );

    sw::VirtualCursor             cursor = TestVirtualCursorInternal::makeCursor();
    sw::vector<sw::RawInputEvent> listEvent;

    // 데드존 안(0.1)은 움직이지 않는다.
    pPad->setAxis( 2, 0.1f );
    cursor.emitFrame( pPad, sw::int2{ 100, 100 }, TestVirtualCursorInternal::kClientSize, 0.01f, listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
    SW_EXPECT_FALSE( cursor.didEmitLastFrame() );

    // 오른쪽 끝까지 민다: 1000 px/s × 0.01 s = 10 px
    pPad->setAxis( 2, 1.0f );
    cursor.emitFrame( pPad, sw::int2{ 100, 100 }, TestVirtualCursorInternal::kClientSize, 0.01f, listEvent );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listEvent.size() ) );
    SW_EXPECT_TRUE( listEvent[0]._type == sw::RawInputEventType::MouseMove );
    SW_EXPECT_EQUAL( 110, listEvent[0]._payload._mouseData._x );
    SW_EXPECT_EQUAL( 100, listEvent[0]._payload._mouseData._y );
    SW_EXPECT_TRUE( cursor.didEmitLastFrame() );

    // 이어서 위로 민다: 스틱 위(+y)는 화면 위(-y)다. 지난 프레임에 낸 위치(110, 100)에서 이어 움직인다.
    listEvent.clear();
    pPad->setAxis( 2, 0.0f );
    pPad->setAxis( 3, 1.0f );
    cursor.emitFrame( pPad, sw::int2{ 110, 100 }, TestVirtualCursorInternal::kClientSize, 0.01f, listEvent );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listEvent.size() ) );
    SW_EXPECT_EQUAL( 110, listEvent[0]._payload._mouseData._x );
    SW_EXPECT_EQUAL( 90, listEvent[0]._payload._mouseData._y );

    // 실제 마우스가 다른 곳으로 움직였으면 거기서 이어 움직인다.
    listEvent.clear();
    cursor.emitFrame( pPad, sw::int2{ 400, 300 }, TestVirtualCursorInternal::kClientSize, 0.01f, listEvent );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listEvent.size() ) );
    SW_EXPECT_EQUAL( 400, listEvent[0]._payload._mouseData._x );
    SW_EXPECT_EQUAL( 290, listEvent[0]._payload._mouseData._y );

    // 클라이언트 영역 밖으로 나가지 않는다.
    listEvent.clear();
    cursor.emitFrame( pPad, sw::int2{ 400, 0 }, TestVirtualCursorInternal::kClientSize, 1.0f, listEvent );
    SW_EXPECT_TRUE( listEvent.empty() ); // 이미 맨 위라 움직일 곳이 없다

    pPad->setVirtualSession( false );
    input.shutdown();
}

/**
 * @brief [VirtualCursorTest] 패드 A · B 는 마우스 왼쪽 · 오른쪽 버튼의 누름 · 뗌 이벤트가 되고, 꺼 두면 아무것도 내지 않는다
 */
SW_TEST_CASE( VirtualCursorTest, PadButtonsBecomeMouseButtons )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::GamepadDevice* pPad = input.getGamepad( 0 );
    if ( pPad == nullptr )
    {
        input.shutdown();
        return;
    }
    pPad->setVirtualSession( true );
    pPad->setVirtualConnected( true );

    sw::VirtualCursor             cursor = TestVirtualCursorInternal::makeCursor();
    sw::vector<sw::RawInputEvent> listEvent;
    const sw::int2                position{ 200, 150 };

    pPad->setButtonDown( sw::GamepadButton::A, true );
    cursor.emitFrame( pPad, position, TestVirtualCursorInternal::kClientSize, 0.016f, listEvent );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listEvent.size() ) );
    SW_EXPECT_TRUE( listEvent[0]._type == sw::RawInputEventType::MouseButtonDown );
    SW_EXPECT_TRUE( listEvent[0]._payload._mouseData._button == sw::MouseButton::Left );
    SW_EXPECT_EQUAL( 200, listEvent[0]._payload._mouseData._x );

    // 누르고 있는 동안은 다시 내지 않는다.
    listEvent.clear();
    cursor.emitFrame( pPad, position, TestVirtualCursorInternal::kClientSize, 0.016f, listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );

    pPad->setButtonDown( sw::GamepadButton::A, false );
    pPad->setButtonDown( sw::GamepadButton::B, true );
    cursor.emitFrame( pPad, position, TestVirtualCursorInternal::kClientSize, 0.016f, listEvent );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listEvent.size() ) );
    SW_EXPECT_TRUE( listEvent[0]._type == sw::RawInputEventType::MouseButtonUp );
    SW_EXPECT_TRUE( listEvent[0]._payload._mouseData._button == sw::MouseButton::Left );
    SW_EXPECT_TRUE( listEvent[1]._type == sw::RawInputEventType::MouseButtonDown );
    SW_EXPECT_TRUE( listEvent[1]._payload._mouseData._button == sw::MouseButton::Right );

    // 꺼 두면 아무것도 내지 않는다.
    cursor.setEnabled( false );
    listEvent.clear();
    cursor.emitFrame( pPad, position, TestVirtualCursorInternal::kClientSize, 0.016f, listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );

    pPad->setVirtualSession( false );
    input.shutdown();
}

/**
 * @brief [VirtualCursorTest] 커서 이동은 프레임률과 무관하다 — 같은 시간을 한 프레임으로 가든 여러 프레임으로 가든 간 거리가 같다(속도 × 시간)
 */
SW_TEST_CASE( VirtualCursorTest, DistanceDoesNotDependOnFrameRate )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::GamepadDevice* pPad = input.getGamepad( 0 );
    if ( pPad == nullptr )
    {
        input.shutdown();
        return; // 이 플랫폼에는 게임패드 장치가 없다
    }
    pPad->setVirtualSession( true );
    pPad->setVirtualConnected( true );
    pPad->setAxis( 0, 1.0f );
    pPad->setAxis( 2, 1.0f );

    // 0.12 초를 한 프레임, 여섯 프레임(0.02 초), 열두 프레임(0.01 초)으로 간다. 이벤트의 마우스 위치를 따라가며 마지막 위치를 견준다.
    const float32 arrFrameSeconds[] = { 0.12f, 0.02f, 0.01f };
    float32       arrFinalX[3]{};
    for ( int32 caseIndex = 0; caseIndex < 3; ++caseIndex )
    {
        sw::VirtualCursor             cursor = TestVirtualCursorInternal::makeCursor();
        sw::vector<sw::RawInputEvent> listEvent;
        sw::int2                      mouse{ 100, 100 };
        const int32                   frameCount = static_cast<int32>( 0.12f / arrFrameSeconds[caseIndex] + 0.5f );
        for ( int32 frame = 0; frame < frameCount; ++frame )
        {
            listEvent.clear();
            cursor.emitFrame( pPad, mouse, TestVirtualCursorInternal::kClientSize, arrFrameSeconds[caseIndex], listEvent );
            for ( const sw::RawInputEvent& event : listEvent )
            {
                if ( event._type == sw::RawInputEventType::MouseMove )
                    mouse = sw::int2{ event._payload._mouseData._x, event._payload._mouseData._y };
            }
        }
        arrFinalX[caseIndex] = static_cast<float32>( mouse._x );
    }
    // 1000 px/s × 0.12 s = 120 px. 프레임마다 정수 픽셀로 반올림되므로 프레임 수만큼의 오차는 허용한다.
    SW_EXPECT_NEAR_EQUAL( 220.0f, arrFinalX[0], 1.0f );
    SW_EXPECT_NEAR_EQUAL( 220.0f, arrFinalX[1], 6.0f );
    SW_EXPECT_NEAR_EQUAL( 220.0f, arrFinalX[2], 12.0f );
    pPad->setVirtualSession( false );
    input.shutdown();
}
