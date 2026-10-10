#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/UIFocusManager.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"

#include "EngineTest/UI/UILayoutTestUtil.h"
#include "EngineTest/UI/UITestWidgets.h"

#include "TestFramework/TestFramework.h"

// UIInputTest — 행동 층 입력 → UI 사건: UI 행동 맵(`engine/input/ui.input.xml`)의 확인 · 탐색 · 스틱, UI 가 쓴 물리 입력은 뗄 때까지 먹힌 입력,
// 입력 방식 전환, 글 입력 칸의 키보드 포커스. 패드 · 키는 가상 입력(배타)으로 넣는다. 디바이스 없음(nogpu).

namespace
{
    struct UIInputTestUtil
    {
        static constexpr float32     kFrameSeconds = 1.0f / 60.0f;
        static constexpr const utf8* kUIInputMap   = "engine/input/ui.input.xml";
    };

    /** @brief 입력 관리자 · UI · 버튼 세로 목록 메뉴입니다. 게임 맵: Jump = 패드 A, Fire = 마우스 왼쪽, MoveForward = W, Move = 왼쪽 스틱. */
    struct UIInputFixture
    {
        sw::InputManager                       _input;
        sw::UISystem                           _ui;
        sw::VirtualInputScript                 _script;
        sw::vector<sw::uitest::TestBoxWidget*> _listButton;
        sw::UIViewport                         _viewport; ///< `endFrame` 이 UI 에 넘기는 화면(기본 1920 × 1080, 배율 1)
        sw::UIScreenHandle                     _menu;

        UIInputFixture()
            : _input{}
            , _ui{}
            , _script{}
            , _listButton{}
            , _viewport{}
            , _menu{ sw::kInvalidUIScreenHandle }
        {
            _viewport._size         = sw::float2{ 1920.0f, 1080.0f };
            _viewport._physicalSize = _viewport._size;
            SW_EXPECT_TRUE( _input.initialize() );
            sw::InputMap& gameMap = _input.getInputMap();
            gameMap.bind( "Jump", sw::GamepadButton::A );
            gameMap.bind( "Fire", sw::MouseButton::Left );
            gameMap.bind( "MoveForward", sw::Key::W, sw::ActionTrigger::Down );
            gameMap.bindGamepadStick2D( "Move" );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr, UIInputTestUtil::kUIInputMap ) );
            SW_EXPECT_NOT_NULL( _ui.getUIInputMap() );
        }

        ~UIInputFixture()
        {
            if ( _input.isVirtualInputAttached() )
                _input.detachVirtualInput();
            _ui.shutdown();
            _input.shutdown();
        }

        UIInputFixture( const UIInputFixture& )            = delete;
        UIInputFixture& operator=( const UIInputFixture& ) = delete;

        /** @brief 버튼 @p count 개(세로, 200 × 40, 틈 10)인 메뉴 화면을 엽니다. */
        void openMenu( uint32 count )
        {
            auto root = sw::make_unique<sw::uitest::TestPanelWidget>( "menuRoot" );
            sw::uitest::UITestUtil::placeWidget( *root, 0.0f, 0.0f, 800.0f, 600.0f );
            _listButton.clear();
            for ( uint32 index = 0; index < count; ++index )
            {
                auto* pButton = static_cast<sw::uitest::TestBoxWidget*>(
                    root->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( sw::hashed_string( "b" + sw::to_string( index ) ), true ) ) );
                sw::uitest::UITestUtil::placeWidget( *pButton, 100.0f, 100.0f + static_cast<float32>( index ) * 50.0f, 200.0f, 40.0f );
                _listButton.push_back( pButton );
            }
            _menu = _ui.pushScreen( sw::make_unique<sw::UIScreen>( sw::UIScreenDesc{}, std::move( root ) ) );
        }

        bool focus( uint32 index ) { return _ui.getFocusManager().setFocus( _ui.findScreen( _menu )->getTree(), _listButton[index]->getID() ); }

        /** @brief 지금 포커스 버튼의 자리입니다(없으면 -1). */
        int32 getFocusedIndex() const
        {
            for ( uint32 index = 0; index < static_cast<uint32>( _listButton.size() ); ++index )
            {
                if ( _listButton[index]->hasFocus() )
                    return static_cast<int32>( index );
            }
            return -1;
        }

        /** @brief 가상 입력을 붙입니다(배타 — 패드 0 연결 사건이 0 프레임에 든다). 프레임 번호는 붙인 뒤 `beginFrame` 수입니다. */
        void attachScript()
        {
            _script.addEvent( 0, sw::RawInputEvent::makeGamepadConnection( 0, true ) );
            _input.attachVirtualInput( &_script, sw::VirtualInputMode::Exclusive );
        }

        void beginFrame()
        {
            _input.beginFrame( UIInputTestUtil::kFrameSeconds );
            _ui.processInput( UIInputTestUtil::kFrameSeconds );
        }

        void endFrame()
        {
            _ui.update( UIInputTestUtil::kFrameSeconds, _viewport );
            _input.endFrame();
        }

        void runFrame()
        {
            beginFrame();
            endFrame();
        }

        /** @brief 게임이 보는 "발동" — 입력 맵이 발동했고 UI 가 먹은 입력이 아니다(플레이어 조종자와 같은 질의). */
        bool wasTriggeredForGame( const sw::hashed_string& action ) const
        {
            const sw::InputMap& gameMap = _input.getInputMap();
            return gameMap.wasActionTriggered( action ) && _ui.isActionConsumed( gameMap, action ) == false;
        }

        bool isDownForGame( const sw::hashed_string& action ) const
        {
            const sw::InputMap& gameMap = _input.getInputMap();
            return gameMap.isActionDown( action ) && _ui.isActionConsumed( gameMap, action ) == false;
        }
    };
} // namespace

