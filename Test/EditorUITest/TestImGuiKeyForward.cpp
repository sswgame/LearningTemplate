#include "pch.h"

#include "Editor/Common/Backend/ImGuiKeyForward.h"

#include "TestFramework/TestFramework.h"

#include <imgui.h>
#include <imgui_internal.h>

using namespace sw;
using namespace sw::editor;

// 떼어 낸 게임 뷰 창의 키보드 — ImGui 가 받은 키 · 글자가 게임 입력 사건으로 바뀌는지 본다.

namespace
{
    struct ImGuiKeyForwardTestInternal
    {
        /** @brief 테스트 동안 ImGui 컨텍스트를 하나 세웠다 내립니다. 프레임은 `runFrame` 이 돌린다. */
        class ScopedContext
        {
        public:
            ScopedContext()
                : _pContext{ ImGui::CreateContext() }
            {
                ImGuiIO& io    = ImGui::GetIO();
                io.DisplaySize = ImVec2( 640.0f, 480.0f );
                uint8* pPixels = nullptr;
                int32  width   = 0;
                int32  height  = 0;
                io.Fonts->GetTexDataAsRGBA32( &pPixels, &width, &height ); // 글꼴 아틀라스가 없으면 첫 프레임이 선다
            }

            ~ScopedContext()
            {
                if ( _pContext != nullptr )
                    ImGui::DestroyContext( _pContext );
            }

            ScopedContext( const ScopedContext& )            = delete;
            ScopedContext& operator=( const ScopedContext& ) = delete;

        private:
            ImGuiContext* _pContext{ nullptr };
        };

        /** @brief 프레임 하나를 돌립니다. 넣어 둔 입력은 이 프레임의 상태가 된다. */
        static void runFrame()
        {
            ImGui::NewFrame();
            ImGui::EndFrame();
        }

        /** @brief 프레임을 돌리고 그 프레임의 키 변화를 읽습니다. 프레임 밖에서 읽으면 ImGui 가 입력을 비운 뒤라 NewFrame 과 EndFrame 사이에서 읽는다. */
        static vector<RawInputEvent> runFrameAndReadKeys()
        {
            vector<RawInputEvent> listEvent;
            ImGui::NewFrame();
            ImGuiKeyForward::appendKeyChanges( listEvent );
            ImGui::EndFrame();
            return listEvent;
        }
    };
} // namespace

/**
 * @brief [ImGuiKeyForwardTest] 눌림은 누름 사건 하나, 놓음은 뗌 사건 하나가 되고 지금 누른 수정 키가 붙는다
 */
SW_TEST_CASE( ImGuiKeyForwardTest, PressAndReleaseBecomeKeyEventsWithModifiers )
{
    using Internal = ImGuiKeyForwardTestInternal;
    Internal::ScopedContext context;

    // ImGui 의 입력 큐는 수정 키와 다른 키를 한 프레임에 섞지 않는다(트리클) — Ctrl 을 한 프레임 먼저 누른다.
    ImGui::GetIO().AddKeyEvent( ImGuiKey_LeftCtrl, true );
    Internal::runFrame();
    ImGui::GetIO().AddKeyEvent( ImGuiKey_W, true );
    const vector<RawInputEvent> listDown = Internal::runFrameAndReadKeys();
    bool                        bFoundW  = false;
    for ( const RawInputEvent& event : listDown )
    {
        if ( event._type == RawInputEventType::KeyDown && event._payload._keyData._key == Key::W )
        {
            bFoundW = true;
            SW_EXPECT_EQUAL( 0, static_cast<int32>( event._bRepeat ) );
            SW_EXPECT_EQUAL( static_cast<int32>( ModifierKey::Ctrl ), static_cast<int32>( event._modifierMask ) );
        }
    }
    SW_EXPECT_TRUE( bFoundW );
    SW_EXPECT_TRUE( ImGuiKeyForward::isKeyDown( Key::W ) );

    ImGui::GetIO().AddKeyEvent( ImGuiKey_W, false );
    const vector<RawInputEvent> listUp = Internal::runFrameAndReadKeys();
    bool                        bUpW   = false;
    for ( const RawInputEvent& event : listUp )
    {
        bUpW = bUpW || ( event._type == RawInputEventType::KeyUp && event._payload._keyData._key == Key::W );
    }
    SW_EXPECT_TRUE( bUpW );
    SW_EXPECT_FALSE( ImGuiKeyForward::isKeyDown( Key::W ) );
}

/**
 * @brief [ImGuiKeyForwardTest] ImGui 키를 하나씩 눌러 보면 엔진 키 전부가 한 번씩 나온다 — 짝이 빠진 키도, 두 ImGui 키가 한 엔진 키로 겹치는 일도 없다
 * @details 영문 · 윗줄 숫자 · 기능 키 · 숫자 패드 구간과 이름 있는 키 표를 함께 본다. 엔진 키를 더하고 표에 빼먹으면 여기서 선다.
 */
