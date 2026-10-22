#include "pch.h"

#include "Engine/UI/Accessibility/IUIAccessibilityBridge.h"
#include "Engine/UI/Accessibility/UIAccessibilityActions.h"
#include "Engine/UI/Accessibility/UIAccessibilityTree.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/CheckBoxWidget.h"
#include "Engine/UI/Widgets/ComboBoxWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextInputWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "EngineTest/UI/UITestWidgets.h"

#include "TestFramework/TestFramework.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
    #include "Engine/UI/Accessibility/Windows/UiaAccessibilityBridge.h"
#endif

// UIScreenReaderTest — 스크린 리더에 내보이는 접근성 트리: 역할 · 이름 · 값 · 상태 · 읽는 순서 · 숨긴 것 빼기 · 단추 안의 글 접기, 그리고 UI 시스템이 브리지에 알리는 포커스 · 알림. 디바이스 없음(nogpu).

namespace
{
    struct UIScreenReaderTestUtil
    {
        static sw::TextWidget* addText( sw::PanelWidget& parent, const utf8* pText )
        {
            sw::unique_ptr<sw::TextWidget> text = sw::make_unique<sw::TextWidget>();
            text->setText( pText );
            sw::TextWidget* pAdded = text.get();
            (void)parent.addChild( std::move( text ) );
            return pAdded;
        }

        static sw::ButtonWidget* addButton( sw::PanelWidget& parent, const utf8* pLabel )
        {
            sw::unique_ptr<sw::ButtonWidget> button  = sw::make_unique<sw::ButtonWidget>();
            sw::ButtonWidget*                pButton = button.get();
            (void)addText( *pButton, pLabel );
            (void)parent.addChild( std::move( button ) );
            return pButton;
        }

        /** @brief 받은 알림을 기록하는 가짜 브리지입니다. */
        class FakeBridge final : public sw::IUIAccessibilityBridge
        {
        public:
            bool isClientActive() const override { return _bActive; }
            void onUIUpdated() override { ++_updateCount; }
            void notifyFocusChanged( uint32 screen, sw::WidgetID widget ) override
            {
                _lastFocusScreen = screen;
                _lastFocusWidget = widget;
                ++_focusCount;
            }
            void announce( sw::string_view text, sw::UIAnnouncePriority priority ) override
            {
                _lastText     = sw::string( text );
                _lastPriority = priority;
                ++_announceCount;
            }

            bool                   _bActive{ false };
            uint32                 _lastFocusScreen{ 0 };
            sw::WidgetID           _lastFocusWidget{ sw::kInvalidWidgetID };
            uint32                 _focusCount{ 0 };
            sw::string             _lastText{};
            sw::UIAnnouncePriority _lastPriority{ sw::UIAnnouncePriority::Polite };
            uint32                 _announceCount{ 0 };
            uint32                 _updateCount{ 0 };
        };
    };
} // namespace

/**
 * @brief [UIScreenReaderTest] 접근성 트리는 보이는 위젯을 트리 순서로 역할 · 이름 · 값 · 상태와 함께 낸다 — 단추 안의 글은 단추의 이름이 되고 따로 나오지 않는다,
 *        역할 없는 패널은 건너뛰어 자손이 위로 올라오고, 이름을 준 패널은 묶음이 된다, 접은 위젯은 자손까지 뺀다
 * @details 변이: `describeAccessible` 가 `_bLeaf` 를 켜지 않으면 단추 안의 글이 노드로 한 번 더 나와 노드 수 검사가 진다.
 */
