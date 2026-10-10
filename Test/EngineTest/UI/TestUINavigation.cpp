#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Input/InputManager.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/UIFocusManager.h"
#include "Engine/UI/Base/UINavigationSolver.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"

#include "EngineTest/UI/UILayoutTestUtil.h"
#include "EngineTest/UI/UITestWidgets.h"

#include "TestFramework/TestFramework.h"

// UINavigationTest — 포커스와 게임패드 공간 탐색: 이웃 점수 · 규칙(Stop · Wrap · Explicit) · 꺼짐 · 탭 순서 · 떨어진 포커스. 디바이스 없음(nogpu).
// 배치는 레이아웃 없이 `UITestUtil::placeWidget` 으로 고정한다.

namespace
{
    /** @brief 열 × 행 버튼 격자입니다. 버튼 이름은 "b<행><열>", 격자 패널 이름은 "grid"(루트). */
    struct UINavigationFixture
    {
        sw::WidgetTree               _tree;
        sw::UIFocusManager           _focus;
        sw::uitest::TestPanelWidget* _pGrid;

        UINavigationFixture( uint32 columnCount, uint32 rowCount, float32 width, float32 height, float32 gap )
            : _tree{}
            , _focus{}
            , _pGrid{ nullptr }
        {
            auto grid = sw::make_unique<sw::uitest::TestPanelWidget>( "grid" );
            _pGrid    = grid.get();
            _tree.setRoot( std::move( grid ) );
            sw::uitest::UITestUtil::placeWidget( *_pGrid, 0.0f, 0.0f, 2000.0f, 2000.0f );
            for ( uint32 row = 0; row < rowCount; ++row )
            {
                for ( uint32 column = 0; column < columnCount; ++column )
                {
                    const sw::string name    = "b" + sw::to_string( row ) + sw::to_string( column );
                    sw::Widget*      pButton = _pGrid->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( sw::hashed_string( name ), true ) );
                    sw::uitest::UITestUtil::placeWidget( *pButton, static_cast<float32>( column ) * ( width + gap ), static_cast<float32>( row ) * ( height + gap ), width, height );
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
        bool navigateAndExpect( sw::UINavigationDirection direction, const utf8* pExpected )
        {
            (void)_focus.navigate( _tree, direction );
            const sw::string actual = getFocusedName();
            if ( actual == pExpected )
                return true;
            SW_LOG_WARNING( "[UINavigationTest] expected focus '%#' but it is '%#'", pExpected, actual.c_str() );
            return false;
        }
    };
} // namespace

/** @brief [UINavigationTest] 3×3 격자의 가운데에서 네 방향이 바로 이웃이다 — 수직 틈이 0 인 같은 줄 · 같은 열이 먼저 */
SW_TEST_CASE( UINavigationTest, GridMovesToDirectNeighbors )
{
    UINavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    SW_ASSERT_TRUE( fixture.focus( "b11" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Right, "b12" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Down, "b22" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Left, "b21" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Up, "b11" ) );
}

/** @brief [UINavigationTest] 오른쪽 끝에서 Right 는 그대로 선다(기본 규칙 Escape → 화면 뿌리에서 후보 없음) */
SW_TEST_CASE( UINavigationTest, EdgeStopsByDefault )
{
    UINavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    SW_ASSERT_TRUE( fixture.focus( "b12" ) );
    SW_EXPECT_FALSE( fixture._focus.navigate( fixture._tree, sw::UINavigationDirection::Right ) );
    SW_EXPECT_STREQ( "b12", fixture.getFocusedName().c_str() );
}

/** @brief [UINavigationTest] 격자 패널의 Right 가 Wrap 이면 끝에서 같은 줄 반대쪽 끝으로 돈다 — b11 → b12 → b10 */
SW_TEST_CASE( UINavigationTest, WrapRuleWrapsWithinPanel )
{
    UINavigationFixture  fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    sw::WidgetNavigation navigation = fixture._pGrid->getNavigation();
    navigation._right._rule         = sw::UINavigationRule::Wrap;
    fixture._pGrid->setNavigation( navigation );

    SW_ASSERT_TRUE( fixture.focus( "b11" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Right, "b12" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Right, "b10" ) );
}

/** @brief [UINavigationTest] 명시 이웃이 공간 탐색보다 먼저다 — b00 의 Down = Explicit "b22" */
SW_TEST_CASE( UINavigationTest, ExplicitTargetWins )
{
    UINavigationFixture  fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    sw::Widget*          pB00       = fixture.find( "b00" );
    sw::WidgetNavigation navigation = pB00->getNavigation();
    navigation._down._rule          = sw::UINavigationRule::Explicit;
    navigation._down._target        = "b22";
    pB00->setNavigation( navigation );

    SW_ASSERT_TRUE( fixture.focus( "b00" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Down, "b22" ) );
}

/** @brief [UINavigationTest] 명시 이웃이 받을 수 없으면(꺼짐) 다음 규칙 — 공간 탐색 결과로 간다 */
SW_TEST_CASE( UINavigationTest, ExplicitTargetUnfocusableFallsBack )
{
    UINavigationFixture  fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    sw::Widget*          pB00       = fixture.find( "b00" );
    sw::WidgetNavigation navigation = pB00->getNavigation();
    navigation._down._rule          = sw::UINavigationRule::Explicit;
    navigation._down._target        = "b22";
    pB00->setNavigation( navigation );
    fixture.find( "b22" )->setEnabled( false );

    SW_ASSERT_TRUE( fixture.focus( "b00" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Down, "b10" ) );
}

/**
 * @brief [UINavigationTest] 꺼진 · 접힌 위젯은 건너뛴다 — b12 를 끄면 b11 에서 Right 는 같은 줄에 후보가 없어 다른 줄로 간다
 * @details b02 · b22 의 점수가 같고(주축 10 + 2 × 수직 틈 10 = 30) 중심 거리도 같아 문서 순서가 앞인 b02. Collapsed 로 해도 같다.
 */
SW_TEST_CASE( UINavigationTest, DisabledAndCollapsedAreSkipped )
{
    {
        UINavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
        fixture.find( "b12" )->setEnabled( false );
        SW_ASSERT_TRUE( fixture.focus( "b11" ) );
        SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Right, "b02" ) );
    }
    {
        UINavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
        fixture.find( "b12" )->setVisibility( sw::WidgetVisibility::Collapsed );
        SW_ASSERT_TRUE( fixture.focus( "b11" ) );
        SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Right, "b02" ) );
        SW_EXPECT_FALSE( fixture.focus( "b12" ) ); // 접힌 위젯은 포커스를 받지 않는다
    }
}

