#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/GridPanel.h"
#include "Engine/UI/Layout/OverlayPanel.h"
#include "Engine/UI/Layout/SafeZonePanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Layout/WrapPanel.h"
#include "Engine/UI/WidgetTree/Tests/UITestWidgets.h"

using namespace sw;

// ---------------------------------------------------------------------- 상자

SW_TEST_CASE( UILayoutPanelsTest, BoxPlacesAutoChildrenInOrderWithSpacing )
{
    WidgetTree tree;
    BoxPanel&  box = UITestUtil::setRoot<BoxPanel>( tree );
    box.setSpacing( 5.0f );
    UITestLeaf& first  = UITestUtil::addLeaf( box, float2{ 10.0f, 20.0f } );
    UITestLeaf& second = UITestUtil::addLeaf( box, float2{ 30.0f, 40.0f } );
    (void)UITestUtil::layout( tree, float2{ 200.0f, 100.0f } );
    UITestUtil::expectRect( first, 0.0f, 0.0f, 10.0f, 100.0f );
    UITestUtil::expectRect( second, 15.0f, 0.0f, 30.0f, 100.0f );
}

SW_TEST_CASE( UILayoutPanelsTest, BoxDesiredSizeIsTheSumOnTheMainAxisAndTheMaxAcross )
{
    WidgetTree tree;
    BoxPanel&  box = UITestUtil::setRoot<BoxPanel>( tree );
    box.setOrientation( UIOrientation::Vertical );
    box.setSpacing( 2.0f );
    (void)UITestUtil::addLeaf( box, float2{ 10.0f, 20.0f } );
    (void)UITestUtil::addLeaf( box, float2{ 30.0f, 5.0f } );
    (void)UITestUtil::layout( tree, float2{ 200.0f, 100.0f } );
    SW_EXPECT_EQUAL( 30.0f, box.getDesiredSize()._x );
    SW_EXPECT_EQUAL( 27.0f, box.getDesiredSize()._y );
}

SW_TEST_CASE( UILayoutPanelsTest, BoxFillChildrenShareTheRestByWeight )
{
    WidgetTree tree;
    BoxPanel&  box = UITestUtil::setRoot<BoxPanel>( tree );
    (void)UITestUtil::addLeaf( box, float2{ 20.0f, 10.0f } );
    WidgetLayoutSlot fillSlot;
    fillSlot._sizeRule   = UISizeRule::Fill;
    fillSlot._fillWeight = 1.0f;
    UITestLeaf& light    = UITestUtil::addLeaf( box, float2{ 1.0f, 1.0f }, fillSlot );
    fillSlot._fillWeight = 3.0f;
    UITestLeaf& heavy    = UITestUtil::addLeaf( box, float2{ 1.0f, 1.0f }, fillSlot );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 10.0f } );
    UITestUtil::expectRect( light, 20.0f, 0.0f, 20.0f, 10.0f );
    UITestUtil::expectRect( heavy, 40.0f, 0.0f, 60.0f, 10.0f );
}

// 접힌 자식은 자리도 간격도 차지하지 않는다. 숨긴 자식은 자리를 차지한다.
SW_TEST_CASE( UILayoutPanelsTest, BoxCollapsedTakesNoSpaceHiddenDoes )
{
    WidgetTree tree;
    BoxPanel&  box = UITestUtil::setRoot<BoxPanel>( tree );
    box.setSpacing( 5.0f );
    (void)UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    UITestLeaf& middle = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    UITestLeaf& last   = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );

    middle.setVisibility( WidgetVisibility::Collapsed );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 10.0f } );
    SW_EXPECT_EQUAL( 15.0f, last.getGeometry()._position._x );

    middle.setVisibility( WidgetVisibility::Hidden );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 10.0f } );
    SW_EXPECT_EQUAL( 30.0f, last.getGeometry()._position._x );

    middle.setVisibility( WidgetVisibility::Visible );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 10.0f } );
    SW_EXPECT_EQUAL( 30.0f, last.getGeometry()._position._x );
}

