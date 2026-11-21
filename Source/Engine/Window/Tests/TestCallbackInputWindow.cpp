#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Window/CallbackInputWindow.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/PlatformInputEvent.h"
#include "Engine/Window/WindowTextInput.h"

using namespace sw;

namespace
{
    /** @brief 콜백(웹 · Android) 창의 바탕만 있는 창입니다. 플랫폼 콜백 대신 테스트가 포커스를 바꾼다. */
    class FakeCallbackWindow final : public CallbackInputWindow
    {
    public:
        uint32 _wantedChangeCount{ 0 };

        bool              initializeWindow( const utf8* /*pTitle*/, uint32 /*width*/, uint32 /*height*/ ) override { return true; }
        void              destroy() override {}
        NativeSurfaceKind getSurfaceKind() const override { return NativeSurfaceKind::WebCanvas; }
        bool              supportsPointerLock() const override { return false; }
        bool              setPointerLock( WindowPointerLock lock ) override { return lock == WindowPointerLock::Released; }
        bool              supportsCursorShape() const override { return false; }
        bool              setCursorShape( CursorShape /*shape*/ ) override { return false; }
        bool              setCursorVisible( bool /*bVisible*/ ) override { return false; }
        // 콜백 창 바탕은 끌어 놓기를 정하지 않는다(웹 · Android 가 저마다 None) — 이 가짜도 받지 않는다.
        WindowFileDropSupport getFileDropSupport() const override { return WindowFileDropSupport::None; }
        void                  simulateFocus( bool bFocused ) { applyPlatformFocus( bFocused ); }
        void                  simulateKey( PlatformInputKind kind )
        {
            PlatformInputEvent input{};
            input._kind = kind;
            queueInput( input );
        }
        void simulateText( WindowTextInputKind kind, string_view text ) { queueTextInput( kind, text ); }

    protected:
        void* getNativeHandle() const override { return nullptr; }
        void  applyTextInputWanted( bool /*bWanted*/ ) override { ++_wantedChangeCount; }
        void  applyTextInputArea( const WindowTextInputArea& /*area*/ ) override {}
    };
} // namespace

/**
 * @brief [CallbackInputWindowTest] 포커스가 바뀌면 상태와 함께 입력 관리자가 받을 포커스 입력이 나간다 — 웹 · Android 에서 포커스를 잃어도 눌린 키가 남던 결함(감사 P-3).
 *        같은 값이 겹쳐 오면(브라우저 focus 두 번) 한 번만 나간다.
 */
SW_TEST_CASE( CallbackInputWindowTest, FocusChangeIsDeliveredAsInput )
{
    FakeCallbackWindow        window;
    vector<PlatformInputKind> listKind;
    const Subscription        subscription = window.bindCustomMessageHandler( SW_DELEGATE_LAMBDA( WindowMessageHandlerDelegate, [&listKind]( const NativeWindowEvent& event )
           {
        if ( event._message == NativeWindowEvent::kMessagePlatformInput && event._lParam != 0 )
            listKind.push_back( reinterpret_cast<const PlatformInputEvent*>( event._lParam )->_kind );
        return true;
    } ) );

    window.simulateFocus( true );
    window.simulateFocus( true );
    window.simulateFocus( false );
    SW_EXPECT_FALSE( window.hasFocus() );
    SW_EXPECT_TRUE( window.processMessages() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listKind.size() );
    SW_EXPECT_TRUE( listKind[0] == PlatformInputKind::FocusGained );
    SW_EXPECT_TRUE( listKind[1] == PlatformInputKind::FocusLost );
}

/**
 * @brief [CallbackInputWindowTest] 콜백 창(웹 숨은 글 칸 · Android 화면 키보드)의 조합 · 확정 글이 키와 받은 차례 그대로, 데스크톱 창과 같은 글 메시지(`kMessageTextInput`)로 나간다(감사 P-4).
 *        확정 글은 자르지 않는다 — 예전에는 플랫폼 입력 한 칸(`PlatformInputEvent::_arrText`, 32 바이트)에 실어 긴 확정(자동 완성 낱말 · 화면 키보드의 붙여넣기)이 잘렸다.
 */