SW_TEST_CASE( ImGuiKeyForwardTest, EveryEngineKeyIsReachableFromExactlyOneImGuiKey )
{
    using Internal = ImGuiKeyForwardTestInternal;
    Internal::ScopedContext context;

    vector<int32> listReached( static_cast<size_t>( Key::Count ), 0 );
    for ( int32 imguiKey = ImGuiKey_NamedKey_BEGIN; imguiKey < ImGuiKey_NamedKey_END; ++imguiKey )
    {
        if ( ImGui::IsAliasKey( static_cast<ImGuiKey>( imguiKey ) ) )
            continue; // 마우스 단추 같은 별칭 키는 AddKeyEvent 로 넣을 수 없다(ImGui 가 단언한다)
        ImGui::GetIO().AddKeyEvent( static_cast<ImGuiKey>( imguiKey ), true );
        ImGui::NewFrame();
        const Key found = ImGuiKeyForward::findPressedKey();
        ImGui::EndFrame();
        if ( found != Key::Unknown )
            ++listReached[static_cast<size_t>( found )];
        ImGui::GetIO().AddKeyEvent( static_cast<ImGuiKey>( imguiKey ), false );
        Internal::runFrame();
    }
    for ( int32 keyIndex = static_cast<int32>( Key::Unknown ) + 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
    {
        SW_EXPECT_TRUE_MSG( listReached[static_cast<size_t>( keyIndex )] == 1, KeyCodeUtil::toName( static_cast<Key>( keyIndex ) ) );
    }
}

/**
 * @brief [ImGuiKeyForwardTest] 글자 입력은 글자마다 `TextInput` 사건이 되고 한글(3 바이트)도 그대로 UTF-8 로 간다
 */
SW_TEST_CASE( ImGuiKeyForwardTest, TextInputBecomesOneEventPerCharacter )
{
    using Internal = ImGuiKeyForwardTestInternal;
    Internal::ScopedContext context;

    ImGui::GetIO().AddInputCharacter( 'a' );
    ImGui::GetIO().AddInputCharacter( 0xAC00 ); // 가
    vector<RawInputEvent> listEvent;
    ImGui::NewFrame();
    ImGuiKeyForward::appendTextInput( listEvent );
    ImGui::EndFrame();

    SW_ASSERT_EQUAL( 2, static_cast<int32>( listEvent.size() ) );
    SW_EXPECT_EQUAL( static_cast<int32>( RawInputEventType::TextInput ), static_cast<int32>( listEvent[0]._type ) );
    SW_EXPECT_TRUE( string_view{ listEvent[0]._payload._textData._arrUtf8 } == "a" );
    SW_EXPECT_TRUE( string_view{ listEvent[1]._payload._textData._arrUtf8 } == "\xEA\xB0\x80" );
}

/**
 * @brief [ImGuiKeyForwardTest] 키를 누른 채 포커스를 잃어도(창을 바꿔도) 게임에 눌린 채 남지 않는다 — 짝 뗌을 넣고, 큐가 차서 못 넣은 뗌은 다음 프레임에 다시 넣는다
 * @details 떼어 낸 OS 창으로 포커스가 실제로 옮겨 가는 것은 ImGui 백엔드의 일이라 이 테스트가 보지 않는다(사람이 있는 Windows 에디터의 눈 확인).
 *          변이: 포커스를 잃을 때 기억한 키를 놓지 않으면 눌린 키가 남아 첫 단언이 진다. 못 넣은 뗌을 기억에서 지우면 마지막 단언이 진다.
 */
SW_TEST_CASE( ImGuiKeyForwardTest, DetachedForwarderReleasesHeldKeysWhenFocusIsLost )
{
    DetachedKeyForwarder  forwarder;
    vector<RawInputEvent> listPosted;
    bool                  bQueueFull = false;
    const auto            post       = [&]( const RawInputEvent& event )
    {
        if ( bQueueFull )
            return false;
        listPosted.push_back( event );
        return true;
    };

    // 포커스가 있을 때 W 를 누르면 게임으로 가고, 눌린 채 기억한다.
    forwarder.forward( true, vector<RawInputEvent>{ RawInputEvent::makeKeyDown( Key::W ) }, post );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( listPosted.size() ) );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( forwarder.getHeldKeys().size() ) );

    // 포커스를 잃으면(그 프레임의 사건은 버린다) 누른 채 남은 W 의 뗌이 간다.
    forwarder.forward( false, vector<RawInputEvent>{ RawInputEvent::makeKeyDown( Key::A ) }, post );
    SW_ASSERT_EQUAL( 2, static_cast<int32>( listPosted.size() ) );
    SW_EXPECT_EQUAL( static_cast<int32>( RawInputEventType::KeyUp ), static_cast<int32>( listPosted[1]._type ) );
    SW_EXPECT_TRUE( listPosted[1]._payload._keyData._key == Key::W );
    SW_EXPECT_TRUE( forwarder.getHeldKeys().empty() );

    // 큐가 차서 뗌을 못 넣으면 기억을 남겨 다음 프레임에 다시 시도한다.
    forwarder.forward( true, vector<RawInputEvent>{ RawInputEvent::makeKeyDown( Key::S ) }, post );
    bQueueFull = true;
    forwarder.forward( false, vector<RawInputEvent>{}, post );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( forwarder.getHeldKeys().size() ) );
    bQueueFull = false;
    forwarder.forward( false, vector<RawInputEvent>{}, post );
    SW_EXPECT_TRUE( forwarder.getHeldKeys().empty() );
    SW_EXPECT_TRUE( listPosted.back()._payload._keyData._key == Key::S );
}