SW_TEST_CASE( UIScreenReaderTest, TreeListsVisibleWidgetsInReadingOrder )
{
    using Util = UIScreenReaderTestUtil;
    sw::WidgetTree tree;
    tree.setRoot( sw::make_unique<sw::uitest::TestPanelWidget>( "root" ) );
    sw::PanelWidget* pRoot = static_cast<sw::PanelWidget*>( tree.getRoot() );

    (void)Util::addText( *pRoot, "Title" );                                      // 0
    sw::ButtonWidget*                pPlay  = Util::addButton( *pRoot, "Play" ); // 1
    sw::unique_ptr<sw::SliderWidget> slider = sw::make_unique<sw::SliderWidget>();
    slider->setRange( 0.0f, 10.0f, 1.0f );
    slider->setValue( 3.0f );
    slider->setAccessibleName( "Volume" );
    (void)pRoot->addChild( std::move( slider ) ); // 2
    sw::unique_ptr<sw::CheckBoxWidget> mute = sw::make_unique<sw::CheckBoxWidget>();
    mute->setChecked( true );
    (void)Util::addText( *mute, "Mute" );
    (void)pRoot->addChild( std::move( mute ) ); // 3

    sw::unique_ptr<sw::uitest::TestPanelWidget> hidden = sw::make_unique<sw::uitest::TestPanelWidget>( "hidden" );
    hidden->setVisibility( sw::WidgetVisibility::Collapsed );
    (void)Util::addText( *hidden, "Hidden" );
    (void)pRoot->addChild( std::move( hidden ) ); // 접은 패널 — 노드 없음

    sw::unique_ptr<sw::uitest::TestPanelWidget> plain = sw::make_unique<sw::uitest::TestPanelWidget>( "plain" );
    (void)Util::addText( *plain, "Plain" );
    (void)pRoot->addChild( std::move( plain ) ); // 4 (패널은 건너뜀)

    sw::unique_ptr<sw::uitest::TestPanelWidget> group = sw::make_unique<sw::uitest::TestPanelWidget>( "group" );
    group->setAccessibleName( "Audio" );
    (void)Util::addText( *group, "InGroup" );
    (void)pRoot->addChild( std::move( group ) ); // 5 묶음, 6 그 안의 글

    sw::ButtonWidget* pQuit = Util::addButton( *pRoot, "Quit" ); // 7
    pQuit->setEnabled( false );

    sw::UIAccessibilitySnapshot snapshot;
    sw::UIAccessibilityTree::appendTree( tree, 7, pPlay->getID(), snapshot );
    SW_ASSERT_EQUAL( size_t{ 8 }, snapshot._listNode.size() );

    SW_EXPECT_TRUE( snapshot._listNode[0]._info._role == sw::AccessibleRole::Text );
    SW_EXPECT_STREQ( "Title", snapshot._listNode[0]._info._name.c_str() );
    SW_EXPECT_TRUE( snapshot._listNode[1]._info._role == sw::AccessibleRole::Button );
    SW_EXPECT_STREQ( "Play", snapshot._listNode[1]._info._name.c_str() );
    SW_EXPECT_TRUE( ( snapshot._listNode[1]._info._states & sw::AccessibleState::kFocused ) != 0 );
    SW_EXPECT_TRUE( ( snapshot._listNode[1]._info._states & sw::AccessibleState::kFocusable ) != 0 );
    SW_EXPECT_EQUAL( 1, snapshot._focusedNode );

    const sw::AccessibleInfo& volume = snapshot._listNode[2]._info;
    SW_EXPECT_TRUE( volume._role == sw::AccessibleRole::Slider );
    SW_EXPECT_STREQ( "Volume", volume._name.c_str() );
    SW_ASSERT_TRUE( volume._bHasRange );
    SW_EXPECT_NEAR_EQUAL( 10.0f, volume._rangeMax, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, volume._rangeValue, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, volume._rangeStep, 1e-6f );

    SW_EXPECT_TRUE( snapshot._listNode[3]._info._role == sw::AccessibleRole::CheckBox );
    SW_EXPECT_STREQ( "Mute", snapshot._listNode[3]._info._name.c_str() );
    SW_EXPECT_TRUE( ( snapshot._listNode[3]._info._states & sw::AccessibleState::kChecked ) != 0 );

    SW_EXPECT_STREQ( "Plain", snapshot._listNode[4]._info._name.c_str() );
    SW_EXPECT_EQUAL( -1, snapshot._listNode[4]._parent ); // 역할 없는 패널은 건너뛰어 루트 아래로 올라온다
    SW_EXPECT_TRUE( snapshot._listNode[5]._info._role == sw::AccessibleRole::Group );
    SW_EXPECT_STREQ( "InGroup", snapshot._listNode[6]._info._name.c_str() );
    SW_EXPECT_EQUAL( 5, snapshot._listNode[6]._parent );

    SW_EXPECT_STREQ( "Quit", snapshot._listNode[7]._info._name.c_str() );
    SW_EXPECT_TRUE( ( snapshot._listNode[7]._info._states & sw::AccessibleState::kDisabled ) != 0 );
    SW_EXPECT_EQUAL( 1, snapshot.findNode( 7, pPlay->getID() ) );
    SW_EXPECT_EQUAL( -1, snapshot.findNode( 8, pPlay->getID() ) );
}