/** @brief [UINavigationTest] 탭 순서는 문서 순서이고 끝에서 처음으로 돈다 — Next 아홉 번이면 제자리, Previous 는 거꾸로 */
SW_TEST_CASE( UINavigationTest, TabOrderIsDocumentOrderAndWraps )
{
    UINavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    SW_ASSERT_TRUE( fixture.focus( "b11" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Next, "b12" ) );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Next, "b20" ) );
    for ( uint32 step = 0; step < 7; ++step )
    {
        (void)fixture._focus.navigate( fixture._tree, sw::UINavigationDirection::Next );
    }
    SW_EXPECT_STREQ( "b11", fixture.getFocusedName().c_str() );
    SW_EXPECT_TRUE( fixture.navigateAndExpect( sw::UINavigationDirection::Previous, "b10" ) );
}

/** @brief [UINavigationTest] 포커스 위젯을 떼면 포커스가 풀린다 — 옛 번호가 남지 않고, 그 뒤 탐색은 아무 일도 하지 않는다 */
SW_TEST_CASE( UINavigationTest, RemovingFocusedWidgetClearsFocus )
{
    UINavigationFixture fixture( 3, 3, 100.0f, 50.0f, 10.0f );
    SW_ASSERT_TRUE( fixture.focus( "b11" ) );
    sw::Widget* pFocused = fixture.find( "b11" );
    SW_EXPECT_TRUE( pFocused->hasFocus() );

    sw::unique_ptr<sw::Widget> removed = fixture._pGrid->removeChild( pFocused );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetId, fixture._focus.getFocusedWidget() );
    SW_EXPECT_FALSE( removed->hasFocus() );
    SW_EXPECT_FALSE( fixture._focus.navigate( fixture._tree, sw::UINavigationDirection::Right ) );
}

