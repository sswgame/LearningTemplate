#include "pch.h"

#include "Engine/Input/RawInputEvent.h"
#include "Engine/UI/Input/UIInputRouter.h"
#include "Engine/UI/Screen/UIScreen.h"

#include "EngineTest/UI/UITestWidgets.h"

#include "TestFramework/TestFramework.h"

// UIInputRouterTest — 입력 라우팅 규칙(엔진 · 입력 관리자 없음): 원시 이벤트가 가리키는 입력 방식, 입력 방식 바꾸기, 화면이 활성이 될 때의 포커스 복원. 디바이스 없음(nogpu).

namespace
{
    struct UIInputRouterTestUtil
    {
        /** @brief 포커스를 받는 상자 A · B 를 가진 화면입니다. 기본 포커스는 @p defaultFocus 입니다(비우면 첫 위젯). */
        static sw::unique_ptr<sw::UIScreen> makeScreen( const sw::hashed_string& defaultFocus )
        {
            sw::unique_ptr<sw::uitest::TestPanelWidget> root = sw::make_unique<sw::uitest::TestPanelWidget>( "Root" );
            (void)root->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "A", true ) );
            (void)root->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "B", true ) );
            sw::UIScreenDesc desc{};
            desc._defaultFocus = defaultFocus;
            return sw::make_unique<sw::UIScreen>( desc, std::move( root ) );
        }

        static sw::WidgetID findID( sw::UIScreen& screen, const sw::hashed_string& name )
        {
            const sw::Widget* pWidget = screen.getTree().findWidgetByName( name );
            return pWidget != nullptr ? pWidget->getID() : sw::kInvalidWidgetID;
        }
    };
} // namespace

/** @brief [UIInputRouterTest] 키 · 패드 버튼 · 크게 기울인 스틱은 탐색, 마우스 이동 · 버튼 · 휠은 포인터, 스틱 떨림과 키 떼기는 방식과 무관하다 */
SW_TEST_CASE( UIInputRouterTest, RawEventSelectsInputMode )
{
    sw::UIInputMode mode = sw::UIInputMode::Pointer;
    SW_EXPECT_TRUE( sw::UIInputRouter::tryGetInputMode( sw::RawInputEvent::makeKeyDown( sw::Key::W ), mode ) );
    SW_EXPECT_TRUE( mode == sw::UIInputMode::Navigation );
    mode = sw::UIInputMode::Pointer;
    SW_EXPECT_TRUE( sw::UIInputRouter::tryGetInputMode( sw::RawInputEvent::makeGamepadButtonDown( sw::GamepadButton::A ), mode ) );
    SW_EXPECT_TRUE( mode == sw::UIInputMode::Navigation );
    mode = sw::UIInputMode::Pointer;
    SW_EXPECT_TRUE( sw::UIInputRouter::tryGetInputMode( sw::RawInputEvent::makeGamepadAxis( 0, 0.9f ), mode ) );
    SW_EXPECT_TRUE( mode == sw::UIInputMode::Navigation );

    SW_EXPECT_TRUE( sw::UIInputRouter::tryGetInputMode( sw::RawInputEvent::makeMouseMove( 10, 10 ), mode ) );
    SW_EXPECT_TRUE( mode == sw::UIInputMode::Pointer );
    mode = sw::UIInputMode::Navigation;
    SW_EXPECT_TRUE( sw::UIInputRouter::tryGetInputMode( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left ), mode ) );
    SW_EXPECT_TRUE( mode == sw::UIInputMode::Pointer );
    mode = sw::UIInputMode::Navigation;
    SW_EXPECT_TRUE( sw::UIInputRouter::tryGetInputMode( sw::RawInputEvent::makeMouseWheel( 1.0f ), mode ) );
    SW_EXPECT_TRUE( mode == sw::UIInputMode::Pointer );

    mode = sw::UIInputMode::Pointer;
    SW_EXPECT_FALSE( sw::UIInputRouter::tryGetInputMode( sw::RawInputEvent::makeGamepadAxis( 0, 0.1f ), mode ) ); // 손을 뗀 스틱의 떨림
    SW_EXPECT_FALSE( sw::UIInputRouter::tryGetInputMode( sw::RawInputEvent::makeKeyUp( sw::Key::W ), mode ) );
    SW_EXPECT_FALSE( sw::UIInputRouter::tryGetInputMode( sw::RawInputEvent::makeFocusChange( true ), mode ) );
    SW_EXPECT_TRUE( mode == sw::UIInputMode::Pointer ); // 무관한 이벤트는 방식을 쓰지 않는다
}

/** @brief [UIInputRouterTest] 입력 방식은 바뀔 때만 true 이고, 포인터 방식에서는 기억한 포커스가 없으면 포커스를 잡지 않는다 */
SW_TEST_CASE( UIInputRouterTest, PointerModeDoesNotPickDefaultFocus )
{
    sw::UIInputRouter            router;
    sw::unique_ptr<sw::UIScreen> screen = UIInputRouterTestUtil::makeScreen( "B" );
    SW_EXPECT_TRUE( router.getInputMode() == sw::UIInputMode::Pointer );
    SW_EXPECT_FALSE( router.setInputMode( sw::UIInputMode::Pointer ) );

    router.restoreFocus( *screen );
    SW_EXPECT_NULL( router.getFocusManager().getFocusedTree() );

    SW_EXPECT_TRUE( router.setInputMode( sw::UIInputMode::Navigation ) );
    SW_EXPECT_FALSE( router.setInputMode( sw::UIInputMode::Navigation ) );
    router.forgetScreen( *screen );
}

/** @brief [UIInputRouterTest] 탐색 방식에서는 기본 포커스(없으면 첫 위젯)를 잡는다 */
SW_TEST_CASE( UIInputRouterTest, NavigationModeRestoresDefaultFocus )
{
    sw::UIInputRouter router;
    SW_ASSERT_TRUE( router.setInputMode( sw::UIInputMode::Navigation ) );

    sw::unique_ptr<sw::UIScreen> withDefault = UIInputRouterTestUtil::makeScreen( "B" );
    router.restoreFocus( *withDefault );
    SW_EXPECT_TRUE( router.getFocusManager().getFocusedTree() == &withDefault->getTree() );
    SW_EXPECT_EQUAL( UIInputRouterTestUtil::findID( *withDefault, "B" ), router.getFocusManager().getFocusedWidget() );

    sw::unique_ptr<sw::UIScreen> withoutDefault = UIInputRouterTestUtil::makeScreen( sw::hashed_string{} );
    router.restoreFocus( *withoutDefault );
    SW_EXPECT_TRUE( router.getFocusManager().getFocusedTree() == &withoutDefault->getTree() );
    SW_EXPECT_EQUAL( UIInputRouterTestUtil::findID( *withoutDefault, "A" ), router.getFocusManager().getFocusedWidget() );

    // 화면을 지우기 전에 포인터와 포커스가 트리를 놓게 한다(UISystem::destroyScreenAt 과 같은 순서).
    router.forgetScreen( *withoutDefault );
    SW_EXPECT_NULL( router.getFocusManager().getFocusedTree() );
    router.forgetScreen( *withDefault );
}