/** @brief [UIScreenReaderTest] 콤보 상자는 고른 항목을 값으로, 입력 칸은 글을 값으로 낸다 — 이름은 `accessibleName` 만 쓴다(안의 글은 값이라 이름이 아니다) */
SW_TEST_CASE( UIScreenReaderTest, ComboAndInputExposeValues )
{
    sw::WidgetTree tree;
    tree.setRoot( sw::make_unique<sw::uitest::TestPanelWidget>( "root" ) );
    sw::PanelWidget* pRoot = static_cast<sw::PanelWidget*>( tree.getRoot() );

    sw::unique_ptr<sw::ComboBoxWidget> combo = sw::make_unique<sw::ComboBoxWidget>();
    combo->setOptions( sw::vector<sw::string>{ "Low", "High" } );
    combo->setSelectedIndex( 1 );
    combo->setAccessibleName( "Quality" );
    (void)pRoot->addChild( std::move( combo ) );
    sw::unique_ptr<sw::TextInputWidget> input = sw::make_unique<sw::TextInputWidget>();
    input->setText( "Alice" );
    input->setAccessibleName( "Player name" );
    (void)pRoot->addChild( std::move( input ) );

    sw::UIAccessibilitySnapshot snapshot;
    sw::UIAccessibilityTree::appendTree( tree, 1, sw::kInvalidWidgetID, snapshot );
    SW_ASSERT_EQUAL( size_t{ 2 }, snapshot._listNode.size() );
    SW_EXPECT_TRUE( snapshot._listNode[0]._info._role == sw::AccessibleRole::ComboBox );
    SW_EXPECT_STREQ( "Quality", snapshot._listNode[0]._info._name.c_str() );
    SW_EXPECT_STREQ( "High", snapshot._listNode[0]._info._value.c_str() );
    SW_EXPECT_TRUE( snapshot._listNode[1]._info._role == sw::AccessibleRole::TextInput );
    SW_EXPECT_STREQ( "Player name", snapshot._listNode[1]._info._name.c_str() );
    SW_EXPECT_STREQ( "Alice", snapshot._listNode[1]._info._value.c_str() );
    SW_EXPECT_EQUAL( -1, snapshot._focusedNode );
}

/** @brief [UIScreenReaderTest] 이름이 없는 슬라이더는 `accessibleLabelledBy` 가 가리키는 글 위젯의 글로 이름을 얻는다 — 옵션 행의 슬라이더가 옆 레이블로 읽히는 길이고, 자기 `accessibleName` 이 있으면 그것이 우선한다 */
SW_TEST_CASE( UIScreenReaderTest, LabelledByNamesAWidgetFromTheLabelText )
{
    using Util = UIScreenReaderTestUtil;
    sw::WidgetTree tree;
    tree.setRoot( sw::make_unique<sw::uitest::TestPanelWidget>( "root" ) );
    sw::PanelWidget* pRoot  = static_cast<sw::PanelWidget*>( tree.getRoot() );
    sw::TextWidget*  pLabel = Util::addText( *pRoot, "Master volume" );
    pLabel->setName( sw::hashed_string( "row.Label" ) );
    sw::unique_ptr<sw::SliderWidget> slider = sw::make_unique<sw::SliderWidget>();
    slider->setAccessibleLabelledBy( sw::hashed_string( "row.Label" ) );
    sw::SliderWidget* pSlider = slider.get();
    (void)pRoot->addChild( std::move( slider ) );

    sw::UIAccessibilitySnapshot snapshot;
    sw::UIAccessibilityTree::appendTree( tree, 1, sw::kInvalidWidgetID, snapshot );
    SW_ASSERT_EQUAL( size_t{ 2 }, snapshot._listNode.size() );
    SW_EXPECT_STREQ( "Master volume", snapshot._listNode[1]._info._name.c_str() );

    pSlider->setAccessibleName( "Volume" );
    snapshot.clear();
    sw::UIAccessibilityTree::appendTree( tree, 1, sw::kInvalidWidgetID, snapshot );
    SW_EXPECT_STREQ( "Volume", snapshot._listNode[1]._info._name.c_str() );
}

/**
 * @brief [UIScreenReaderTest] UI 시스템은 스크린 리더가 붙어 있을 때만 포커스 변화와 알림을 브리지에 알린다 — 붙지 않은 동안은 아무것도 부르지 않고, 붙은 뒤 첫 프레임에 지금 포커스를 알린다
 * @details 입력을 막는 화면(모달) 아래의 화면은 접근성 트리에서 뺀다. 변이: `announce` 가 `isClientActive` 를 보지 않으면 붙지 않은 동안의 알림이 브리지에 닿아 진다.
 */