/** @brief [UINavigationTest] 트리가 먼저 지워져도 포커스 관리자는 떨어진 트리를 들고 있지 않는다 */
SW_TEST_CASE( UINavigationTest, DestroyedTreeReleasesFocus )
{
    sw::UIFocusManager focus;
    {
        sw::WidgetTree   tree;
        auto             root  = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
        sw::PanelWidget* pRoot = root.get();
        tree.setRoot( std::move( root ) );
        sw::Widget* pButton = pRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "button", true ) );
        SW_ASSERT_TRUE( focus.setFocus( tree, pButton->getId() ) );
        SW_EXPECT_TRUE( focus.getFocusedTree() == &tree );
    }
    SW_EXPECT_TRUE( focus.getFocusedTree() == nullptr );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetId, focus.getFocusedWidget() );
}

/**
 * @brief [UINavigationTest] 수직 틈에 벌을 준다 — 같은 줄 A(주축 50 · 틈 0)가 옆 줄 B(주축 10 · 틈 30)보다 가깝다(50 < 10 + 2 × 30)
 * @details 변이: `computeSpatialScore` 의 `2.0f *` 를 `0.0f *` 로 바꾸면 B 를 골라 진다.
 */
SW_TEST_CASE( UINavigationTest, PerpendicularGapIsPenalized )
{
    const sw::UIRect from      = sw::UIRect::makeFromPositionSize( 0.0f, 0.0f, 100.0f, 50.0f );
    const sw::UIRect sameRow   = sw::UIRect::makeFromPositionSize( 150.0f, 0.0f, 100.0f, 50.0f );
    const sw::UIRect nearbyRow = sw::UIRect::makeFromPositionSize( 110.0f, 80.0f, 100.0f, 50.0f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, sw::UINavigationSolver::computeSpatialScore( from, sameRow, sw::UINavigationDirection::Right ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 70.0f, sw::UINavigationSolver::computeSpatialScore( from, nearbyRow, sw::UINavigationDirection::Right ), 0.001f );
    SW_EXPECT_TRUE( sw::UINavigationSolver::computeSpatialScore( from, sameRow, sw::UINavigationDirection::Left ) < 0.0f );

    sw::WidgetTree   tree;
    auto             root  = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
    sw::PanelWidget* pRoot = root.get();
    tree.setRoot( std::move( root ) );
    sw::uitest::UITestUtil::placeWidget( *pRoot, 0.0f, 0.0f, 500.0f, 500.0f );
    sw::Widget* pFrom = pRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "from", true ) );
    sw::Widget* pB    = pRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "B", true ) ); // 문서 순서가 앞이라도 점수로 진다
    sw::Widget* pA    = pRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "A", true ) );
    sw::uitest::UITestUtil::placeWidget( *pFrom, 0.0f, 0.0f, 100.0f, 50.0f );
    sw::uitest::UITestUtil::placeWidget( *pB, 110.0f, 80.0f, 100.0f, 50.0f );
    sw::uitest::UITestUtil::placeWidget( *pA, 150.0f, 0.0f, 100.0f, 50.0f );
    SW_EXPECT_EQUAL( pA->getId(), sw::UINavigationSolver::findNextWidget( tree, pFrom->getId(), sw::UINavigationDirection::Right ) );
}