/**
 * @brief [UIInputTest] 메뉴 버튼에 포커스가 있을 때 패드 A 는 버튼 클릭이고 게임의 Jump 는 발화하지 않는다 — 누른 채 다음 프레임에도 안 보이고, 화면을 닫고 다시 누르면 보인다
 * @details 변이: `UIInputConsumption::consumeAction` 본문을 비우면 진다.
 */
SW_TEST_CASE( UIInputTest, AcceptOnFocusedButtonClicksAndConsumes )
{
    UIInputFixture fixture;
    fixture.openMenu( 2 );
    SW_ASSERT_TRUE( fixture.focus( 0 ) );
    SW_ASSERT_TRUE( fixture._script.addSlot( 1, sw::InputSlot::fromGamepadButton( sw::GamepadButton::A ), true ) );
    SW_ASSERT_TRUE( fixture._script.addSlot( 3, sw::InputSlot::fromGamepadButton( sw::GamepadButton::A ), false ) );
    SW_ASSERT_TRUE( fixture._script.addSlot( 6, sw::InputSlot::fromGamepadButton( sw::GamepadButton::A ), true ) );
    fixture.attachScript();

    fixture.runFrame();   // 0 — 패드 연결
    fixture.beginFrame(); // 1 — A 누름
    SW_EXPECT_EQUAL( 1u, fixture._listButton[0]->_clickCount );
    SW_EXPECT_TRUE( fixture._input.getInputMap().wasActionTriggered( "Jump" ) ); // 맵은 발동했지만
    SW_EXPECT_FALSE( fixture.wasTriggeredForGame( "Jump" ) );                    // UI 가 먹었다
    fixture.endFrame();
    fixture.beginFrame(); // 2 — 누른 채
    SW_EXPECT_FALSE( fixture.isDownForGame( "Jump" ) );
    fixture.endFrame();
    fixture.runFrame(); // 3 — 뗌(뗀 프레임까지 먹힌 채)
    fixture._ui.closeScreen( fixture._menu );
    fixture.runFrame();   // 4 — 화면 닫힘
    fixture.runFrame();   // 5
    fixture.beginFrame(); // 6 — 다시 누름: 화면이 없으니 게임의 것
    SW_EXPECT_TRUE( fixture.wasTriggeredForGame( "Jump" ) );
    fixture.endFrame();
}

