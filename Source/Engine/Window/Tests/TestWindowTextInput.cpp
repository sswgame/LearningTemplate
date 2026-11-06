#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/WindowTextInput.h"

// WindowTextInputTest - 플랫폼 공통 입력기의 조합 → 확정 흐름(nogpu). 실제 입력기는 무인 환경(Xvfb)에 없어 가짜 창이 입력기의 몫을 스크립트로 낸다.
// 실제 XIM · text-input-v3 호출과 한글 입력 자체는 이 테스트가 보지 못한다.

using namespace sw;

namespace
{
    /** @brief 입력기 프로토콜을 흉내 내는 창입니다. 켜고 끄기 · 자리를 세고, 조합 · 확정을 실제 창과 같은 함수로 낸다. */
    class FakeInputMethodWindow final : public IWindow
    {
    public:
        uint32 _wantedChangeCount;
        uint32 _areaChangeCount;

        FakeInputMethodWindow()
            : _wantedChangeCount{ 0 }
            , _areaChangeCount{ 0 }
        {
        }

        bool initializeWindow( const utf8* /*pTitle*/, uint32 /*width*/, uint32 /*height*/ ) override { return true; }
        void destroy() override {}
        bool processMessages() override { return true; }
        void waitForMessages( uint32 /*timeoutMilliseconds*/ ) override {}
        bool tryGetFrameExtents( WindowFrameExtents& outExtents ) const override
        {
            outExtents = WindowFrameExtents{};
            return true;
        }
        bool              canPlaceWindow() const override { return false; }
        bool              setFramePosition( int32 /*x*/, int32 /*y*/ ) override { return false; }
        NativeSurfaceKind getSurfaceKind() const override { return NativeSurfaceKind::Wayland; }

        /** @brief X11 XIM 의 PreeditDraw 처럼 일부를 고쳐 알립니다. */
        void imeEdit( uint32 first, uint32 deleteCount, string_view inserted )
        {
            _composition.applyEdit( first, deleteCount, inserted );
            dispatchTextInput( WindowTextInputKind::Composition, _composition.getText() );
        }
        void imeCommit( string_view text )
        {
            dispatchTextInput( WindowTextInputKind::Commit, text );
            _composition.clear();
            dispatchTextInput( WindowTextInputKind::Composition, string_view{} );
        }

    protected:
        void* getNativeHandle() const override { return nullptr; }
        void  applyTextInputWanted( bool /*bWanted*/ ) override { ++_wantedChangeCount; }
        void  applyTextInputArea( const WindowTextInputArea& /*area*/ ) override { ++_areaChangeCount; }

    private:
        WindowTextComposition _composition;
    };

    /** @brief 창 → 입력 관리자 → 글 콜백을 잇고 받은 글을 적어 둡니다. 실제 경로의 `App::onWindowMessage` · `processNativeEvent` 자리입니다. */
    struct WindowTextInputTestUtil
    {
        InputManager*  _pInput{ nullptr };
        vector<string> _listComposition;
        string         _committed;
        uint32         _commitCount{ 0 };

        bool onWindowMessage( const NativeWindowEvent& event )
        {
            if ( event._message == NativeWindowEvent::kMessageTextInput )
                _pInput->postWindowTextInput( *reinterpret_cast<const WindowTextInputEvent*>( event._lParam ) );
            return false;
        }
        void onComposition( string_view text ) { _listComposition.emplace_back( text ); }
        void onCommit( string_view text )
        {
            _committed += text;
            ++_commitCount;
        }
    };
} // namespace

/**
 * @brief [WindowTextInputTest] 조합 글의 일부 고침(글자 단위)을 글 전체로 바꾼다. 범위를 넘는 값은 끝에서 멈춘다
 */
SW_TEST_CASE( WindowTextInputTest, CompositionEditIsCharacterBased )
{
    WindowTextComposition composition;
    composition.applyEdit( 0, 0, "\xE3\x85\x8E" ); // ㅎ
    SW_EXPECT_TRUE( composition.getText() == "\xE3\x85\x8E" );
    composition.applyEdit( 0, 1, "\xED\x95\x98" ); // 하 - 한 글자(3 바이트)를 바꾼다
    SW_EXPECT_TRUE( composition.getText() == "\xED\x95\x98" );
    composition.applyEdit( 1, 0, "A" ); // 끝에 덧붙인다
    composition.applyEdit( 0, 1, string_view{} );
    SW_EXPECT_TRUE( composition.getText() == "A" );
    composition.applyEdit( 9, 9, "B" ); // 범위 밖
    SW_EXPECT_TRUE( composition.getText() == "AB" );
    composition.applyEdit( 1, 9, string_view{} );
    SW_EXPECT_TRUE( composition.getText() == "A" );
    composition.clear();
    SW_EXPECT_TRUE( composition.isEmpty() );
}

/**
 * @brief [WindowTextInputTest] `done` 이 묶음을 적용한다 - 확정 글이 나오고, 다음 묶음에 실리지 않은 조합은 빈 글이 된다(text-input-v3)
 */