/** @brief [UINavigationTest] 탐색은 활성(모달) 화면의 트리 안에서만 — 오른쪽에 아래 메뉴의 버튼이 있어도 모달 밖으로 나가지 않는다 */
SW_TEST_CASE( UINavigationTest, NavigationStaysInsideModalScreen )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    {
        sw::UISystem ui;
        SW_ASSERT_TRUE( ui.initialize( input, nullptr ) );
        ui.setInputMode( sw::UIInputMode::Navigation );

        auto menuRoot = sw::make_unique<sw::uitest::TestPanelWidget>( "menuRoot" );
        sw::uitest::UITestUtil::placeWidget( *menuRoot, 0.0f, 0.0f, 800.0f, 600.0f );
        sw::Widget* pMenuButton = menuRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "menuButton", true ) );
        sw::uitest::UITestUtil::placeWidget( *pMenuButton, 500.0f, 100.0f, 100.0f, 40.0f );
        (void)ui.pushScreen( sw::make_unique<sw::UIScreen>( sw::UIScreenDesc{}, std::move( menuRoot ) ) );

        auto modalRoot = sw::make_unique<sw::uitest::TestPanelWidget>( "modalRoot" );
        sw::uitest::UITestUtil::placeWidget( *modalRoot, 0.0f, 80.0f, 300.0f, 100.0f );
        sw::Widget* pModalButton = modalRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "modalButton", true ) );
        sw::uitest::UITestUtil::placeWidget( *pModalButton, 10.0f, 100.0f, 100.0f, 40.0f );
        sw::UIScreenDesc modalDesc{};
        modalDesc._layer  = sw::UILayer::Modal;
        modalDesc._bModal = true;
        (void)ui.pushScreen( sw::make_unique<sw::UIScreen>( modalDesc, std::move( modalRoot ) ) );

        SW_ASSERT_NOT_NULL( ui.getActiveScreen() );
        SW_EXPECT_TRUE( pModalButton->hasFocus() );
        SW_EXPECT_FALSE( ui.getFocusManager().navigate( ui.getActiveScreen()->getTree(), sw::UINavigationDirection::Right ) );
        SW_EXPECT_TRUE( pModalButton->hasFocus() );
        SW_EXPECT_FALSE( pMenuButton->hasFocus() );
        ui.shutdown();
    }
    input.shutdown();
}

/**
 * @brief [UINavigationTest] 스크롤 패널이 자른(완전히 밖인) 항목도 아래 탐색의 후보이고, 포커스가 가면 패널이 그 항목을 보이게 옮긴다
 * @details 보이는 높이 100 · 항목 0..40 · 빈칸 110 · 항목 150..190. 변이: 탐색 후보 거르기에서 스크롤 패널 예외(`canScrollIntoView`)를 빼면 포커스가
 *          옮겨지지 않고, `UIFocusManager::navigate` 의 `scrollIntoView` 를 빼면 오프셋이 0 으로 남는다.
 */
SW_TEST_CASE( UINavigationTest, NavigateRevealsClippedItemInScrollPanel )
{
    sw::test::UILayoutFixture fixture( 200.0f, 100.0f );
    sw::ScrollPanel*          pScroll  = fixture.setRoot<sw::ScrollPanel>( "scroll" );
    sw::BoxPanel*             pContent = fixture.addPanel<sw::BoxPanel>( pScroll, "content" );
    pContent->setOrientation( sw::UIOrientation::Vertical );
    sw::Widget* pFirst = pContent->addChild( sw::make_unique<sw::test::TestFocusableFixedWidget>( "first", sw::float2{ 50.0f, 40.0f } ) );
    fixture.addFixed( pContent, "gap", 50.0f, 110.0f );
    sw::Widget* pLast = pContent->addChild( sw::make_unique<sw::test::TestFocusableFixedWidget>( "last", sw::float2{ 50.0f, 40.0f } ) );
    fixture.update();

    sw::UIFocusManager focus;
    SW_ASSERT_TRUE( focus.setFocus( fixture.getTree(), pFirst->getId() ) );
    SW_EXPECT_TRUE( focus.navigate( fixture.getTree(), sw::UINavigationDirection::Down ) );
    SW_EXPECT_EQUAL( pLast->getId(), focus.getFocusedWidget() );
    SW_EXPECT_NEAR_EQUAL( 90.0f, pScroll->getScrollOffset()._y, 0.001f ); // 끝(190 − 100)까지 — 여백 8 을 두려다 최대에 묶인다
    fixture.update();
    SW_EXPECT_NEAR_EQUAL( 60.0f, pLast->getGeometry()._position._y, 0.001f );
    focus.clearFocus();
}