SW_TEST_CASE( UILayoutPanelsTest, SlotPaddingAndAlignmentApply )
{
    WidgetTree tree;
    BoxPanel&  box = UITestUtil::setRoot<BoxPanel>( tree );
    box.setOrientation( UIOrientation::Vertical );
    WidgetLayoutSlot slot;
    slot._padding             = float4{ 3.0f, 4.0f, 5.0f, 6.0f };
    slot._horizontalAlignment = UIAlignment::End;
    UITestLeaf& padded        = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f }, slot );
    UITestLeaf& next          = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    UITestUtil::expectRect( padded, 85.0f, 4.0f, 10.0f, 10.0f );
    SW_EXPECT_EQUAL( 20.0f, next.getGeometry()._position._y );
}

SW_TEST_CASE( UILayoutPanelsTest, RightToLeftMirrorsTheBox )
{
    WidgetTree      tree;
    BoxPanel&       box     = UITestUtil::setRoot<BoxPanel>( tree );
    UITestLeaf&     first   = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    UITestLeaf&     second  = UITestUtil::addLeaf( box, float2{ 20.0f, 10.0f } );
    UILayoutContext context = UITestUtil::makeContext( float2{ 100.0f, 10.0f } );
    context._bRightToLeft   = true;
    (void)UILayoutPass::update( tree, context );
    SW_EXPECT_TRUE( box.isRightToLeft() );
    UITestUtil::expectRect( first, 90.0f, 0.0f, 10.0f, 10.0f );
    UITestUtil::expectRect( second, 70.0f, 0.0f, 20.0f, 10.0f );

    // 왼쪽에서 오른쪽을 고정한 패널은 문화권을 따르지 않는다.
    box.setFlowDirection( UIFlowDirection::LeftToRight );
    (void)UILayoutPass::update( tree, context );
    UITestUtil::expectRect( first, 0.0f, 0.0f, 10.0f, 10.0f );
}

SW_TEST_CASE( UILayoutPanelsTest, EdgesSnapToPhysicalPixels )
{
    WidgetTree      tree;
    BoxPanel&       box     = UITestUtil::setRoot<BoxPanel>( tree );
    UITestLeaf&     first   = UITestUtil::addLeaf( box, float2{ 10.3f, 10.0f } );
    UITestLeaf&     second  = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    UILayoutContext context = UITestUtil::makeContext( float2{ 100.0f, 10.0f } );
    context._uiScale        = 2.0f;
    (void)UILayoutPass::update( tree, context );
    SW_EXPECT_NEAR( 10.5f, first.getGeometry()._size._x, 1e-4f );
    SW_EXPECT_NEAR( 10.5f, second.getGeometry()._position._x, 1e-4f );
}

// ---------------------------------------------------------------------- 격자

SW_TEST_CASE( UILayoutPanelsTest, GridResolvesFixedAutoAndFillTracks )
{
    WidgetTree tree;
    GridPanel& grid = UITestUtil::setRoot<GridPanel>( tree );
    grid.setColumns( {
        UIGridTrack{UIGridTrackKind::Fixed, 50.0f},
        UIGridTrack{ UIGridTrackKind::Auto,  0.0f},
        UIGridTrack{ UIGridTrackKind::Fill,  1.0f}
    } );
    grid.setCellSpacing( float2{ 10.0f, 0.0f } );
    WidgetLayoutSlot slot;
    slot._column = 1;
    (void)UITestUtil::addLeaf( grid, float2{ 30.0f, 10.0f }, slot );
    slot._column     = 2;
    UITestLeaf& fill = UITestUtil::addLeaf( grid, float2{ 1.0f, 1.0f }, slot );
    (void)UITestUtil::layout( tree, float2{ 200.0f, 100.0f } );
    SW_REQUIRE_EQUAL( size_t{ 3 }, grid.getArrangedColumnSizes().size() );
    SW_EXPECT_EQUAL( 50.0f, grid.getArrangedColumnSizes()[0] );
    SW_EXPECT_EQUAL( 30.0f, grid.getArrangedColumnSizes()[1] );
    SW_EXPECT_EQUAL( 100.0f, grid.getArrangedColumnSizes()[2] );
    UITestUtil::expectRect( fill, 100.0f, 0.0f, 100.0f, 100.0f );
}