/** @brief [UIInputTest] 포커스 받는 화면이 없으면(HUD 만) UI 행동 레이어가 꺼져 패드 A 는 게임의 Jump 다 */
SW_TEST_CASE( UIInputTest, UnhandledActionReachesGame )
{
    UIInputFixture   fixture;
    sw::UIScreenDesc hudDesc{};
    hudDesc._layer = sw::UILayer::HUD;
    auto hudRoot   = sw::make_unique<sw::uitest::TestPanelWidget>( "hud" );
    (void)hudRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "hudButton", true ) );
    (void)fixture._ui.pushScreen( sw::make_unique<sw::UIScreen>( hudDesc, std::move( hudRoot ) ) );
    SW_ASSERT_TRUE( fixture._script.addSlot( 1, sw::InputSlot::fromGamepadButton( sw::GamepadButton::A ), true ) );
    fixture.attachScript();

    fixture.runFrame();
    fixture.beginFrame();
    SW_EXPECT_TRUE( fixture.wasTriggeredForGame( "Jump" ) );
    SW_EXPECT_FALSE( fixture._ui.getInputConsumption().hasConsumedInput() );
    fixture.endFrame();
}

/** @brief [UIInputTest] D-pad 아래를 누르고 있으면 반복한다 — 반복 지연 0.4 · 간격 0.1 에서 약 0.53 초면 세 칸(누를 때 · 0.4 · 0.5) */
SW_TEST_CASE( UIInputTest, DPadRepeatsWhileHeld )
{
    UIInputFixture fixture;
    fixture._ui.getUIInputMap()->setNavRepeatTiming( 0.4f, 0.1f );
    fixture.openMenu( 6 );
    SW_ASSERT_TRUE( fixture.focus( 0 ) );
    SW_ASSERT_TRUE( fixture._script.addSlot( 1, sw::InputSlot::fromGamepadButton( sw::GamepadButton::DPadDown ), true ) );
    SW_ASSERT_TRUE( fixture._script.addSlot( 34, sw::InputSlot::fromGamepadButton( sw::GamepadButton::DPadDown ), false ) );
    fixture.attachScript();

    for ( uint32 frame = 0; frame < 40; ++frame )
    {
        fixture.runFrame();
    }
    SW_EXPECT_EQUAL( 3, fixture.getFocusedIndex() );
}

/** @brief [UIInputTest] 스틱을 아래로 크게 기울이고 있으면 한 칸 옮긴 뒤 반복 간격으로 더 옮긴다 — 그 동안 게임의 Move(같은 스틱)는 먹힌 입력이다 */
SW_TEST_CASE( UIInputTest, StickNavigatesOnceThenRepeats )
{
    UIInputFixture fixture;
    fixture._ui.getUIInputMap()->setNavRepeatTiming( 0.4f, 0.1f );
    fixture.openMenu( 6 );
    SW_ASSERT_TRUE( fixture.focus( 0 ) );
    fixture._script.addGamepadAxis( 1, 1, -0.9f ); // 왼쪽 스틱 y — 아래
    fixture.attachScript();

    fixture.runFrame();   // 0
    fixture.beginFrame(); // 1 — 기울임: 한 칸
    SW_EXPECT_EQUAL( 1, fixture.getFocusedIndex() );
    SW_EXPECT_TRUE( fixture._input.getInputMap().isActionDown( "Move" ) );
    SW_EXPECT_FALSE( fixture.isDownForGame( "Move" ) );
    fixture.endFrame();
    for ( uint32 frame = 2; frame < 20; ++frame ) // 0.3 초 — 아직 반복 전
    {
        fixture.runFrame();
    }
    SW_EXPECT_EQUAL( 1, fixture.getFocusedIndex() );
    for ( uint32 frame = 20; frame < 28; ++frame ) // 0.45 초 — 첫 반복
    {
        fixture.runFrame();
    }
    SW_EXPECT_EQUAL( 2, fixture.getFocusedIndex() );
}

