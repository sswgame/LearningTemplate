#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Input/InputManager.h"
#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/UiFocusManager.h"
#include "Engine/UI/Core/UiNavigationSolver.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"

#include "EngineTest/UI/UiTestWidgets.h"

#include "TestFramework/TestFramework.h"

// UiNavigationTest — 포커스와 게임패드 공간 탐색: 이웃 점수 · 규칙(Stop · Wrap · Explicit) · 꺼짐 · 탭 순서 · 떨어진 포커스. 디바이스 없음(nogpu).
// 배치는 레이아웃 없이 `UiTestUtil::placeWidget` 으로 고정한다.

namespace
{
    /** @brief 열 × 행 버튼 격자입니다. 버튼 이름은 "b<행><열>", 격자 패널 이름은 "grid"(루트). */
    struct UiNavigationFixture
    {
        sw::WidgetTree             _tree;
        sw::UiFocusManager         _focus;
        sw::test::TestPanelWidget* _pGrid;

        UiNavigationFixture( uint32 columnCount, uint32 rowCount, float32 width, float32 height, float32 gap )
            : _tree{}
            , _focus{}
            , _pGrid{ nullptr }
        {
            auto grid = sw::make_unique<sw::test::TestPanelWidget>( "grid" );
            _pGrid    = grid.get();
            _tree.setRoot( std::move( grid ) );
            sw::test::UiTestUtil::placeWidget( *_pGrid, 0.0f, 0.0f, 2000.0f, 2000.0f );
            for ( uint32 row = 0; row < rowCount; ++row )
            {
                for ( uint32 column = 0; column < columnCount; ++column )
                {
                    const sw::string name    = "b" + sw::to_string( row ) + sw::to_string( column );
                    sw::Widget*      pButton = _pGrid->addChild( sw::make_unique<sw::test::TestBoxWidget>( sw::hashed_string( name ), true ) );
                    sw::test::UiTestUtil::placeWidget( *pButton, static_cast<float32>( column ) * ( width + gap ), static_cast<float32>( row ) * ( height + gap ), width, height );
                }
            }
        }

        sw::Widget* find( const sw::hashed_string& name ) const { return _tree.findWidgetByName( name ); }

        bool focus( const sw::hashed_string& name ) { return find( name ) != nullptr && _focus.setFocus( _tree, find( name )->getId() ); }

        sw::string getFocusedName() const
        {
            const sw::Widget* pWidget = _tree.findWidgetById( _focus.getFocusedWidget() );
            return pWidget != nullptr ? sw::string{ pWidget->getName().c_str() } : sw::string{};
        }

        /** @brief 방향으로 옮긴 뒤 포커스가 @p expected 이면 true 입니다(틀리면 실제 이름을 남긴다). */
        bool navigateAndExpect( sw::UiNavigationDirection direction, const utf8* pExpected )
        {
            (void)_focus.navigate( _tree, direction );
            const sw::string actual = getFocusedName();
            if ( actual == pExpected )
                return true;
            SW_LOG_WARNING( "[UiNavigationTest] expected focus '%#' but it is '%#'", pExpected, actual.c_str() );
            return false;
        }
    };
} // namespace

/** @brief [UiNavigationTest] 3×3 격자의 가운데에서 네 방향이 바로 이웃이다 — 수직 틈이 0 인 같은 줄 · 같은 열이 먼저 */
SW_TEST_CASE( UiNavigationTest, GridMovesToDirectNeighbors )
{
    UiNavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    SW_ASSERT_TRUE( fixture.focus( "b11" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Right, "b12" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Down, "b22" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Left, "b21" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Up, "b11" ) );
}

/** @brief [UiNavigationTest] 오른쪽 끝에서 Right 는 그대로 선다(기본 규칙 Escape → 화면 뿌리에서 후보 없음) */
SW_TEST_CASE( UiNavigationTest, EdgeStopsByDefault )
{
    UiNavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    SW_ASSERT_TRUE( fixture.focus( "b12" ) );
    SW_EXPECT_FALSE( fixture._focus.navigate( fixture._tree, sw::UiNavigationDirection::Right ) );
    SW_EXPECT_STREQ( "b12", fixture.getFocusedName().c_str() );
}

/** @brief [UiNavigationTest] 격자 패널의 Right 가 Wrap 이면 끝에서 같은 줄 반대쪽 끝으로 돈다 — b11 → b12 → b10 */
SW_TEST_CASE( UiNavigationTest, WrapRuleWrapsWithinPanel )
{
    UiNavigationFixture  fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    sw::WidgetNavigation navigation = fixture._pGrid->getNavigation();
    navigation._right._rule         = sw::UiNavigationRule::Wrap;
    fixture._pGrid->setNavigation( navigation );

    SW_ASSERT_TRUE( fixture.focus( "b11" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Right, "b12" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Right, "b10" ) );
}