SW_TEST_CASE( UIScreenReaderTest, SystemNotifiesBridgeOnlyWhileClientIsAttached )
{
    using Util = UIScreenReaderTestUtil;
    sw::UISystem   ui;
    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ 640.0f, 360.0f };
    viewport._physicalSize = viewport._size;

    sw::unique_ptr<Util::FakeBridge> bridge  = sw::make_unique<Util::FakeBridge>();
    Util::FakeBridge*                pBridge = bridge.get();
    ui.setAccessibilityBridge( std::move( bridge ) );

    sw::unique_ptr<sw::uitest::TestPanelWidget> lowerRoot = sw::make_unique<sw::uitest::TestPanelWidget>( "lower" );
    (void)Util::addText( *lowerRoot, "Below" );
    const sw::UIScreenDesc lowerDesc{};
    (void)ui.pushScreen( sw::make_unique<sw::UIScreen>( lowerDesc, std::move( lowerRoot ) ) );

    sw::unique_ptr<sw::uitest::TestPanelWidget> modalRoot = sw::make_unique<sw::uitest::TestPanelWidget>( "modal" );
    sw::ButtonWidget*                           pOk       = Util::addButton( *modalRoot, "OK" );
    sw::UIScreenDesc                            modalDesc{};
    modalDesc._bModal                    = true;
    const sw::UIScreenHandle modalHandle = ui.pushScreen( sw::make_unique<sw::UIScreen>( modalDesc, std::move( modalRoot ) ) );
    sw::UIScreen*            pModal      = ui.findScreen( modalHandle );
    SW_ASSERT_NOT_NULL( pModal );
    SW_ASSERT_TRUE( ui.getInputRouter().getFocusManager().setFocus( pModal->getTree(), pOk->getID() ) );

    // 붙지 않은 동안: 알림도 포커스 알림도 닿지 않는다.
    ui.announce( "Saved", sw::UIAnnouncePriority::Polite );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( 0u, pBridge->_announceCount );
    SW_EXPECT_EQUAL( 0u, pBridge->_focusCount );

    // 붙은 뒤: 첫 갱신이 지금 포커스를 알리고, 같은 포커스는 다시 알리지 않는다.
    pBridge->_bActive = true;
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( 1u, pBridge->_focusCount );
    SW_EXPECT_EQUAL( modalHandle, pBridge->_lastFocusScreen );
    SW_EXPECT_EQUAL( pOk->getID(), pBridge->_lastFocusWidget );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( 1u, pBridge->_focusCount );
    ui.announce( "Saved", sw::UIAnnouncePriority::Assertive );
    SW_EXPECT_EQUAL( 1u, pBridge->_announceCount );
    SW_EXPECT_STREQ( "Saved", pBridge->_lastText.c_str() );
    SW_EXPECT_TRUE( pBridge->_lastPriority == sw::UIAnnouncePriority::Assertive );

    // 모달 아래 화면은 읽지 않는다.
    sw::UIAccessibilitySnapshot snapshot;
    ui.populateAccessibilitySnapshot( snapshot );
    SW_ASSERT_EQUAL( size_t{ 1 }, snapshot._listNode.size() );
    SW_EXPECT_STREQ( "OK", snapshot._listNode[0]._info._name.c_str() );
    SW_EXPECT_EQUAL( 0, snapshot._focusedNode );
}

/**
 * @brief [UIScreenReaderTest] 스크린 리더가 시키는 일은 사람의 입력과 같은 길을 탄다 — 누르기는 단추 클릭이 되고, 포커스는 포커스 관리자가 정하고, 슬라이더 값은 범위로 제한된다. 꺼진 위젯 · 없는 위젯은 거절한다
 * @details 변이: `invoke` 가 꺼진 위젯을 거르지 않으면 꺼진 단추도 눌려 `DisabledAndMissingWidgetsAreRefused` 부분이 진다.
 */