SW_TEST_CASE( UILayoutPanelsTest, GridWithoutTrackDefinitionsMakesEqualFillTracks )
{
    WidgetTree       tree;
    GridPanel&       grid = UITestUtil::setRoot<GridPanel>( tree );
    WidgetLayoutSlot slot;
    slot._column     = 2;
    UITestLeaf& leaf = UITestUtil::addLeaf( grid, float2{ 1.0f, 1.0f }, slot );
    (void)UITestUtil::layout( tree, float2{ 300.0f, 100.0f } );
    UITestUtil::expectRect( leaf, 200.0f, 0.0f, 100.0f, 100.0f );
}

// 넓은 칸의 모자란 길이는 덮은 Auto 트랙들이 고르게 나눠 갖는다.
SW_TEST_CASE( UILayoutPanelsTest, GridSpanGrowsTheAutoTracksItCovers )
{
    WidgetTree tree;
    GridPanel& grid = UITestUtil::setRoot<GridPanel>( tree );
    grid.setColumns( {
        UIGridTrack{UIGridTrackKind::Auto, 0.0f},
        UIGridTrack{UIGridTrackKind::Auto, 0.0f}
    } );
    (void)UITestUtil::addLeaf( grid, float2{ 10.0f, 1.0f } );
    WidgetLayoutSlot slot;
    slot._row        = 1;
    slot._columnSpan = 2;
    (void)UITestUtil::addLeaf( grid, float2{ 30.0f, 1.0f }, slot );
    (void)UITestUtil::layout( tree, float2{ 300.0f, 100.0f } );
    SW_REQUIRE_EQUAL( size_t{ 2 }, grid.getArrangedColumnSizes().size() );
    SW_EXPECT_EQUAL( 20.0f, grid.getArrangedColumnSizes()[0] );
    SW_EXPECT_EQUAL( 10.0f, grid.getArrangedColumnSizes()[1] );
}

SW_TEST_CASE( UILayoutPanelsTest, GridChildOutsideTheTracksGoesToTheLastOneAndWarns )
{
    SW_EXPECT_LOG( "GridPanel.GridPanelChildrenOutside" );
    WidgetTree tree;
    GridPanel& grid = UITestUtil::setRoot<GridPanel>( tree );
    grid.setColumns( {
        UIGridTrack{UIGridTrackKind::Fixed, 10.0f},
        UIGridTrack{UIGridTrackKind::Fixed, 20.0f}
    } );
    WidgetLayoutSlot slot;
    slot._column     = 5;
    UITestLeaf& leaf = UITestUtil::addLeaf( grid, float2{ 1.0f, 1.0f }, slot );
    (void)UITestUtil::layout( tree, float2{ 300.0f, 100.0f } );
    SW_EXPECT_EQUAL( 1u, grid.getOutOfRangeCellCount() );
    UITestUtil::expectRect( leaf, 10.0f, 0.0f, 20.0f, 100.0f );
}

// ---------------------------------------------------------------------- 흐름