/** @brief [UiNavigationTest] 명시 이웃이 공간 탐색보다 먼저다 — b00 의 Down = Explicit "b22" */
SW_TEST_CASE( UiNavigationTest, ExplicitTargetWins )
{
    UiNavigationFixture  fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    sw::Widget*          pB00       = fixture.find( "b00" );
    sw::WidgetNavigation navigation = pB00->getNavigation();
    navigation._down._rule          = sw::UiNavigationRule::Explicit;
    navigation._down._target        = "b22";
    pB00->setNavigation( navigation );

    SW_ASSERT_TRUE( fixture.focus( "b00" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Down, "b22" ) );
}

/** @brief [UiNavigationTest] 명시 이웃이 받을 수 없으면(꺼짐) 다음 규칙 — 공간 탐색 결과로 간다 */
SW_TEST_CASE( UiNavigationTest, ExplicitTargetUnfocusableFallsBack )
{
    UiNavigationFixture  fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    sw::Widget*          pB00       = fixture.find( "b00" );
    sw::WidgetNavigation navigation = pB00->getNavigation();
    navigation._down._rule          = sw::UiNavigationRule::Explicit;
    navigation._down._target        = "b22";
    pB00->setNavigation( navigation );
    fixture.find( "b22" )->setEnabled( false );

    SW_ASSERT_TRUE( fixture.focus( "b00" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Down, "b10" ) );
}

/**
 * @brief [UiNavigationTest] 꺼진 · 접힌 위젯은 건너뛴다 — b12 를 끄면 b11 에서 Right 는 같은 줄에 후보가 없어 다른 줄로 간다
 * @details b02 · b22 의 점수가 같고(주축 10 + 2 × 수직 틈 10 = 30) 중심 거리도 같아 문서 순서가 앞인 b02. Collapsed 로 해도 같다.
 */
SW_TEST_CASE( UiNavigationTest, DisabledAndCollapsedAreSkipped )
{
    {
        UiNavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
        fixture.find( "b12" )->setEnabled( false );
        SW_ASSERT_TRUE( fixture.focus( "b11" ) );
        SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Right, "b02" ) );
    }
    {
        UiNavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
        fixture.find( "b12" )->setVisibility( sw::WidgetVisibility::Collapsed );
        SW_ASSERT_TRUE( fixture.focus( "b11" ) );
        SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Right, "b02" ) );
        SW_EXPECT_FALSE( fixture.focus( "b12" ) ); // 접힌 위젯은 포커스를 받지 않는다
    }
}

/** @brief [UiNavigationTest] 탭 순서는 문서 순서이고 끝에서 처음으로 돈다 — Next 아홉 번이면 제자리, Previous 는 거꾸로 */
SW_TEST_CASE( UiNavigationTest, TabOrderIsDocumentOrderAndWraps )
{
    UiNavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    SW_ASSERT_TRUE( fixture.focus( "b11" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Next, "b12" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Next, "b20" ) );
    for ( uint32 step = 0; step < 7; ++step )
        (void)fixture._focus.navigate( fixture._tree, sw::UiNavigationDirection::Next );
    SW_EXPECT_STREQ( "b11", fixture.getFocusedName().c_str() );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UiNavigationDirection::Previous, "b10" ) );
}

/** @brief [UiNavigationTest] 포커스 위젯을 떼면 포커스가 풀린다 — 옛 번호가 남지 않고, 그 뒤 탐색은 아무 일도 하지 않는다 */
SW_TEST_CASE( UiNavigationTest, RemovingFocusedWidgetClearsFocus )
{
    UiNavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    SW_ASSERT_TRUE( fixture.focus( "b11" ) );
    sw::Widget* pFocused = fixture.find( "b11" );
    SW_EXPECT_TRUE( pFocused->hasFocus() );

    sw::unique_ptr<sw::Widget> removed = fixture._pGrid->removeChild( pFocused );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetId, fixture._focus.getFocusedWidget() );
    SW_EXPECT_FALSE( removed->hasFocus() );
    SW_EXPECT_FALSE( fixture._focus.navigate( fixture._tree, sw::UiNavigationDirection::Right ) );
}

/** @brief [UiNavigationTest] 트리가 먼저 지워져도 포커스 관리자는 떨어진 트리를 들고 있지 않는다 */
SW_TEST_CASE( UiNavigationTest, DestroyedTreeReleasesFocus )
{
    sw::UiFocusManager focus;
    {
        sw::WidgetTree   tree;
        auto             root  = sw::make_unique<sw::test::TestPanelWidget>( "root" );
        sw::PanelWidget* pRoot = root.get();
        tree.setRoot( std::move( root ) );
        sw::Widget* pButton = pRoot->addChild( sw::make_unique<sw::test::TestBoxWidget>( "button", true ) );
        SW_ASSERT_TRUE( focus.setFocus( tree, pButton->getId() ) );
        SW_EXPECT_TRUE( focus.getFocusedTree() == &tree );
    }
    SW_EXPECT_TRUE( focus.getFocusedTree() == nullptr );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetId, focus.getFocusedWidget() );
}

