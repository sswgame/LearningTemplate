/**
 * @file TestInputFrameQueue.cpp
 * @brief 입력 프레임 큐의 계약 테스트입니다 — 넣은 순서, 입력 프레임 번호, 가상 입력이 실제 입력과 같은 적용 길.
 * @details 에디터 UI 스레드 분리 2 단계의 경계다. 스레드는 아직 하나라 한 스레드에서 넣는 쪽과 적용하는 쪽을 차례로 밟는다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"

using namespace sw;

/** @brief [InputFrameQueueTest] 한 프레임에 넣은 이벤트는 넣은 순서대로 적용됩니다. */
SW_TEST_CASE( InputFrameQueueTest, AppliesEventsInPostOrder )
{
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );

    SW_EXPECT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::B ) ) );
    SW_EXPECT_TRUE( input.postRawEvent( RawInputEvent::makeMouseMove( 10, 20 ) ) );
    SW_EXPECT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::A ) ) );
    input.beginFrame( 0.016f );

    const vector<RawInputEvent>& listEvent = input.getLastFrameEvents();
    SW_REQUIRE_EQUAL( size_t{ 3 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._type == RawInputEventKind::KeyDown && listEvent[0]._payload._keyData._key == Key::B );
    SW_EXPECT_TRUE( listEvent[1]._type == RawInputEventKind::MouseMove );
    SW_EXPECT_TRUE( listEvent[2]._type == RawInputEventKind::KeyDown && listEvent[2]._payload._keyData._key == Key::A );
    input.endFrame();
    input.shutdown();
}

/** @brief [InputFrameQueueTest] 뒤 입력 프레임 번호로 넣은 이벤트는 그 프레임에 적용되고, 지난 번호는 다음 프레임에 적용됩니다(한 프레임 지연 계약의 바탕). */
SW_TEST_CASE( InputFrameQueueTest, AppliesEachEventAtItsInputFrame )
{
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );
    KeyboardDevice* pKeyboard = input.getKeyboard();
    SW_REQUIRE_TRUE( pKeyboard != nullptr );

    const uint32 firstFrame = input.getCollectingInputFrame();
    SW_EXPECT_TRUE( input.postRawEventAt( RawInputEvent::makeKeyDown( Key::A ), firstFrame + 1 ) ); // 다음 프레임 몫
    SW_EXPECT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::B ) ) );                   // 이번 프레임 몫

    input.beginFrame( 0.016f );
    SW_EXPECT_EQUAL( firstFrame + 1, input.getCollectingInputFrame() );
    SW_EXPECT_TRUE( pKeyboard->isKeyDown( Key::B ) );
    SW_EXPECT_FALSE( pKeyboard->isKeyDown( Key::A ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, input.getLastFrameEvents().size() );
    input.endFrame();

    SW_EXPECT_TRUE( input.postRawEventAt( RawInputEvent::makeKeyDown( Key::C ), firstFrame ) ); // 이미 지난 번호 — 다음 프레임에 적용
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( pKeyboard->wasKeyPressed( Key::A ) );
    SW_EXPECT_TRUE( pKeyboard->wasKeyPressed( Key::C ) );
    const vector<RawInputEvent>& listEvent = input.getLastFrameEvents();
    SW_REQUIRE_EQUAL( size_t{ 2 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._payload._keyData._key == Key::A ); // 남겨 둔 칸이 그 뒤에 넣은 칸보다 앞이다
    SW_EXPECT_TRUE( listEvent[1]._payload._keyData._key == Key::C );
    input.endFrame();
    input.shutdown();
}

/** @brief [InputFrameQueueTest] 가상 입력(시나리오 · 리플레이)은 실제 입력과 같은 적용 길을 지나 같은 장치 상태와 같은 프레임 이벤트를 남깁니다(가상 표시만 다르다). */
SW_TEST_CASE( InputFrameQueueTest, VirtualInputTakesTheSamePathAsRealInput )
{
    InputManager realInput;
    SW_REQUIRE_TRUE( realInput.initialize() );
    SW_EXPECT_TRUE( realInput.postRawEvent( RawInputEvent::makeKeyDown( Key::A ) ) );
    realInput.beginFrame( 0.016f );

    InputManager       virtualInput;
    VirtualInputScript script;
    SW_REQUIRE_TRUE( virtualInput.initialize() );
    script.addEvent( 0, RawInputEvent::makeKeyDown( Key::A ) );
    Subscription attachment = virtualInput.attachVirtualInput( script, VirtualInputMode::Exclusive );
    virtualInput.beginFrame( 0.016f );

    SW_EXPECT_TRUE( realInput.getKeyboard()->isKeyDown( Key::A ) && virtualInput.getKeyboard()->isKeyDown( Key::A ) );
    SW_EXPECT_TRUE( realInput.getKeyboard()->wasKeyPressed( Key::A ) && virtualInput.getKeyboard()->wasKeyPressed( Key::A ) );
    SW_REQUIRE_EQUAL( realInput.getLastFrameEvents().size(), virtualInput.getLastFrameEvents().size() );
    const RawInputEvent& realEvent    = realInput.getLastFrameEvents()[0];
    const RawInputEvent& virtualEvent = virtualInput.getLastFrameEvents()[0];
    SW_EXPECT_TRUE( realEvent._type == virtualEvent._type );
    SW_EXPECT_TRUE( realEvent._payload._keyData._key == virtualEvent._payload._keyData._key );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( realEvent._bSynthetic ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( virtualEvent._bSynthetic ) );

    realInput.endFrame();
    virtualInput.endFrame();
    attachment.reset();
    virtualInput.shutdown();
    realInput.shutdown();
}
