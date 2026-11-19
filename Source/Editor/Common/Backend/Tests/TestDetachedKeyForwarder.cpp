// 떼어 낸 창의 키 넘기기 — 누름을 넣고 아직 뗌을 넣지 않은 키를 기억했다가, 넘기기를 멈추면(포커스를 잃음 · 주 창으로 되돌림) 짝 뗌을 넣는다(nogpu, ImGui · 입력 매니저 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Backend/ImGuiKeyForward.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [DetachedKeyForwarderTest] 자동 반복 누름이 여럿 와도 누른 키는 한 번만 들고, 뗌을 넣으면 빠지며, 넘기기를 멈추면 새 사건은 버리고 남은 키를 뗀다
 */
SW_TEST_CASE( DetachedKeyForwarderTest, HeldKeysAreReleasedWhenForwardingStops )
{
    DetachedKeyForwarder  forwarder;
    vector<RawInputEvent> listPosted;
    const auto            post = [&listPosted]( const RawInputEvent& event )
    {
        listPosted.push_back( event );
        return true;
    };

    forwarder.forward( true, { RawInputEvent::makeKeyDown( Key::A ), RawInputEvent::makeKeyDown( Key::A, 0, true ), RawInputEvent::makeKeyDown( Key::B ) }, post );
    SW_EXPECT_EQUAL( size_t{ 2 }, forwarder.getHeldKeys().size() );
    forwarder.forward( true, { RawInputEvent::makeKeyUp( Key::A ) }, post );
    SW_REQUIRE_EQUAL( size_t{ 1 }, forwarder.getHeldKeys().size() );
    SW_EXPECT_TRUE( forwarder.getHeldKeys()[0] == Key::B );

    listPosted.clear();
    forwarder.forward( false, { RawInputEvent::makeKeyDown( Key::C ) }, post );
    SW_EXPECT_TRUE( forwarder.getHeldKeys().empty() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listPosted.size() );
    SW_EXPECT_TRUE( listPosted[0]._type == RawInputEventKind::KeyUp );
    SW_EXPECT_TRUE( listPosted[0]._payload._keyData._key == Key::B );
}

/**
 * @brief [DetachedKeyForwarderTest] 큐가 차서 못 넣은 누름은 기억하지 않고, 못 넣은 뗌은 남겨 다음 프레임에 다시 넣는다
 */
SW_TEST_CASE( DetachedKeyForwarderTest, EventsTheQueueRefusedAreNotLost )
{
    DetachedKeyForwarder        forwarder;
    const vector<RawInputEvent> listNoEvent;
    const auto                  refuse = []( const RawInputEvent& )
    { return false; };
    const auto accept = []( const RawInputEvent& )
    { return true; };

    forwarder.forward( true, { RawInputEvent::makeKeyDown( Key::A ) }, refuse );
    SW_EXPECT_TRUE( forwarder.getHeldKeys().empty() );

    forwarder.forward( true, { RawInputEvent::makeKeyDown( Key::B ) }, accept );
    forwarder.forward( false, listNoEvent, refuse );
    SW_REQUIRE_EQUAL( size_t{ 1 }, forwarder.getHeldKeys().size() );
    forwarder.forward( false, listNoEvent, accept );
    SW_EXPECT_TRUE( forwarder.getHeldKeys().empty() );
}