SW_TEST_CASE( UILayoutPanelsTest, WrapBreaksLinesWhenTheNextItemDoesNotFit )
{
    WidgetTree tree;
    WrapPanel& wrap = UITestUtil::setRoot<WrapPanel>( tree );
    wrap.setItemSpacing( 10.0f );
    wrap.setLineSpacing( 5.0f );
    UITestLeaf& first  = UITestUtil::addLeaf( wrap, float2{ 40.0f, 10.0f } );
    UITestLeaf& second = UITestUtil::addLeaf( wrap, float2{ 40.0f, 10.0f } );
    UITestLeaf& third  = UITestUtil::addLeaf( wrap, float2{ 40.0f, 10.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    UITestUtil::expectRect( first, 0.0f, 0.0f, 40.0f, 10.0f );
    UITestUtil::expectRect( second, 50.0f, 0.0f, 40.0f, 10.0f );
    UITestUtil::expectRect( third, 0.0f, 15.0f, 40.0f, 10.0f );
    SW_EXPECT_EQUAL( 90.0f, wrap.getDesiredSize()._x );
    SW_EXPECT_EQUAL( 25.0f, wrap.getDesiredSize()._y );
}

// 줄보다 넓은 항목은 혼자 한 줄을 차지하고, 다음 항목은 그다음 줄로 간다(멈추지 않는다).
SW_TEST_CASE( UILayoutPanelsTest, WrapOversizedItemTakesItsOwnLine )
{
    WidgetTree  tree;
    WrapPanel&  wrap = UITestUtil::setRoot<WrapPanel>( tree );
    UITestLeaf& big  = UITestUtil::addLeaf( wrap, float2{ 150.0f, 10.0f } );
    UITestLeaf& next = UITestUtil::addLeaf( wrap, float2{ 40.0f, 10.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    UITestUtil::expectRect( big, 0.0f, 0.0f, 150.0f, 10.0f );
    UITestUtil::expectRect( next, 0.0f, 10.0f, 40.0f, 10.0f );
}

SW_TEST_CASE( UILayoutPanelsTest, WrapWithOnlyCollapsedChildrenWantsNothing )
{
    WidgetTree tree;
    WrapPanel& wrap = UITestUtil::setRoot<WrapPanel>( tree );
    wrap.setLineSpacing( 5.0f );
    UITestUtil::addLeaf( wrap, float2{ 40.0f, 10.0f } ).setVisibility( WidgetVisibility::Collapsed );
    UITestUtil::addLeaf( wrap, float2{ 40.0f, 10.0f } ).setVisibility( WidgetVisibility::Collapsed );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    SW_EXPECT_EQUAL( 0.0f, wrap.getDesiredSize()._x );
    SW_EXPECT_EQUAL( 0.0f, wrap.getDesiredSize()._y );
}

// ---------------------------------------------------------------------- 캔버스

SW_TEST_CASE( UILayoutPanelsTest, CanvasAnchorsStretchAndPin )
{
    WidgetTree       tree;
    CanvasPanel&     canvas = UITestUtil::setRoot<CanvasPanel>( tree );
    WidgetLayoutSlot stretch;
    stretch._anchorMax         = float2{ 1.0f, 1.0f };
    stretch._offsetMin         = float2{ 10.0f, 10.0f };
    stretch._offsetMax         = float2{ -10.0f, -10.0f };
    UITestLeaf&      stretched = UITestUtil::addLeaf( canvas, float2{ 1.0f, 1.0f }, stretch );
    WidgetLayoutSlot center;
    center._anchorMin    = float2{ 0.5f, 0.5f };
    center._anchorMax    = float2{ 0.5f, 0.5f };
    center._offsetMin    = float2{ -10.0f, -10.0f };
    center._offsetMax    = float2{ 10.0f, 10.0f };
    UITestLeaf& centered = UITestUtil::addLeaf( canvas, float2{ 1.0f, 1.0f }, center );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    UITestUtil::expectRect( stretched, 10.0f, 10.0f, 80.0f, 80.0f );
    UITestUtil::expectRect( centered, 40.0f, 40.0f, 20.0f, 20.0f );
}

SW_TEST_CASE( UILayoutPanelsTest, CanvasAutoSizeGrowsFromTheChosenEdge )
{
    WidgetTree       tree;
    CanvasPanel&     canvas = UITestUtil::setRoot<CanvasPanel>( tree );
    WidgetLayoutSlot slot;
    slot._anchorMin      = float2{ 1.0f, 0.0f };
    slot._anchorMax      = float2{ 1.0f, 0.0f };
    slot._offsetMin      = float2{};
    slot._offsetMax      = float2{};
    slot._bAutoSize      = true;
    slot._growHorizontal = UIGrowDirection::Begin;
    UITestLeaf& leaf     = UITestUtil::addLeaf( canvas, float2{ 30.0f, 20.0f }, slot );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    UITestUtil::expectRect( leaf, 70.0f, 0.0f, 30.0f, 20.0f );
}

// 패널보다 큰 자동 크기는 패널에 맞춰 줄이고 패널 안으로 밀어 넣는다.
SW_TEST_CASE( UILayoutPanelsTest, CanvasAutoSizeStaysInsideThePanel )
{
    WidgetTree       tree;
    CanvasPanel&     canvas = UITestUtil::setRoot<CanvasPanel>( tree );
    WidgetLayoutSlot slot;
    slot._offsetMin  = float2{ 50.0f, 0.0f };
    slot._offsetMax  = float2{};
    slot._bAutoSize  = true;
    UITestLeaf& leaf = UITestUtil::addLeaf( canvas, float2{ 200.0f, 20.0f }, slot );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    UITestUtil::expectRect( leaf, 0.0f, 0.0f, 100.0f, 20.0f );
}

SW_TEST_CASE( UILayoutPanelsTest, CanvasPaintOrderFollowsZStably )
{
    WidgetTree       tree;
    CanvasPanel&     canvas = UITestUtil::setRoot<CanvasPanel>( tree );
    WidgetLayoutSlot slot;
    slot._zOrder = 1;
    (void)UITestUtil::addLeaf( canvas, float2{}, slot );
    slot._zOrder = 0;
    (void)UITestUtil::addLeaf( canvas, float2{}, slot );
    slot._zOrder = 1;
    (void)UITestUtil::addLeaf( canvas, float2{}, slot );
    vector<uint32> listOrder;
    canvas.collectPaintOrder( listOrder );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listOrder.size() );
    SW_EXPECT_EQUAL( 1u, listOrder[0] );
    SW_EXPECT_EQUAL( 0u, listOrder[1] );
    SW_EXPECT_EQUAL( 2u, listOrder[2] );
}

// ---------------------------------------------------------------------- 겹침 · 안전 영역

SW_TEST_CASE( UILayoutPanelsTest, OverlayChildrenShareTheRect )
{
    WidgetTree       tree;
    OverlayPanel&    overlay = UITestUtil::setRoot<OverlayPanel>( tree );
    WidgetLayoutSlot padded;
    padded._padding._x    = 5.0f;
    UITestLeaf&      back = UITestUtil::addLeaf( overlay, float2{ 10.0f, 10.0f }, padded );
    WidgetLayoutSlot corner;
    corner._horizontalAlignment = UIAlignment::End;
    corner._verticalAlignment   = UIAlignment::End;
    UITestLeaf& front           = UITestUtil::addLeaf( overlay, float2{ 10.0f, 10.0f }, corner );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    UITestUtil::expectRect( back, 5.0f, 0.0f, 95.0f, 100.0f );
    UITestUtil::expectRect( front, 90.0f, 90.0f, 10.0f, 10.0f );
    SW_EXPECT_EQUAL( 15.0f, overlay.getDesiredSize()._x );
}

SW_TEST_CASE( UILayoutPanelsTest, SafeZoneKeepsChildrenInsideTheSafeArea )
{
    WidgetTree      tree;
    SafeZonePanel&  zone    = UITestUtil::setRoot<SafeZonePanel>( tree );
    UITestLeaf&     leaf    = UITestUtil::addLeaf( zone, float2{ 1.0f, 1.0f } );
    UILayoutContext context = UITestUtil::makeContext( float2{ 100.0f, 100.0f } );
    context._safeInsets     = float4{ 10.0f, 20.0f, 5.0f, 0.0f };
    (void)UILayoutPass::update( tree, context );
    UITestUtil::expectRect( leaf, 10.0f, 20.0f, 85.0f, 80.0f );
}

// 안전 영역은 화면의 것이다 — 오른쪽에서 왼쪽 문화권이어도 왼쪽 노치는 왼쪽에 있다.
SW_TEST_CASE( UILayoutPanelsTest, SafeZoneDoesNotMirrorInRightToLeft )
{
    WidgetTree      tree;
    SafeZonePanel&  zone    = UITestUtil::setRoot<SafeZonePanel>( tree );
    UITestLeaf&     leaf    = UITestUtil::addLeaf( zone, float2{ 1.0f, 1.0f } );
    UILayoutContext context = UITestUtil::makeContext( float2{ 100.0f, 100.0f } );
    context._safeInsets     = float4{ 10.0f, 0.0f, 0.0f, 0.0f };
    context._bRightToLeft   = true;
    (void)UILayoutPass::update( tree, context );
    UITestUtil::expectRect( leaf, 10.0f, 0.0f, 90.0f, 100.0f );
}

// 안전 영역에 닿지 않은 패널은 그대로다.
SW_TEST_CASE( UILayoutPanelsTest, SafeZoneAwayFromTheEdgeStaysPut )
{
    WidgetTree      tree;
    CanvasPanel&    canvas  = UITestUtil::setRoot<CanvasPanel>( tree );
    SafeZonePanel&  zone    = UITestUtil::addPanel<SafeZonePanel>( canvas, UITestUtil::makeCanvasSlot( 30.0f, 30.0f, 40.0f, 40.0f ) );
    UITestLeaf&     leaf    = UITestUtil::addLeaf( zone, float2{ 1.0f, 1.0f } );
    UILayoutContext context = UITestUtil::makeContext( float2{ 100.0f, 100.0f } );
    context._safeInsets     = float4{ 10.0f, 10.0f, 10.0f, 10.0f };
    (void)UILayoutPass::update( tree, context );
    UITestUtil::expectRect( leaf, 30.0f, 30.0f, 40.0f, 40.0f );
}

// ---------------------------------------------------------------------- 스크롤

SW_TEST_CASE( UILayoutPanelsTest, ScrollOffsetIsClampedToTheOverflow )
{
    WidgetTree   tree;
    ScrollPanel& scroll = UITestUtil::setRoot<ScrollPanel>( tree );
    UITestLeaf&  leaf   = UITestUtil::addLeaf( scroll, float2{ 50.0f, 300.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    SW_EXPECT_EQUAL( 200.0f, scroll.getMaxScrollOffset()._y );
    SW_EXPECT_EQUAL( 100.0f, scroll.getDesiredSize()._y );

    scroll.setScrollOffset( float2{ 30.0f, 500.0f } );
    SW_EXPECT_EQUAL( 0.0f, scroll.getScrollOffset()._x ); // 가로는 스크롤하지 않는다
    SW_EXPECT_EQUAL( 200.0f, scroll.getScrollOffset()._y );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    UITestUtil::expectRect( leaf, 0.0f, -200.0f, 100.0f, 300.0f );
}

SW_TEST_CASE( UILayoutPanelsTest, ScrollFollowsContentThatShrinks )
{
    WidgetTree   tree;
    ScrollPanel& scroll = UITestUtil::setRoot<ScrollPanel>( tree );
    UITestLeaf&  leaf   = UITestUtil::addLeaf( scroll, float2{ 50.0f, 300.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    scroll.setScrollOffset( float2{ 0.0f, 200.0f } );
    leaf.setSize( float2{ 50.0f, 150.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    SW_EXPECT_EQUAL( 50.0f, scroll.getScrollOffset()._y );
    leaf.setSize( float2{ 50.0f, 10.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    SW_EXPECT_EQUAL( 0.0f, scroll.getScrollOffset()._y );
}

SW_TEST_CASE( UILayoutPanelsTest, ScrollWheelAndStickMoveUntilTheEnd )
{
    WidgetTree   tree;
    ScrollPanel& scroll = UITestUtil::setRoot<ScrollPanel>( tree );
    (void)UITestUtil::addLeaf( scroll, float2{ 50.0f, 200.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );

    UIPointerEvent wheel;
    wheel._kind     = UIPointerEventKind::Wheel;
    wheel._position = float2{ 10.0f, 10.0f };
    wheel._wheel    = -1.0f;
    SW_EXPECT_FALSE( scroll.onPointerEvent( wheel, UIRoutePhase::Tunnel ).isHandled() );
    SW_EXPECT_TRUE( scroll.onPointerEvent( wheel, UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 48.0f, scroll.getScrollOffset()._y ); // 휠 한 칸의 기본값

    UIActionEvent stick;
    stick._action       = hashed_string( UIActionName::kScroll );
    stick._value        = float2{ 0.0f, -1.0f };
    stick._deltaSeconds = 1.0f;
    SW_EXPECT_TRUE( scroll.onActionEvent( stick, UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 100.0f, scroll.getScrollOffset()._y );

    // 끝에 닿으면 처리하지 않아 바깥 스크롤이 받는다.
    SW_EXPECT_FALSE( scroll.onPointerEvent( wheel, UIRoutePhase::Bubble ).isHandled() );
}

SW_TEST_CASE( UILayoutPanelsTest, ScrollBarThumbReflectsTheOffset )
{
    WidgetTree   tree;
    ScrollPanel& scroll = UITestUtil::setRoot<ScrollPanel>( tree );
    (void)UITestUtil::addLeaf( scroll, float2{ 50.0f, 300.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    scroll.setScrollOffset( float2{ 0.0f, 100.0f } );
    ScrollPanel::ScrollBarLayout bar{};
    SW_EXPECT_FALSE( scroll.computeScrollBar( UIOrientation::Horizontal, bar ) );
    SW_REQUIRE_TRUE( scroll.computeScrollBar( UIOrientation::Vertical, bar ) );
    SW_EXPECT_NEAR( 100.0f / 3.0f, bar._thumbSize._y, 1e-3f );
    SW_EXPECT_NEAR( 100.0f / 3.0f, bar._thumbPosition._y, 1e-3f );
}

SW_TEST_CASE( UILayoutPanelsTest, ScrollBarDragAndTrackPress )
{
    WidgetTree   tree;
    ScrollPanel& scroll = UITestUtil::setRoot<ScrollPanel>( tree );
    (void)UITestUtil::addLeaf( scroll, float2{ 50.0f, 300.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );

    // 트랙을 누르면 한 화면만큼 옮기고 잡지 않는다.
    UIPointerEvent press;
    press._kind              = UIPointerEventKind::Down;
    press._position          = float2{ 97.0f, 80.0f };
    const UIReply trackReply = scroll.onPointerEvent( press, UIRoutePhase::Tunnel );
    SW_EXPECT_TRUE( trackReply.isHandled() );
    SW_EXPECT_TRUE( trackReply._bCapturePointer == SW_FALSE );
    SW_EXPECT_EQUAL( 100.0f, scroll.getScrollOffset()._y );

    // 엄지를 끌면 잡고, 빈 트랙 길이에 비례해 옮긴다.
    scroll.setScrollOffset( float2{} );
    press._position          = float2{ 97.0f, 10.0f };
    const UIReply thumbReply = scroll.onPointerEvent( press, UIRoutePhase::Tunnel );
    SW_EXPECT_TRUE( thumbReply._bCapturePointer == SW_TRUE );
    UIPointerEvent move;
    move._kind     = UIPointerEventKind::Move;
    move._position = float2{ 97.0f, 10.0f + 100.0f / 3.0f };
    SW_EXPECT_TRUE( scroll.onPointerEvent( move, UIRoutePhase::Tunnel ).isHandled() );
    SW_EXPECT_NEAR( 100.0f, scroll.getScrollOffset()._y, 1e-2f );
    UIPointerEvent release;
    release._kind         = UIPointerEventKind::Up;
    const UIReply upReply = scroll.onPointerEvent( release, UIRoutePhase::Bubble );
    SW_EXPECT_TRUE( upReply._bReleasePointer == SW_TRUE );
    SW_EXPECT_FALSE( scroll.onPointerEvent( move, UIRoutePhase::Tunnel ).isHandled() );
}

SW_TEST_CASE( UILayoutPanelsTest, ScrollIntoViewMovesOnlyAsMuchAsNeeded )
{
    WidgetTree       tree;
    ScrollPanel&     scroll = UITestUtil::setRoot<ScrollPanel>( tree );
    WidgetLayoutSlot contentSlot;
    BoxPanel&        content = UITestUtil::addPanel<BoxPanel>( scroll, contentSlot );
    content.setOrientation( UIOrientation::Vertical );
    vector<UITestLeaf*> listLeaf;
    for ( uint32 index = 0; index < 10; ++index )
    {
        listLeaf.push_back( &UITestUtil::addLeaf( content, float2{ 10.0f, 50.0f } ) );
    }
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    scroll.setNavigationMargin( 8.0f );

    SW_EXPECT_TRUE( scroll.scrollIntoView( *listLeaf[4] ) ); // y 200..250 의 끝이 92 까지 와야 한다
    SW_EXPECT_NEAR( 158.0f, scroll.getScrollOffset()._y, 1e-3f );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    SW_EXPECT_FALSE( scroll.scrollIntoView( *listLeaf[4] ) ); // 이미 보인다

    SW_EXPECT_TRUE( scroll.scrollIntoView( *listLeaf[0] ) ); // 위로 돌아가면 처음에 닿는다
    SW_EXPECT_EQUAL( 0.0f, scroll.getScrollOffset()._y );

    SW_EXPECT_FALSE( scroll.scrollIntoView( scroll ) ); // 자손이 아니다
}

// ---------------------------------------------------------------------- 무효화

SW_TEST_CASE( UILayoutPanelsTest, NothingDirtyMeasuresNothing )
{
    WidgetTree  tree;
    BoxPanel&   box  = UITestUtil::setRoot<BoxPanel>( tree );
    UITestLeaf& leaf = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    SW_EXPECT_TRUE( UITestUtil::layout( tree, float2{ 100.0f, 100.0f } ) > 0u );
    SW_EXPECT_EQUAL( 0u, UITestUtil::layout( tree, float2{ 100.0f, 100.0f } ) );

    leaf.setSize( float2{ 20.0f, 10.0f } );
    SW_EXPECT_TRUE( UITestUtil::layout( tree, float2{ 100.0f, 100.0f } ) > 0u );
    UITestUtil::expectRect( leaf, 0.0f, 0.0f, 20.0f, 100.0f );
}

// 두 축 크기를 정한 패널은 레이아웃 경계다 — 그 안의 변화가 바깥을 다시 측정하지 않는다.
SW_TEST_CASE( UILayoutPanelsTest, LayoutBoundaryStopsTheRemeasure )
{
    WidgetTree       tree;
    BoxPanel&        box = UITestUtil::setRoot<BoxPanel>( tree );
    WidgetLayoutSlot fixedSlot;
    fixedSlot._widthOverride  = 50.0f;
    fixedSlot._heightOverride = 50.0f;
    OverlayPanel& boundary    = UITestUtil::addPanel<OverlayPanel>( box, fixedSlot );
    UITestLeaf&   inner       = UITestUtil::addLeaf( boundary, float2{ 10.0f, 10.0f } );
    UITestLeaf&   sibling     = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    const uint32 siblingCount = sibling.getMeasureCount();

    inner.setSize( float2{ 20.0f, 20.0f } );
    SW_EXPECT_EQUAL( 2u, UITestUtil::layout( tree, float2{ 100.0f, 100.0f } ) ); // 경계와 안쪽 잎
    SW_EXPECT_EQUAL( siblingCount, sibling.getMeasureCount() );
    SW_EXPECT_EQUAL( 50.0f, sibling.getGeometry()._position._x );
}

// 접힌 동안 바뀐 자손은 다시 보일 때 측정한다.
SW_TEST_CASE( UILayoutPanelsTest, ChangesWhileCollapsedAreMeasuredWhenShown )
{
    WidgetTree    tree;
    BoxPanel&     box   = UITestUtil::setRoot<BoxPanel>( tree );
    OverlayPanel& panel = UITestUtil::addPanel<OverlayPanel>( box );
    UITestLeaf&   leaf  = UITestUtil::addLeaf( panel, float2{ 10.0f, 10.0f } );
    UITestLeaf&   next  = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );

    panel.setVisibility( WidgetVisibility::Collapsed );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    SW_EXPECT_EQUAL( 0.0f, next.getGeometry()._position._x );

    leaf.setSize( float2{ 30.0f, 10.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    panel.setVisibility( WidgetVisibility::Visible );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    SW_EXPECT_EQUAL( 30.0f, next.getGeometry()._position._x );
}

SW_TEST_CASE( UILayoutPanelsTest, ViewportAndScaleChangesReachTheTree )
{
    WidgetTree  tree;
    BoxPanel&   box  = UITestUtil::setRoot<BoxPanel>( tree );
    UITestLeaf& leaf = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );

    (void)UITestUtil::layout( tree, float2{ 100.0f, 60.0f } );
    UITestUtil::expectRect( leaf, 0.0f, 0.0f, 10.0f, 60.0f );

    const uint32    before  = leaf.getMeasureCount();
    UILayoutContext context = UITestUtil::makeContext( float2{ 100.0f, 60.0f } );
    context._uiScale        = 2.0f;
    SW_EXPECT_TRUE( UILayoutPass::update( tree, context ) > 0u );
    SW_EXPECT_TRUE( leaf.getMeasureCount() > before );
}

SW_TEST_CASE( UILayoutPanelsTest, MeasureHonoursOverrideMinAndMax )
{
    WidgetTree       tree;
    BoxPanel&        box = UITestUtil::setRoot<BoxPanel>( tree );
    WidgetLayoutSlot slot;
    slot._widthOverride    = 40.0f;
    slot._minSize          = float2{ 0.0f, 25.0f };
    UITestLeaf&      fixed = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f }, slot );
    WidgetLayoutSlot limited;
    limited._maxSize  = float2{ 5.0f, 0.0f };
    UITestLeaf& small = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f }, limited );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    SW_EXPECT_EQUAL( 40.0f, fixed.getDesiredSize()._x );
    SW_EXPECT_EQUAL( 25.0f, fixed.getDesiredSize()._y );
    SW_EXPECT_EQUAL( 5.0f, small.getDesiredSize()._x );
}
