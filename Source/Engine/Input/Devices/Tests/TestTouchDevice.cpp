#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Devices/TouchDevice.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"

using namespace sw;

/** @brief [TouchDeviceTest] 손가락마다 눌림 · 엣지 · 위치가 따로이고, 범위 밖 번호는 무시합니다. */
SW_TEST_CASE( TouchDeviceTest, TracksEachPointerSeparately )
{
    TouchDevice touch;
    touch.onFrameBegin( 0.0f );
    touch.setPointerState( 1, 30, 40, true );
    SW_EXPECT_TRUE( touch.isControlDown( 1 ) );
    SW_EXPECT_TRUE( touch.wasControlPressed( 1 ) );
    SW_EXPECT_FALSE( touch.isControlDown( 0 ) );
    SW_EXPECT_EQUAL( 40, touch.getPointerPosition( 1 )._y );

    touch.onFrameBegin( 0.0f );
    touch.setPointerState( 1, 31, 41, true ); // 이동은 눌림 엣지가 아니다
    SW_EXPECT_FALSE( touch.wasControlPressed( 1 ) );
    SW_EXPECT_EQUAL( 31, touch.getPointerPosition( 1 )._x );

    touch.setPointerState( 1, 31, 41, false );
    SW_EXPECT_TRUE( touch.wasControlReleased( 1 ) );
    SW_EXPECT_FALSE( touch.isControlDown( 1 ) );

    touch.setPointerState( TouchDevice::kMaxPointerCount, 0, 0, true );
    SW_EXPECT_FALSE( touch.isControlDown( TouchDevice::kMaxPointerCount ) );
}

/** @brief [TouchDeviceTest] 입력 맵 슬롯은 `Touch.<손가락>` 글로 왕복합니다. */
SW_TEST_CASE( TouchDeviceTest, SlotTextRoundTrips )
{
    InputSlot slot;
    SW_REQUIRE_TRUE( InputSlot::tryParse( "Touch.2", slot ) );
    SW_EXPECT_TRUE( slot == InputSlot::fromTouch( 2 ) );
    SW_EXPECT_EQUAL( string( "Touch.2" ), InputSlot::toText( slot ) );
    SW_EXPECT_FALSE( InputSlot::tryParse( "Touch.10", slot ) ); // 추적하는 손가락 밖이다
    SW_EXPECT_FALSE( InputSlot::tryParse( "Touch.x", slot ) );
}

/** @brief [TouchDeviceTest] 원시 이벤트가 입력 관리자를 지나 장치에 닿습니다. 창이 낼 이벤트와 같은 길입니다. */
SW_TEST_CASE( TouchDeviceTest, RawEventsReachTheDevice )
{
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );
    SW_REQUIRE_TRUE( input.getTouch() != nullptr );

    input.postRawEvent( RawInputEvent::makeTouch( RawInputEventType::TouchDown, 0, 100, 200 ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( input.getTouch()->isControlDown( 0 ) );
    SW_EXPECT_EQUAL( 200, input.getTouch()->getPointerPosition( 0 )._y );
    input.endFrame();

    input.postRawEvent( RawInputEvent::makeTouch( RawInputEventType::TouchMove, 0, 110, 210 ) );
    input.postRawEvent( RawInputEvent::makeTouch( RawInputEventType::TouchUp, 0, 110, 210 ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_FALSE( input.getTouch()->isControlDown( 0 ) );
    SW_EXPECT_EQUAL( 110, input.getTouch()->getPointerPosition( 0 )._x );
    input.endFrame();

    input.shutdown();
}