/** @brief [UIInputTest] 위젯이 처리한 클릭은 게임의 Fire(마우스 왼쪽)를 막고, 빈 곳 클릭은 막지 않는다 */
SW_TEST_CASE( UIInputTest, MouseClickOnWidgetIsConsumed )
{
    UIInputFixture fixture;
    fixture.openMenu( 1 );
    fixture._listButton[0]->_bHandleBubble = true;

    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseMove( 150, 110 ) ) );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, 150, 110 ) ) );
    fixture.beginFrame();
    SW_EXPECT_TRUE( fixture._input.getInputMap().wasActionTriggered( "Fire" ) );
    SW_EXPECT_FALSE( fixture.wasTriggeredForGame( "Fire" ) );
    fixture.endFrame();
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Left, 150, 110 ) ) );
    fixture.runFrame();
    fixture.runFrame();

    // 빈 곳(메뉴 루트는 처리하지 않는다).
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseMove( 600, 500 ) ) );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, 600, 500 ) ) );
    fixture.beginFrame();
    SW_EXPECT_TRUE( fixture.wasTriggeredForGame( "Fire" ) );
    fixture.endFrame();
}

/**
 * @brief [UIInputTest] 입력 방식은 마지막으로 쓴 장치를 따른다 — 패드 버튼 뒤 탐색(포커스가 기본 위젯으로), 마우스가 실제로 움직인 뒤 포인터.
 *        같은 자리의 이동 사건(창이 뜰 때 OS 가 보낸다)은 방식을 바꾸지 않는다
 * @details 변이: `UISystem::updateInputMode` 의 자리 비교를 빼면 3 프레임에 포인터로 바뀌어 진다.
 */
SW_TEST_CASE( UIInputTest, InputModeSwitchesWithLastDevice )
{
    UIInputFixture fixture;
    fixture.openMenu( 2 );
    SW_EXPECT_TRUE( fixture._ui.getInputMode() == sw::UIInputMode::Pointer );
    SW_EXPECT_EQUAL( -1, fixture.getFocusedIndex() );
    fixture._script.addEvent( 0, sw::RawInputEvent::makeMouseMove( 640, 480 ) ); // 창이 뜰 때의 자리(이동 아님)
    SW_ASSERT_TRUE( fixture._script.addTap( 1, sw::InputSlot::fromGamepadButton( sw::GamepadButton::X ), 1 ) );
    fixture._script.addEvent( 3, sw::RawInputEvent::makeMouseMove( 640, 480 ) ); // 같은 자리 — 탐색 방식이 남는다
    fixture._script.addEvent( 4, sw::RawInputEvent::makeMouseMove( 700, 500 ) );
    fixture.attachScript();

    fixture.runFrame();
    fixture.runFrame(); // 1 — 패드 X(UI 행동 아님)
    SW_EXPECT_TRUE( fixture._ui.getInputMode() == sw::UIInputMode::Navigation );
    SW_EXPECT_EQUAL( 0, fixture.getFocusedIndex() ); // 탐색으로 바뀌는 순간 기본 포커스
    fixture.runFrame();
    fixture.runFrame(); // 3 — 같은 자리 이동 사건
    SW_EXPECT_TRUE( fixture._ui.getInputMode() == sw::UIInputMode::Navigation );
    fixture.runFrame(); // 4 — 마우스 이동
    SW_EXPECT_TRUE( fixture._ui.getInputMode() == sw::UIInputMode::Pointer );
}

/**
 * @brief [UIInputTest] 글 입력 칸이 포커스를 쥔 동안 키보드 포커스는 UI 다 — W 를 눌러도 게임의 MoveForward 가 보이지 않고 글자는 칸에 간다.
 *        칸을 떠나면 게임으로 돌아오지만, 칸에 있는 동안 눌린 W 는 뗄 때까지 보이지 않는다
 */
SW_TEST_CASE( UIInputTest, TextFieldTakesKeyboardFocus )
{
    UIInputFixture fixture;
    fixture.openMenu( 2 );
    fixture._listButton[0]->_bTextInput = true;
    SW_ASSERT_TRUE( fixture.focus( 0 ) );
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture._input.getKeyboardFocus() == sw::InputKeyboardFocus::UI );

    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::W ) ) );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeTextInput( "w" ) ) );
    fixture.beginFrame();
    SW_EXPECT_FALSE( fixture._input.getInputMap().isActionDown( "MoveForward" ) );
    SW_EXPECT_STREQ( "w", fixture._listButton[0]->_receivedText.c_str() );
    fixture.endFrame();

    // 칸을 떠난다(W 는 누른 채).
    SW_ASSERT_TRUE( fixture.focus( 1 ) );
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture._input.getKeyboardFocus() == sw::InputKeyboardFocus::Game );
    fixture.runFrame();
    SW_EXPECT_FALSE( fixture._input.getInputMap().isActionDown( "MoveForward" ) );

    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::W ) ) );
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::W ) ) );
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture._input.getInputMap().isActionDown( "MoveForward" ) );
}