SW_TEST_CASE( WindowTextInputTest, FrameAppliesOnDone )
{
    WindowTextInputFrame frame;
    string               commit;
    string               preedit;

    frame.setPendingPreedit( "ko" );
    SW_EXPECT_TRUE( frame.finish( commit, preedit ) );
    SW_EXPECT_TRUE( commit.empty() );
    SW_EXPECT_TRUE( preedit == "ko" );

    frame.setPendingPreedit( "ko" ); // 같은 조합
    SW_EXPECT_FALSE( frame.finish( commit, preedit ) );

    frame.setPendingCommit( "kor" ); // 조합 없는 묶음이다 - 확정되며 조합이 지워진다
    SW_EXPECT_TRUE( frame.finish( commit, preedit ) );
    SW_EXPECT_TRUE( commit == "kor" );
    SW_EXPECT_TRUE( preedit.empty() );

    SW_EXPECT_FALSE( frame.finish( commit, preedit ) );
    SW_EXPECT_TRUE( commit.empty() );

    frame.setPendingPreedit( "x" );
    (void)frame.finish( commit, preedit );
    SW_EXPECT_TRUE( frame.reset() ); // 포커스를 잃었다
    SW_EXPECT_FALSE( frame.reset() );
}

/**
 * @brief [WindowTextInputTest] 원하는 쪽이 하나라도 있으면 켜져 있다. 한쪽이 꺼도 다른 쪽을 끄지 않고, 값이 바뀔 때만 플랫폼에 내려간다. 자리도 같으면 내려가지 않는다
 */
SW_TEST_CASE( WindowTextInputTest, ClientsShareTheInputMethod )
{
    FakeInputMethodWindow window;
    SW_EXPECT_FALSE( window.isTextInputWanted() );

    window.requestTextInput( WindowTextInputClient::Engine, true );
    window.requestTextInput( WindowTextInputClient::Editor, true );
    SW_EXPECT_TRUE( window.isTextInputWanted() );
    SW_EXPECT_EQUAL( uint32{ 1 }, window._wantedChangeCount );

    window.requestTextInput( WindowTextInputClient::Engine, false );
    SW_EXPECT_TRUE( window.isTextInputWanted() );
    SW_EXPECT_EQUAL( uint32{ 1 }, window._wantedChangeCount );

    window.requestTextInput( WindowTextInputClient::Editor, false );
    SW_EXPECT_FALSE( window.isTextInputWanted() );
    SW_EXPECT_EQUAL( uint32{ 2 }, window._wantedChangeCount );

    WindowTextInputArea area;
    area._position   = int2{ 10, 20 };
    area._lineHeight = 16;
    window.setTextInputArea( area );
    window.setTextInputArea( area );
    SW_EXPECT_EQUAL( uint32{ 1 }, window._areaChangeCount );
}

/**
 * @brief [WindowTextInputTest] 조합 → 확정이 창에서 입력 관리자의 글 콜백까지 간다. 키보드 포커스 주인이 받고, 조합이 끝나면 빈 조합이 온다(막으면 위젯에 지운 글이 남는다)
 */
SW_TEST_CASE( WindowTextInputTest, CompositionThenCommitReachesTheFocusOwner )
{
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );
    WindowTextInputTestUtil util;
    util._pInput = &input;

    FakeInputMethodWindow window;
    const Subscription    windowSubscription = window.bindCustomMessageHandler( SW_DELEGATE_METHOD( WindowMessageHandlerDelegate, &WindowTextInputTestUtil::onWindowMessage, &util ) );
    const Subscription    compositionSubscription =
        input.bindTextCompositionCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &WindowTextInputTestUtil::onComposition, &util ), InputKeyboardFocus::UI );
    const Subscription commitSubscription = input.bindTextInputCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &WindowTextInputTestUtil::onCommit, &util ), InputKeyboardFocus::UI );
    input.setKeyboardFocus( InputKeyboardFocus::UI );

    window.imeEdit( 0, 0, "\xE3\x85\x8E" ); // ㅎ
    window.imeEdit( 0, 1, "\xED\x95\x98" ); // 하
    window.imeEdit( 0, 1, "\xED\x95\x9C" ); // 한
    window.imeCommit( "\xED\x95\x9C" );
    input.beginFrame();
    input.endFrame();

    SW_REQUIRE_EQUAL( size_t{ 4 }, util._listComposition.size() );
    SW_EXPECT_TRUE( util._listComposition[0] == "\xE3\x85\x8E" );
    SW_EXPECT_TRUE( util._listComposition[2] == "\xED\x95\x9C" );
    SW_EXPECT_TRUE( util._listComposition[3].empty() );
    SW_EXPECT_TRUE( util._committed == "\xED\x95\x9C" );

    // 포커스가 게임이면 UI 콜백은 받지 않는다.
    input.setKeyboardFocus( InputKeyboardFocus::Game );
    window.imeCommit( "x" );
    input.beginFrame();
    input.endFrame();
    SW_EXPECT_TRUE( util._committed == "\xED\x95\x9C" );

    input.shutdown();
}

/**
 * @brief [WindowTextInputTest] 글 이벤트 칸보다 긴 확정 글은 글자 경계에서 나뉘어 잃는 글자 없이 온다(입력기는 문장을 한 번에 확정한다)
 */
SW_TEST_CASE( WindowTextInputTest, LongCommitIsSplitOnCharacterBoundaries )
{
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );
    WindowTextInputTestUtil util;
    util._pInput                          = &input;
    const Subscription commitSubscription = input.bindTextInputCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &WindowTextInputTestUtil::onCommit, &util ), InputKeyboardFocus::Game );

    string longText;
    for ( int32 index = 0; index < 40; ++index )
    {
        longText += "\xED\x95\x9C"; // 3 바이트 글자 40 개 - 120 바이트
    }
    WindowTextInputEvent event;
    event._kind = WindowTextInputKind::Commit;
    event._text = longText;
    input.postWindowTextInput( event );
    input.beginFrame();
    input.endFrame();

    SW_EXPECT_TRUE( util._committed == longText );
    SW_EXPECT_TRUE( util._commitCount > 1 );

    input.shutdown();
}