SW_TEST_CASE( CallbackInputWindowTest, TextInputKeepsOrderWithKeys )
{
    FakeCallbackWindow window;
    vector<string>     listReceived;
    const Subscription subscription = window.bindCustomMessageHandler( SW_DELEGATE_LAMBDA( WindowMessageHandlerDelegate, [&listReceived]( const NativeWindowEvent& event )
    {
        if ( event._message == NativeWindowEvent::kMessagePlatformInput && event._lParam != 0 )
            listReceived.emplace_back( reinterpret_cast<const PlatformInputEvent*>( event._lParam )->_kind == PlatformInputKind::KeyDown ? "down" : "up" );
        if ( event._message == NativeWindowEvent::kMessageTextInput && event._lParam != 0 )
        {
            const WindowTextInputEvent&              text = *reinterpret_cast<const WindowTextInputEvent*>( event._lParam );
            StringBuilder<constant::buffer::kMax256> received;
            received.appendFormat( "%#%#", text._kind == WindowTextInputKind::Commit ? "commit:" : "composition:", text._text );
            listReceived.emplace_back( received.view() );
        }
        return true;
    } ) );

    const string longCommit( 100, 'a' );
    window.simulateText( WindowTextInputKind::Commit, "x" ); // 첫 키보다 먼저 온 글
    window.simulateKey( PlatformInputKind::KeyDown );
    window.simulateText( WindowTextInputKind::Composition, "\xED\x95\x9C" ); // 한
    window.simulateText( WindowTextInputKind::Commit, longCommit );
    window.simulateKey( PlatformInputKind::KeyUp );
    window.simulateText( WindowTextInputKind::Composition, string_view{} ); // 마지막 키 뒤의 글
    SW_EXPECT_TRUE( listReceived.empty() );                                 // 모았다가 메시지 처리에서 넘긴다

    SW_EXPECT_TRUE( window.processMessages() );
    SW_REQUIRE_EQUAL( size_t{ 6 }, listReceived.size() );
    SW_EXPECT_TRUE( listReceived[0] == "commit:x" );
    SW_EXPECT_TRUE( listReceived[1] == "down" );
    SW_EXPECT_TRUE( listReceived[2] == "composition:\xED\x95\x9C" );
    StringBuilder<constant::buffer::kMax256> expectedCommit;
    expectedCommit.appendFormat( "commit:%#", longCommit );
    SW_EXPECT_TRUE( string_view( listReceived[3] ) == expectedCommit.view() );
    SW_EXPECT_TRUE( listReceived[4] == "up" );
    SW_EXPECT_TRUE( listReceived[5] == "composition:" );

    listReceived.clear();
    SW_EXPECT_TRUE( window.processMessages() ); // 넘긴 것은 비웠다
    SW_EXPECT_TRUE( listReceived.empty() );
}

/**
 * @brief [CallbackInputWindowTest] 받는 쪽이 글을 원하면 콜백 창이 화면 키보드를 켜는 길(`applyTextInputWanted`)에 닿는다 — 두 받는 쪽이 겹쳐도 켜고 끄기는 한 번씩이다.
 */
SW_TEST_CASE( CallbackInputWindowTest, TextInputRequestReachesPlatform )
{
    FakeCallbackWindow window;
    window.requestTextInput( WindowTextInputClient::Engine, true );
    window.requestTextInput( WindowTextInputClient::Editor, true );
    window.requestTextInput( WindowTextInputClient::Engine, false );
    SW_EXPECT_EQUAL( 1u, window._wantedChangeCount );
    window.requestTextInput( WindowTextInputClient::Editor, false );
    SW_EXPECT_EQUAL( 2u, window._wantedChangeCount );
}

/**
 * @brief [CallbackInputWindowTest] 자동화의 포커스 이벤트(`sendFocusEvent`)가 웹 · Android 에서도 플랫폼 콜백과 같은 길(포커스 입력)로 간다.
 *        예전에는 `GenericWindow` 의 기본 구현이 조용히 false 를 돌려 콜백 창의 포커스 경로를 자동화가 볼 수 없었다.
 */
SW_TEST_CASE( CallbackInputWindowTest, SendFocusEventTakesTheInputPath )
{
    FakeCallbackWindow        window;
    vector<PlatformInputKind> listKind;
    const Subscription        subscription = window.bindCustomMessageHandler( SW_DELEGATE_LAMBDA( WindowMessageHandlerDelegate, [&listKind]( const NativeWindowEvent& event )
           {
        if ( event._message == NativeWindowEvent::kMessagePlatformInput && event._lParam != 0 )
            listKind.push_back( reinterpret_cast<const PlatformInputEvent*>( event._lParam )->_kind );
        return true;
    } ) );

    SW_EXPECT_TRUE( window.sendFocusEvent( true ) );
    SW_EXPECT_TRUE( window.sendFocusEvent( false ) );
    SW_EXPECT_TRUE( window.processMessages() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listKind.size() );
    SW_EXPECT_TRUE( listKind[0] == PlatformInputKind::FocusGained );
    SW_EXPECT_TRUE( listKind[1] == PlatformInputKind::FocusLost );
    SW_EXPECT_FALSE( window.hasFocus() );
}