SW_TEST_CASE( UIScreenReaderTest, InvokeFocusAndRangeValueGoThroughTheInputPath )
{
    using Util = UIScreenReaderTestUtil;
    sw::UISystem                                ui;
    sw::unique_ptr<sw::uitest::TestPanelWidget> root  = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
    sw::ButtonWidget*                           pPlay = Util::addButton( *root, "Play" );
    sw::ButtonWidget*                           pQuit = Util::addButton( *root, "Quit" );
    pQuit->setEnabled( false );
    sw::unique_ptr<sw::SliderWidget> slider = sw::make_unique<sw::SliderWidget>();
    slider->setRange( 0.0f, 10.0f, 1.0f );
    sw::SliderWidget* pSlider = slider.get();
    (void)root->addChild( std::move( slider ) );
    const sw::UIScreenDesc   desc{};
    const sw::UIScreenHandle handle = ui.pushScreen( sw::make_unique<sw::UIScreen>( desc, std::move( root ) ) );

    SW_EXPECT_TRUE( sw::UIAccessibilityActions::invoke( ui, handle, pPlay->getID() ) );
    SW_EXPECT_EQUAL( 1u, pPlay->getClickCount() );
    SW_EXPECT_FALSE( sw::UIAccessibilityActions::invoke( ui, handle, pQuit->getID() ) ); // DisabledAndMissingWidgetsAreRefused
    SW_EXPECT_EQUAL( 0u, pQuit->getClickCount() );
    SW_EXPECT_FALSE( sw::UIAccessibilityActions::invoke( ui, handle, 999999u ) );
    SW_EXPECT_FALSE( sw::UIAccessibilityActions::invoke( ui, handle + 1, pPlay->getID() ) );

    SW_EXPECT_TRUE( sw::UIAccessibilityActions::focus( ui, handle, pPlay->getID() ) );
    SW_EXPECT_EQUAL( pPlay->getID(), ui.getInputRouter().getFocusManager().getFocusedWidget() );
    SW_EXPECT_FALSE( sw::UIAccessibilityActions::focus( ui, handle, pQuit->getID() ) ); // 꺼진 위젯은 포커스를 받지 않는다

    SW_EXPECT_TRUE( sw::UIAccessibilityActions::setRangeValue( ui, handle, pSlider->getID(), 7.0f ) );
    SW_EXPECT_NEAR_EQUAL( 7.0f, pSlider->getValue(), 1e-6f );
    SW_EXPECT_TRUE( sw::UIAccessibilityActions::setRangeValue( ui, handle, pSlider->getID(), 99.0f ) );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pSlider->getValue(), 1e-6f );                                        // 범위로 제한된다
    SW_EXPECT_FALSE( sw::UIAccessibilityActions::setRangeValue( ui, handle, pPlay->getID(), 1.0f ) ); // 슬라이더가 아니다
}

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
/** @brief [UIScreenReaderTest] 역할마다 UIA 컨트롤 타입 번호가 정해져 있다(`UIA_ButtonControlTypeId` = 50000 …) — 번호가 어긋나면 내레이터가 "단추" 를 "편집 상자" 로 읽는다 */
SW_TEST_CASE( UIScreenReaderTest, RolesMapToUiaControlTypes )
{
    SW_EXPECT_EQUAL( 50000, sw::UiaAccessibility::getControlType( sw::AccessibleRole::Button ) );
    SW_EXPECT_EQUAL( 50002, sw::UiaAccessibility::getControlType( sw::AccessibleRole::CheckBox ) );
    SW_EXPECT_EQUAL( 50003, sw::UiaAccessibility::getControlType( sw::AccessibleRole::ComboBox ) );
    SW_EXPECT_EQUAL( 50004, sw::UiaAccessibility::getControlType( sw::AccessibleRole::TextInput ) );
    SW_EXPECT_EQUAL( 50006, sw::UiaAccessibility::getControlType( sw::AccessibleRole::Image ) );
    SW_EXPECT_EQUAL( 50008, sw::UiaAccessibility::getControlType( sw::AccessibleRole::List ) );
    SW_EXPECT_EQUAL( 50012, sw::UiaAccessibility::getControlType( sw::AccessibleRole::ProgressBar ) );
    SW_EXPECT_EQUAL( 50015, sw::UiaAccessibility::getControlType( sw::AccessibleRole::Slider ) );
    SW_EXPECT_EQUAL( 50016, sw::UiaAccessibility::getControlType( sw::AccessibleRole::SpinButton ) );
    SW_EXPECT_EQUAL( 50020, sw::UiaAccessibility::getControlType( sw::AccessibleRole::Text ) );
    SW_EXPECT_EQUAL( 50026, sw::UiaAccessibility::getControlType( sw::AccessibleRole::Group ) );
    SW_EXPECT_EQUAL( 0, sw::UiaAccessibility::getControlType( sw::AccessibleRole::None ) );
}
#endif