/**
 * @brief [UiNavigationTest] 수직 틈에 벌을 준다 — 같은 줄 A(주축 50 · 틈 0)가 옆 줄 B(주축 10 · 틈 30)보다 가깝다(50 < 10 + 2 × 30)
 * @details 변이: `computeSpatialScore` 의 `2.0f *` 를 `0.0f *` 로 바꾸면 B 를 골라 진다.
 */
SW_TEST_CASE( UiNavigationTest, PerpendicularGapIsPenalized )
{
    const sw::UiRect from      = sw::UiRect::makeFromPositionSize( 0.0f, 0.0f, 100.0f, 50.0f );
    const sw::UiRect sameRow   = sw::UiRect::makeFromPositionSize( 150.0f, 0.0f, 100.0f, 50.0f );
    const sw::UiRect nearbyRow = sw::UiRect::makeFromPositionSize( 110.0f, 80.0f, 100.0f, 50.0f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, sw::UiNavigationSolver::computeSpatialScore( from, sameRow, sw::UiNavigationDirection::Right ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 70.0f, sw::UiNavigationSolver::computeSpatialScore( from, nearbyRow, sw::UiNavigationDirection::Right ), 0.001f );
    SW_EXPECT_TRUE( sw::UiNavigationSolver::computeSpatialScore( from, sameRow, sw::UiNavigationDirection::Left ) < 0.0f );

    sw::WidgetTree   tree;
    auto             root  = sw::make_unique<sw::test::TestPanelWidget>( "root" );
    sw::PanelWidget* pRoot = root.get();
    tree.setRoot( std::move( root ) );
    sw::test::UiTestUtil::placeWidget( *pRoot, 0.0f, 0.0f, 500.0f, 500.0f );
    sw::Widget* pFrom = pRoot->addChild( sw::make_unique<sw::test::TestBoxWidget>( "from", true ) );
    sw::Widget* pB    = pRoot->addChild( sw::make_unique<sw::test::TestBoxWidget>( "B", true ) ); // 문서 순서가 앞이라도 점수로 진다
    sw::Widget* pA    = pRoot->addChild( sw::make_unique<sw::test::TestBoxWidget>( "A", true ) );
    sw::test::UiTestUtil::placeWidget( *pFrom, 0.0f, 0.0f, 100.0f, 50.0f );
    sw::test::UiTestUtil::placeWidget( *pB, 110.0f, 80.0f, 100.0f, 50.0f );
    sw::test::UiTestUtil::placeWidget( *pA, 150.0f, 0.0f, 100.0f, 50.0f );
    SW_EXPECT_EQUAL( pA->getId(), sw::UiNavigationSolver::findNextWidget( tree, pFrom->getId(), sw::UiNavigationDirection::Right ) );
}

/** @brief [UiNavigationTest] 탐색은 활성(모달) 화면의 트리 안에서만 — 오른쪽에 아래 메뉴의 버튼이 있어도 모달 밖으로 나가지 않는다 */
SW_TEST_CASE( UiNavigationTest, NavigationStaysInsideModalScreen )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    {
        sw::UiSystem ui;
        SW_ASSERT_TRUE( ui.initialize( input, nullptr ) );
        ui.setInputMode( sw::UiInputMode::Navigation );

        auto menuRoot = sw::make_unique<sw::test::TestPanelWidget>( "menuRoot" );
        sw::test::UiTestUtil::placeWidget( *menuRoot, 0.0f, 0.0f, 800.0f, 600.0f );
        sw::Widget* pMenuButton = menuRoot->addChild( sw::make_unique<sw::test::TestBoxWidget>( "menuButton", true ) );
        sw::test::UiTestUtil::placeWidget( *pMenuButton, 500.0f, 100.0f, 100.0f, 40.0f );
        (void)ui.pushScreen( sw::make_unique<sw::UiScreen>( sw::UiScreenDesc{}, std::move( menuRoot ) ) );

        auto modalRoot = sw::make_unique<sw::test::TestPanelWidget>( "modalRoot" );
        sw::test::UiTestUtil::placeWidget( *modalRoot, 0.0f, 80.0f, 300.0f, 100.0f );
        sw::Widget* pModalButton = modalRoot->addChild( sw::make_unique<sw::test::TestBoxWidget>( "modalButton", true ) );
        sw::test::UiTestUtil::placeWidget( *pModalButton, 10.0f, 100.0f, 100.0f, 40.0f );
        sw::UiScreenDesc modalDesc{};
        modalDesc._layer  = sw::UiLayer::Modal;
        modalDesc._bModal = true;
        (void)ui.pushScreen( sw::make_unique<sw::UiScreen>( modalDesc, std::move( modalRoot ) ) );

        SW_ASSERT_NOT_NULL( ui.getActiveScreen() );
        SW_EXPECT_TRUE( pModalButton->hasFocus() );
        SW_EXPECT_FALSE( ui.getFocusManager().navigate( ui.getActiveScreen()->getTree(), sw::UiNavigationDirection::Right ) );
        SW_EXPECT_TRUE( pModalButton->hasFocus() );
        SW_EXPECT_FALSE( pMenuButton->hasFocus() );
        ui.shutdown();
    }
    input.shutdown();
}