/**
 * @brief [UIInputTest] 마우스 위치는 창 픽셀을 UI 배율로 나눈 UI 단위다 — 배율 2 에서 창 (300, 220) 은 UI (150, 110), 첫 버튼 위다
 * @details 변이: `UISystem::processPointer` 의 배율 나누기를 빼면 (300, 220) 은 버튼 밖이라 게임이 Fire 를 본다.
 */
SW_TEST_CASE( UIInputTest, PointerPositionIsInUIUnits )
{
    UIInputFixture fixture;
    fixture._viewport._size         = sw::float2{ 960.0f, 540.0f };
    fixture._viewport._physicalSize = sw::float2{ 1920.0f, 1080.0f };
    fixture._viewport._uiScale      = 2.0f;
    fixture.openMenu( 1 );
    fixture._listButton[0]->_bHandleBubble = true;
    fixture.runFrame(); // 배율 뷰포트를 UI 에 한 번 넘긴다

    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseMove( 300, 220 ) ) );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, 300, 220 ) ) );
    fixture.beginFrame();
    SW_EXPECT_TRUE( fixture._input.getInputMap().wasActionTriggered( "Fire" ) );
    SW_EXPECT_FALSE( fixture.wasTriggeredForGame( "Fire" ) );
    SW_EXPECT_EQUAL( fixture._listButton[0]->getID(), fixture._ui.getPointerState().getHoveredWidget() );
    fixture.endFrame();
}

/**
 * @brief [UIInputTest] 오른쪽 스틱(`UI.Scroll`)은 포커스 항목이 든 스크롤 목록을 스틱 속도 × 시간만큼 옮기고, 쓴 스틱은 먹힌 입력이다
 * @details 아래로 끝까지 기울이면 1200 × 1/60 = 프레임마다 20. 변이: `UISystem::processScroll` 을 부르지 않으면 오프셋이 0 으로 남는다.
 */
SW_TEST_CASE( UIInputTest, RightStickScrollsFocusedList )
{
    UIInputFixture                  fixture;
    sw::unique_ptr<sw::ScrollPanel> root     = sw::make_unique<sw::ScrollPanel>();
    sw::ScrollPanel*                pScroll  = root.get();
    sw::BoxPanel*                   pContent = static_cast<sw::BoxPanel*>( pScroll->addChild( sw::make_unique<sw::BoxPanel>() ) );
    pContent->setOrientation( sw::UIOrientation::Vertical );
    sw::Widget* pFirst = pContent->addChild( sw::make_unique<sw::test::TestFocusableFixedWidget>( "first", sw::float2{ 100.0f, 1000.0f } ) );
    pContent->addChild( sw::make_unique<sw::test::TestFocusableFixedWidget>( "second", sw::float2{ 100.0f, 1000.0f } ) );
    const sw::UIScreenHandle screen = fixture._ui.pushScreen( sw::make_unique<sw::UIScreen>( sw::UIScreenDesc{}, std::move( root ) ) );
    fixture.runFrame(); // 놓는다
    SW_ASSERT_TRUE( fixture._ui.getFocusManager().setFocus( fixture._ui.findScreen( screen )->getTree(), pFirst->getID() ) );
    fixture._script.addGamepadAxis( 1, 3, -1.0f ); // 오른쪽 스틱 y — 아래
    fixture.attachScript();

    fixture.runFrame();   // 0
    fixture.beginFrame(); // 1 — 기울임
    SW_EXPECT_NEAR_EQUAL( 20.0f, pScroll->getScrollOffset()._y, 0.01f );
    fixture.endFrame();
    for ( uint32 frame = 2; frame < 6; ++frame )
    {
        fixture.runFrame();
    }
    SW_EXPECT_NEAR_EQUAL( 100.0f, pScroll->getScrollOffset()._y, 0.01f );
}
