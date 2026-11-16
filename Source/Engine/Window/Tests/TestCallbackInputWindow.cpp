#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Window/CallbackInputWindow.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/PlatformInputEvent.h"

using namespace sw;

namespace
{
    /** @brief 콜백(웹 · Android) 창의 바탕만 있는 창입니다. 플랫폼 콜백 대신 테스트가 포커스를 바꾼다. */
    class FakeCallbackWindow final : public CallbackInputWindow
    {
    public:
        bool              initializeWindow( const utf8* /*pTitle*/, uint32 /*width*/, uint32 /*height*/ ) override { return true; }
        void              destroy() override {}
        NativeSurfaceKind getSurfaceKind() const override { return NativeSurfaceKind::WebCanvas; }
        void              simulateFocus( bool bFocused ) { applyPlatformFocus( bFocused ); }

    protected:
        void* getNativeHandle() const override { return nullptr; }
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
