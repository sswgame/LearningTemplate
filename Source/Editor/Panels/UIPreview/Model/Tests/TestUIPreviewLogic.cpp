#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Panels/MapCheck/Model/MapCheckCounts.h"
#include "Editor/Panels/UIPreview/Model/UIPreviewLogic.h"

#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/GridPanel.h"
#include "Engine/UI/Layout/OverlayPanel.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Layout/UIResolutionPresetTable.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/WidgetTree/Tests/UITestWidgets.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct UIPreviewTestUtil
    {
        static UIViewport makeViewport( float32 width, float32 height )
        {
            UIViewport viewport{};
            viewport._size         = float2{ width, height };
            viewport._physicalSize = float2{ width * 2.0f, height * 2.0f };
            return viewport;
        }

        static ValidationIssue makeIssue( ValidationSeverity severity, const utf8* pLabel, const utf8* pMessage )
        {
            ValidationIssue issue;
            issue._severity    = severity;
            issue._sourceLabel = pLabel;
            issue._message     = pMessage;
            return issue;
        }
    };
    using Util = UIPreviewTestUtil;
} // namespace

// ---------------------------------------------------------------------- 그림 좌표

SW_TEST_CASE( UIPreviewLogicTest, ImageFitsWithoutUpscaling )
{
    const float2 shrunk = UIPreviewLogic::fitImage( float2{ 200.0f, 100.0f }, float2{ 100.0f, 100.0f } );
    SW_EXPECT_EQUAL( 100.0f, shrunk._x );
    SW_EXPECT_EQUAL( 50.0f, shrunk._y );
    const float2 kept = UIPreviewLogic::fitImage( float2{ 50.0f, 50.0f }, float2{ 500.0f, 500.0f } );
    SW_EXPECT_EQUAL( 50.0f, kept._x );
    SW_EXPECT_EQUAL( 0.0f, UIPreviewLogic::fitImage( float2{}, float2{ 10.0f, 10.0f } )._x );
    SW_EXPECT_EQUAL( 0.0f, UIPreviewLogic::fitImage( float2{ 10.0f, 10.0f }, float2{} )._y );
}

SW_TEST_CASE( UIPreviewLogicTest, ImageAndUICoordinatesMapBothWays )
{
    const UIViewport viewport = Util::makeViewport( 1000.0f, 500.0f );
    const float2     drawSize{ 100.0f, 50.0f };
    const float2     ui = UIPreviewLogic::mapImageToUI( float2{ 10.0f, 25.0f }, drawSize, viewport );
    SW_EXPECT_EQUAL( 100.0f, ui._x );
    SW_EXPECT_EQUAL( 250.0f, ui._y );
    const UIRect image = UIPreviewLogic::mapUIToImage( UIRect{ 100.0f, 250.0f, 200.0f, 500.0f }, drawSize, viewport );
    SW_EXPECT_EQUAL( 10.0f, image._left );
    SW_EXPECT_EQUAL( 25.0f, image._top );
    SW_EXPECT_EQUAL( 20.0f, image._right );
    SW_EXPECT_EQUAL( 50.0f, image._bottom );
    SW_EXPECT_EQUAL( 0.0f, UIPreviewLogic::mapImageToUI( float2{ 1.0f, 1.0f }, float2{}, viewport )._x );
    SW_EXPECT_EQUAL( 0.0f, UIPreviewLogic::mapUIToImage( UIRect{ 1.0f, 1.0f, 2.0f, 2.0f }, drawSize, UIViewport{} )._right );
}

SW_TEST_CASE( UIPreviewLogicTest, TargetPathFollowsThePhysicalSize )
{
    SW_EXPECT_TRUE( UIPreviewLogic::makeTargetPath( Util::makeViewport( 100.0f, 50.0f ) ) == "rendertarget/editor_ui_preview_200x100" );
}

// ---------------------------------------------------------------------- 언어

SW_TEST_CASE( UIPreviewLogicTest, LanguageChoicesAreGameRealThenPseudo )
{
    vector<string> listCode;
    UIPreviewLogic::makeLanguageChoices( { "ko", "en", "ko", "", "qps_ploc" }, listCode );
    SW_REQUIRE_EQUAL( size_t{ 5 }, listCode.size() );
    SW_EXPECT_TRUE( listCode[0].empty() ); // 게임 언어 그대로
    SW_EXPECT_TRUE( listCode[1] == "en" );
    SW_EXPECT_TRUE( listCode[2] == "ko" );
    SW_EXPECT_TRUE( UIPreviewLogic::isPseudoLanguage( listCode[3] ) );
    SW_EXPECT_TRUE( UIPreviewLogic::isPseudoLanguage( listCode[4] ) );
    SW_EXPECT_FALSE( UIPreviewLogic::isPseudoLanguage( "ko" ) );
}

// ---------------------------------------------------------------------- 트리

SW_TEST_CASE( UIPreviewLogicTest, RowsAndOrdinalsFollowTheDocumentOrder )
{
    WidgetTree   tree;
    CanvasPanel& root = UITestUtil::setRoot<CanvasPanel>( tree );
    BoxPanel&    box  = UITestUtil::addPanel<BoxPanel>( root );
    box.setName( hashed_string( "List" ) );
    UITestLeaf& leaf = UITestUtil::addLeaf( box, float2{} );

    vector<UIPreviewWidgetRow> listRow;
    UIPreviewLogic::collectWidgetRows( tree, listRow );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listRow.size() );
    SW_EXPECT_EQUAL( 0u, listRow[0]._depth );
    SW_EXPECT_EQUAL( 2u, listRow[2]._depth );
    SW_EXPECT_TRUE( listRow[1]._label.find( "#List" ) != string::npos );

    SW_EXPECT_TRUE( UIPreviewLogic::findWidgetOrdinal( tree, leaf.getID() ) == 2u );
    SW_EXPECT_TRUE( UIPreviewLogic::findWidgetOrdinal( tree, kInvalidWidgetID ).has_value() == false );
    SW_EXPECT_TRUE( UIPreviewLogic::findWidgetByOrdinal( tree, 2 ) == leaf.getID() );
    SW_EXPECT_TRUE( UIPreviewLogic::findWidgetByOrdinal( tree, 3 ) == kInvalidWidgetID );
    SW_EXPECT_TRUE( UIPreviewLogic::findPanelByOrdinal( tree, 1 ) == &box );
    SW_EXPECT_TRUE( UIPreviewLogic::findPanelByOrdinal( tree, 2 ) == nullptr ); // 패널이 아니다
}

// 위에 그린 쪽(문서의 뒤)이 먼저이고, 접힌 조상 아래의 지난 기하는 고르지 않는다.
SW_TEST_CASE( UIPreviewLogicTest, PickFindsTheTopmostVisibleWidget )
{
    WidgetTree   tree;
    CanvasPanel& root  = UITestUtil::setRoot<CanvasPanel>( tree );
    CanvasPanel& group = UITestUtil::addPanel<CanvasPanel>( root, UITestUtil::makeCanvasSlot( 0.0f, 0.0f, 50.0f, 50.0f ) );
    UITestLeaf&  leaf  = UITestUtil::addLeaf( group, float2{}, UITestUtil::makeCanvasSlot( 0.0f, 0.0f, 50.0f, 50.0f ) );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );

    SW_EXPECT_TRUE( UIPreviewLogic::findWidgetAt( tree, float2{ 10.0f, 10.0f } ) == leaf.getID() );
    SW_EXPECT_TRUE( UIPreviewLogic::findWidgetAt( tree, float2{ 90.0f, 90.0f } ) == root.getID() );
    SW_EXPECT_TRUE( UIPreviewLogic::findWidgetAt( tree, float2{ 500.0f, 500.0f } ) == kInvalidWidgetID );

    group.setVisibility( WidgetVisibility::Collapsed );
    SW_EXPECT_TRUE( UIPreviewLogic::findWidgetAt( tree, float2{ 10.0f, 10.0f } ) == root.getID() );
}

SW_TEST_CASE( UIPreviewLogicTest, DropGoesIntoPanelsAndBesideLeaves )
{
    WidgetTree   tree;
    CanvasPanel& root  = UITestUtil::setRoot<CanvasPanel>( tree );
    UITestLeaf&  first = UITestUtil::addLeaf( root, float2{} );
    (void)UITestUtil::addLeaf( root, float2{} );

    uint32 index = 99;
    SW_EXPECT_TRUE( UIPreviewLogic::findDropContainer( tree, &root, index ) == &root );
    SW_EXPECT_EQUAL( 2u, index ); // 패널에 놓으면 끝이다
    SW_EXPECT_TRUE( UIPreviewLogic::findDropContainer( tree, &first, index ) == &root );
    SW_EXPECT_EQUAL( 1u, index ); // 잎에 놓으면 바로 뒤다
    SW_EXPECT_TRUE( UIPreviewLogic::findDropContainer( tree, nullptr, index ) == &root );
    SW_EXPECT_EQUAL( 2u, index ); // 빈 곳은 루트 끝이다
}

SW_TEST_CASE( UIPreviewLogicTest, DetachAndAttachMoveAWidget )
{
    WidgetTree   tree;
    CanvasPanel& root = UITestUtil::setRoot<CanvasPanel>( tree );
    BoxPanel&    box  = UITestUtil::addPanel<BoxPanel>( root );
    UITestLeaf&  leaf = UITestUtil::addLeaf( root, float2{} );

    unique_ptr<Widget> detached = UIPreviewLogic::detachWidget( tree, 0, 1 );
    SW_REQUIRE_TRUE( detached.get() == &leaf );
    SW_EXPECT_TRUE( UIPreviewLogic::detachWidget( tree, 0, 5 ) == nullptr );
    SW_EXPECT_TRUE( UIPreviewLogic::detachWidget( tree, 99, 0 ) == nullptr );

    SW_EXPECT_TRUE( UIPreviewLogic::attachWidget( tree, std::move( detached ), 1, 0 ) == &leaf );
    SW_EXPECT_TRUE( leaf.getParent() == &box );
    SW_EXPECT_TRUE( UIPreviewLogic::attachWidget( tree, nullptr, 1, 0 ) == nullptr );
}

// ---------------------------------------------------------------------- 슬롯

SW_TEST_CASE( UIPreviewLogicTest, SlotFieldsFollowTheParentPanel )
{
    WidgetTree    tree;
    CanvasPanel&  root    = UITestUtil::setRoot<CanvasPanel>( tree );
    BoxPanel&     box     = UITestUtil::addPanel<BoxPanel>( root );
    GridPanel&    grid    = UITestUtil::addPanel<GridPanel>( root );
    OverlayPanel& overlay = UITestUtil::addPanel<OverlayPanel>( root );
    UITestLeaf&   inBox   = UITestUtil::addLeaf( box, float2{} );
    UITestLeaf&   inGrid  = UITestUtil::addLeaf( grid, float2{} );
    UITestLeaf&   inPlain = UITestUtil::addLeaf( overlay, float2{} );

    SW_EXPECT_FALSE( UIPreviewLogic::findSlotFields( root )._bSizeOverride ); // 루트에는 슬롯이 없다

    const UIPreviewSlotParams canvasFields = UIPreviewLogic::findSlotFields( box );
    SW_EXPECT_TRUE( canvasFields._bCanvas );
    SW_EXPECT_FALSE( canvasFields._bPaddingAlignment );
    SW_EXPECT_TRUE( UIPreviewLogic::canEditSlotByHandle( box ) );

    const UIPreviewSlotParams boxFields = UIPreviewLogic::findSlotFields( inBox );
    SW_EXPECT_TRUE( boxFields._bBoxSize );
    SW_EXPECT_TRUE( boxFields._bPaddingAlignment );
    SW_EXPECT_FALSE( UIPreviewLogic::canEditSlotByHandle( inBox ) );

    SW_EXPECT_TRUE( UIPreviewLogic::findSlotFields( inGrid )._bGrid );
    SW_EXPECT_FALSE( UIPreviewLogic::findSlotFields( inPlain )._bGrid );
    SW_EXPECT_TRUE( UIPreviewLogic::findSlotFields( inPlain )._bSizeOverride );
}

SW_TEST_CASE( UIPreviewLogicTest, SlotDragMovesOnlyWhatTheHandleOwns )
{
    WidgetLayoutSlot slot;
    slot._offsetMin = float2{ 10.0f, 10.0f };
    slot._offsetMax = float2{ 20.0f, 20.0f };

    SW_EXPECT_FALSE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Move, float2{} ) );
    SW_EXPECT_TRUE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Move, float2{ 1.0f, 2.0f } ) );
    SW_EXPECT_EQUAL( 11.0f, slot._offsetMin._x );
    SW_EXPECT_EQUAL( 22.0f, slot._offsetMax._y );

    SW_EXPECT_TRUE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Left, float2{ 1.0f, 5.0f } ) );
    SW_EXPECT_EQUAL( 12.0f, slot._offsetMin._x );
    SW_EXPECT_EQUAL( 12.0f, slot._offsetMin._y ); // 세로는 왼쪽 손잡이의 것이 아니다
    SW_EXPECT_FALSE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Left, float2{ 0.0f, 5.0f } ) );

    SW_EXPECT_TRUE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Bottom, float2{ 0.0f, 1.0f } ) );
    SW_EXPECT_EQUAL( 23.0f, slot._offsetMax._y );

    // 자동 크기면 오른쪽 · 아래 변은 원하는 크기가 정한다.
    slot._bAutoSize = true;
    SW_EXPECT_FALSE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Right, float2{ 1.0f, 0.0f } ) );
    SW_EXPECT_FALSE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Bottom, float2{ 0.0f, 1.0f } ) );
    SW_EXPECT_TRUE( UIPreviewLogic::applySlotDrag( slot, UIPreviewSlotHandle::Top, float2{ 0.0f, 1.0f } ) );
}

// 프리셋을 고른 직후 위젯은 제자리다(여백을 빼먹으면 여백만큼 밀리고 줄어든다).
SW_TEST_CASE( UIPreviewLogicTest, AnchorPresetKeepsTheWidgetInPlace )
{
    WidgetTree       tree;
    CanvasPanel&     root = UITestUtil::setRoot<CanvasPanel>( tree );
    WidgetLayoutSlot slot = UITestUtil::makeCanvasSlot( 10.0f, 20.0f, 30.0f, 40.0f );
    slot._padding         = float4{ 1.0f, 2.0f, 3.0f, 4.0f };
    UITestLeaf& leaf      = UITestUtil::addLeaf( root, float2{}, slot );
    (void)UITestUtil::layout( tree, float2{ 200.0f, 100.0f } );
    const WidgetGeometry before = leaf.getGeometry();

    for ( uint32 preset = 0; preset < static_cast<uint32>( UIPreviewAnchorPreset::Count ); ++preset )
    {
        WidgetLayoutSlot next  = leaf.getLayoutSlot();
        const UIRect     local = UIRect{ before._position._x, before._position._y, before._position._x + before._size._x, before._position._y + before._size._y };
        (void)UIPreviewLogic::applyAnchorPreset( next, static_cast<UIPreviewAnchorPreset>( preset ), local, float2{ 200.0f, 100.0f } );
        leaf.setLayoutSlot( next );
        (void)UITestUtil::layout( tree, float2{ 200.0f, 100.0f } );
        UITestUtil::expectRect( leaf, before._position._x, before._position._y, before._size._x, before._size._y );
    }
}

SW_TEST_CASE( UIPreviewLogicTest, AnchorPresetReportsWhetherItChanged )
{
    WidgetLayoutSlot slot = UITestUtil::makeCanvasSlot( 0.0f, 0.0f, 10.0f, 10.0f );
    const UIRect     local{ 0.0f, 0.0f, 10.0f, 10.0f };
    SW_EXPECT_FALSE( UIPreviewLogic::applyAnchorPreset( slot, UIPreviewAnchorPreset::TopLeft, local, float2{ 100.0f, 100.0f } ) );
    SW_EXPECT_TRUE( UIPreviewLogic::applyAnchorPreset( slot, UIPreviewAnchorPreset::StretchAll, local, float2{ 100.0f, 100.0f } ) );
    SW_EXPECT_EQUAL( 1.0f, slot._anchorMax._x );
    SW_EXPECT_EQUAL( -90.0f, slot._offsetMax._x );
    SW_EXPECT_TRUE( string_view( UIPreviewLogic::getAnchorPresetMark( UIPreviewAnchorPreset::Center ) ) == "center" );
    const UIPreviewAnchorValues outOfRange = UIPreviewLogic::getAnchorPreset( UIPreviewAnchorPreset::Count ); // 범위 밖은 좌상이다
    SW_EXPECT_EQUAL( 0.0f, outOfRange._anchorMax._x );
}

// ---------------------------------------------------------------------- 끌어 놓기 자리

SW_TEST_CASE( UIPreviewLogicTest, BoxInsertIndexReadsTheDirectionFromTheLayout )
{
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findBoxInsertIndex( {}, 5.0f ) );
    SW_EXPECT_EQUAL( 1u, UIPreviewLogic::findBoxInsertIndex( { 10.0f, 30.0f, 50.0f }, 20.0f ) );
    SW_EXPECT_EQUAL( 3u, UIPreviewLogic::findBoxInsertIndex( { 10.0f, 30.0f, 50.0f }, 90.0f ) );
    SW_EXPECT_EQUAL( 1u, UIPreviewLogic::findBoxInsertIndex( { 50.0f, 30.0f, 10.0f }, 40.0f ) ); // 오른쪽에서 왼쪽
}

SW_TEST_CASE( UIPreviewLogicTest, ReorderIndexAccountsForTheRemovedChild )
{
    SW_EXPECT_EQUAL( 2u, UIPreviewLogic::findReorderIndex( 0, 3 ) );
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findReorderIndex( 2, 0 ) );
    SW_EXPECT_EQUAL( 2u, UIPreviewLogic::findReorderIndex( 2, 2 ) );
}

SW_TEST_CASE( UIPreviewLogicTest, GridTracksAreFoundFromTheirStarts )
{
    vector<float32> listStart;
    float32         end = 0.0f;
    UIPreviewLogic::makeGridTrackStarts( { 10.0f, 20.0f }, 5.0f, listStart, end );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listStart.size() );
    SW_EXPECT_EQUAL( 15.0f, listStart[1] );
    SW_EXPECT_EQUAL( 35.0f, end );

    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findGridTrack( listStart, end, -3.0f ) );
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findGridTrack( listStart, end, 12.0f ) ); // 사이 간격은 앞 트랙이다
    SW_EXPECT_EQUAL( 1u, UIPreviewLogic::findGridTrack( listStart, end, 15.0f ) );
    SW_EXPECT_EQUAL( 2u, UIPreviewLogic::findGridTrack( listStart, end, 35.0f ) ); // 끝 너머는 새 트랙이다
    SW_EXPECT_EQUAL( 0u, UIPreviewLogic::findGridTrack( {}, 0.0f, 5.0f ) );
}

SW_TEST_CASE( UIPreviewLogicTest, GridCellFollowsMirroredLayout )
{
    WidgetTree tree;
    GridPanel& grid = UITestUtil::setRoot<GridPanel>( tree );
    grid.setColumns( {
        UIGridTrack{UIGridTrackKind::Fixed, 30.0f},
        UIGridTrack{UIGridTrackKind::Fixed, 70.0f}
    } );
    grid.setRows( {
        UIGridTrack{UIGridTrackKind::Fixed, 10.0f},
        UIGridTrack{UIGridTrackKind::Fixed, 90.0f}
    } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    uint32 row    = 99;
    uint32 column = 99;
    SW_REQUIRE_TRUE( UIPreviewLogic::findGridCell( grid, float2{ 40.0f, 50.0f }, row, column ) );
    SW_EXPECT_EQUAL( 1u, row );
    SW_EXPECT_EQUAL( 1u, column );

    UILayoutContext context = UITestUtil::makeContext( float2{ 100.0f, 100.0f } );
    context._bRightToLeft   = true;
    (void)UILayoutPass::update( tree, context );
    SW_REQUIRE_TRUE( UIPreviewLogic::findGridCell( grid, float2{ 90.0f, 5.0f }, row, column ) );
    SW_EXPECT_EQUAL( 0u, row );
    SW_EXPECT_EQUAL( 0u, column ); // 거울이라 오른쪽 끝이 첫 열이다
}

SW_TEST_CASE( UIPreviewLogicTest, TimeLabelShowsBothTimes )
{
    SW_EXPECT_TRUE( UIPreviewLogic::makeTimeLabel( 1.5f, 3.0f ) == "1.50 / 3.00 s" );
}

SW_TEST_CASE( UIPreviewLogicTest, BoxReorderMovesPastTheCentersItCrosses )
{
    WidgetTree tree;
    BoxPanel&  box = UITestUtil::setRoot<BoxPanel>( tree );
    box.setOrientation( UIOrientation::Vertical );
    UITestLeaf& first  = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    UITestLeaf& second = UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    (void)UITestUtil::addLeaf( box, float2{ 10.0f, 10.0f } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );

    uint32       fromIndex = 99;
    uint32       toIndex   = 99;
    const float2 pastSecond{ 5.0f, second.getGeometry().computeScreenBounds().getCenter()._y + 1.0f };
    SW_REQUIRE_TRUE( UIPreviewLogic::findBoxReorder( box, first, pastSecond, fromIndex, toIndex ) );
    SW_EXPECT_EQUAL( 0u, fromIndex );
    SW_EXPECT_EQUAL( 1u, toIndex );

    const float2 onItself = first.getGeometry().computeScreenBounds().getCenter();
    SW_EXPECT_FALSE( UIPreviewLogic::findBoxReorder( box, first, onItself, fromIndex, toIndex ) ); // 제자리다

    WidgetTree other;
    SW_EXPECT_FALSE( UIPreviewLogic::findBoxReorder( box, UITestUtil::setRoot<BoxPanel>( other ), pastSecond, fromIndex, toIndex ) ); // 자식이 아니다
}

SW_TEST_CASE( UIPreviewLogicTest, GridDropWritesTheCellOnlyWhenItChanges )
{
    WidgetTree tree;
    GridPanel& grid = UITestUtil::setRoot<GridPanel>( tree );
    grid.setColumns( {
        UIGridTrack{UIGridTrackKind::Fixed, 30.0f},
        UIGridTrack{UIGridTrackKind::Fixed, 70.0f}
    } );
    grid.setRows( {
        UIGridTrack{UIGridTrackKind::Fixed, 10.0f},
        UIGridTrack{UIGridTrackKind::Fixed, 90.0f}
    } );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );

    WidgetLayoutSlot slot;
    SW_EXPECT_TRUE( UIPreviewLogic::applyGridDrop( grid, slot, float2{ 40.0f, 50.0f } ) );
    SW_EXPECT_TRUE( slot._row == 1 );
    SW_EXPECT_TRUE( slot._column == 1 );
    SW_EXPECT_FALSE( UIPreviewLogic::applyGridDrop( grid, slot, float2{ 40.0f, 50.0f } ) );
}

SW_TEST_CASE( UIPreviewLogicTest, SiblingMoveStopsAtTheEndsAndAtTheRoot )
{
    WidgetTree   tree;
    CanvasPanel& root      = UITestUtil::setRoot<CanvasPanel>( tree );
    UITestLeaf&  first     = UITestUtil::addLeaf( root, float2{} );
    UITestLeaf&  second    = UITestUtil::addLeaf( root, float2{} );
    uint32       fromIndex = 99;
    uint32       toIndex   = 99;

    SW_REQUIRE_TRUE( UIPreviewLogic::findSiblingMove( first, 1, fromIndex, toIndex ) );
    SW_EXPECT_EQUAL( 0u, fromIndex );
    SW_EXPECT_EQUAL( 1u, toIndex );
    SW_EXPECT_FALSE( UIPreviewLogic::findSiblingMove( first, -1, fromIndex, toIndex ) );
    SW_EXPECT_FALSE( UIPreviewLogic::findSiblingMove( second, 1, fromIndex, toIndex ) );
    SW_EXPECT_FALSE( UIPreviewLogic::findSiblingMove( first, 0, fromIndex, toIndex ) );
    SW_EXPECT_FALSE( UIPreviewLogic::findSiblingMove( root, 1, fromIndex, toIndex ) );
}

// ---------------------------------------------------------------------- 겹쳐 그리기 · 손잡이

// 접힌 조상 아래의 기하는 지난 배치다 — 고르기만 걸러 레이아웃 사각형은 사라진 자리에 그대로 그려졌다.
SW_TEST_CASE( UIPreviewLogicTest, LayoutRectsSkipWidgetsUnderACollapsedAncestor )
{
    WidgetTree   tree;
    CanvasPanel& root  = UITestUtil::setRoot<CanvasPanel>( tree );
    CanvasPanel& group = UITestUtil::addPanel<CanvasPanel>( root, UITestUtil::makeCanvasSlot( 0.0f, 0.0f, 50.0f, 50.0f ) );
    UITestLeaf&  leaf  = UITestUtil::addLeaf( group, float2{}, UITestUtil::makeCanvasSlot( 0.0f, 0.0f, 50.0f, 50.0f ) );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );

    vector<UIRect> listBounds;
    UIPreviewLogic::collectVisibleBounds( tree, listBounds );
    SW_EXPECT_EQUAL( size_t{ 3 }, listBounds.size() );

    group.setVisibility( WidgetVisibility::Collapsed );
    SW_EXPECT_TRUE( leaf.isVisible() );
    SW_EXPECT_FALSE( UIPreviewLogic::isVisibleInTree( leaf ) );
    UIPreviewLogic::collectVisibleBounds( tree, listBounds );
    SW_EXPECT_EQUAL( size_t{ 1 }, listBounds.size() );
}

SW_TEST_CASE( UIPreviewLogicTest, SafeZoneRectIsTheViewportMinusItsInsets )
{
    UIViewport viewport = Util::makeViewport( 100.0f, 50.0f );
    UIRect     rect{};
    SW_EXPECT_FALSE( UIPreviewLogic::findSafeZoneRect( viewport, rect ) );
    viewport._safeInsets = float4{ 1.0f, 2.0f, 3.0f, 4.0f };
    SW_REQUIRE_TRUE( UIPreviewLogic::findSafeZoneRect( viewport, rect ) );
    SW_EXPECT_EQUAL( 1.0f, rect._left );
    SW_EXPECT_EQUAL( 2.0f, rect._top );
    SW_EXPECT_EQUAL( 97.0f, rect._right );
    SW_EXPECT_EQUAL( 46.0f, rect._bottom );
}

SW_TEST_CASE( UIPreviewLogicTest, SlotHandlesSitOnTheEdgeCenters )
{
    const UIRect box{ 10.0f, 20.0f, 30.0f, 60.0f };
    SW_EXPECT_EQUAL( 20.0f, UIPreviewLogic::getSlotHandleCenter( UIPreviewSlotHandle::Move, box )._x );
    SW_EXPECT_EQUAL( 40.0f, UIPreviewLogic::getSlotHandleCenter( UIPreviewSlotHandle::Move, box )._y );
    SW_EXPECT_EQUAL( 10.0f, UIPreviewLogic::getSlotHandleCenter( UIPreviewSlotHandle::Left, box )._x );
    SW_EXPECT_EQUAL( 40.0f, UIPreviewLogic::getSlotHandleCenter( UIPreviewSlotHandle::Left, box )._y );
    SW_EXPECT_EQUAL( 20.0f, UIPreviewLogic::getSlotHandleCenter( UIPreviewSlotHandle::Top, box )._y );
    SW_EXPECT_EQUAL( 30.0f, UIPreviewLogic::getSlotHandleCenter( UIPreviewSlotHandle::Right, box )._x );
    SW_EXPECT_EQUAL( 60.0f, UIPreviewLogic::getSlotHandleCenter( UIPreviewSlotHandle::Bottom, box )._y );
}

SW_TEST_CASE( UIPreviewLogicTest, AnchorPresetIsFoundFromTheSlotOrNotAtAll )
{
    WidgetLayoutSlot slot = UITestUtil::makeCanvasSlot( 0.0f, 0.0f, 10.0f, 10.0f );
    SW_EXPECT_TRUE( UIPreviewLogic::findAnchorPreset( slot ) == UIPreviewAnchorPreset::TopLeft );
    slot._anchorMax = float2{ 1.0f, 1.0f };
    SW_EXPECT_TRUE( UIPreviewLogic::findAnchorPreset( slot ) == UIPreviewAnchorPreset::StretchAll );
    slot._anchorMin = float2{ 0.25f, 0.25f };
    SW_EXPECT_TRUE( UIPreviewLogic::findAnchorPreset( slot ) == UIPreviewAnchorPreset::Count );
}

SW_TEST_CASE( UIPreviewLogicTest, ParentLocalRectSubtractsTheParentPosition )
{
    WidgetTree   tree;
    CanvasPanel& root  = UITestUtil::setRoot<CanvasPanel>( tree );
    CanvasPanel& group = UITestUtil::addPanel<CanvasPanel>( root, UITestUtil::makeCanvasSlot( 20.0f, 30.0f, 50.0f, 50.0f ) );
    UITestLeaf&  leaf  = UITestUtil::addLeaf( group, float2{}, UITestUtil::makeCanvasSlot( 5.0f, 6.0f, 10.0f, 12.0f ) );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );

    const UIRect local = UIPreviewLogic::makeParentLocalRect( leaf );
    SW_EXPECT_EQUAL( 5.0f, local._left );
    SW_EXPECT_EQUAL( 6.0f, local._top );
    SW_EXPECT_EQUAL( 15.0f, local._right );
    SW_EXPECT_EQUAL( 18.0f, local._bottom );
}

// ---------------------------------------------------------------------- 위젯 목록 검색

SW_TEST_CASE( UIPreviewLogicTest, TreeFilterKeepsMatchesAndTheirAncestors )
{
    const auto makeRow = []( const utf8* pLabel, uint32 depth )
    {
        UIPreviewWidgetRow row{};
        row._label = pLabel;
        row._depth = depth;
        return row;
    };
    const vector<UIPreviewWidgetRow> listRow = {
        makeRow( "Canvas", 0 ),      // 0
        makeRow( "Box #Menu", 1 ),   // 1
        makeRow( "Button #Ok", 2 ),  // 2
        makeRow( "Button #No", 2 ),  // 3
        makeRow( "Box #Footer", 1 ), // 4
        makeRow( "Text #Hint", 2 ),  // 5
    };
    vector<uint32> listOrdinal;

    UIPreviewLogic::filterWidgetRows( listRow, EditorListFilter{ "" }, listOrdinal );
    SW_EXPECT_EQUAL( size_t{ 6 }, listOrdinal.size() );

    UIPreviewLogic::filterWidgetRows( listRow, EditorListFilter{ "#ok" }, listOrdinal );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listOrdinal.size() );
    SW_EXPECT_EQUAL( 0u, listOrdinal[0] );
    SW_EXPECT_EQUAL( 1u, listOrdinal[1] );
    SW_EXPECT_EQUAL( 2u, listOrdinal[2] );

    // 뒤 가지만 맞으면 앞 가지는 남지 않는다.
    UIPreviewLogic::filterWidgetRows( listRow, EditorListFilter{ "hint" }, listOrdinal );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listOrdinal.size() );
    SW_EXPECT_EQUAL( 0u, listOrdinal[0] );
    SW_EXPECT_EQUAL( 4u, listOrdinal[1] );
    SW_EXPECT_EQUAL( 5u, listOrdinal[2] );

    // 두 가지가 모두 맞으면 저마다의 조상이 남는다 — 맞지 않는 Canvas · Box #Menu 도 조상이라 보인다.
    UIPreviewLogic::filterWidgetRows( listRow, EditorListFilter{ "t" }, listOrdinal );
    SW_EXPECT_EQUAL( size_t{ 6 }, listOrdinal.size() );

    UIPreviewLogic::filterWidgetRows( listRow, EditorListFilter{ "없는 이름" }, listOrdinal );
    SW_EXPECT_TRUE( listOrdinal.empty() );
}

// 트리를 새로 지은 뒤의 편집이 옛 세대 항목과 합쳐지면 Undo 가 끊긴 항목을 불러 아무 일도 하지 않는다.
SW_TEST_CASE( UIPreviewLogicTest, CoalesceKeyDiffersAcrossGenerations )
{
    SW_EXPECT_TRUE( UIPreviewLogic::makeEditCoalesceKey( 1, 2, "_opacity" ) == UIPreviewLogic::makeEditCoalesceKey( 1, 2, "_opacity" ) );
    SW_EXPECT_FALSE( UIPreviewLogic::makeEditCoalesceKey( 1, 2, "_opacity" ) == UIPreviewLogic::makeEditCoalesceKey( 2, 2, "_opacity" ) );
    SW_EXPECT_FALSE( UIPreviewLogic::makeEditCoalesceKey( 1, 2, "_opacity" ) == UIPreviewLogic::makeEditCoalesceKey( 1, 3, "_opacity" ) );
}

// ---------------------------------------------------------------------- 입력 흉내

SW_TEST_CASE( UIPreviewPointerTrackerTest, EnteringSendsAMoveEvenWithoutMotionAndNoMoreAfterThat )
{
    UIPreviewPointerTracker tracker;
    UIPreviewPointerInput   input{};
    input._bHovered = true;

    const UIPreviewPointerPlan entered = tracker.step( input );
    SW_REQUIRE_EQUAL( size_t{ 1 }, entered._listEvent.size() );
    SW_EXPECT_TRUE( entered._listEvent[0]._kind == UIPointerEventKind::Move );
    SW_EXPECT_TRUE( entered._bMoved );
    SW_EXPECT_FALSE( entered._bMutates ); // 호버만으로는 저장을 막지 않는다

    SW_EXPECT_TRUE( tracker.step( input )._listEvent.empty() );

    input._position                  = float2{ 3.0f, 4.0f };
    const UIPreviewPointerPlan moved = tracker.step( input );
    SW_REQUIRE_EQUAL( size_t{ 1 }, moved._listEvent.size() );
    SW_EXPECT_EQUAL( 3.0f, moved._listEvent[0]._delta._x );
    SW_EXPECT_EQUAL( 4.0f, moved._listEvent[0]._delta._y );
}

SW_TEST_CASE( UIPreviewPointerTrackerTest, LeavingClearsTheHover )
{
    UIPreviewPointerTracker tracker;
    UIPreviewPointerInput   input{};
    input._bHovered = true;
    (void)tracker.step( input );

    input._bHovered                 = false;
    const UIPreviewPointerPlan left = tracker.step( input );
    SW_EXPECT_TRUE( left._bClearHover );
    SW_EXPECT_FALSE( tracker.step( input )._bClearHover ); // 한 번이다
}

// 예전에는 다른 패널에서 누른 버튼을 이미지 위에서 놓아도 Up 을 보내고 값이 바뀐 것으로 쳐 Reload 전까지 저장이 막혔다.
SW_TEST_CASE( UIPreviewPointerTrackerTest, ReleaseWithoutPressInThePreviewIsIgnored )
{
    UIPreviewPointerTracker tracker;
    UIPreviewPointerInput   input{};
    input._bHovered = true;
    (void)tracker.step( input );

    input._arrReleased[0]           = true;
    const UIPreviewPointerPlan plan = tracker.step( input );
    SW_EXPECT_TRUE( plan._listEvent.empty() );
    SW_EXPECT_FALSE( plan._bMutates );
}

SW_TEST_CASE( UIPreviewPointerTrackerTest, PressOutsideTheImageIsIgnored )
{
    UIPreviewPointerTracker tracker;
    UIPreviewPointerInput   input{};
    input._arrPressed[0]            = true;
    const UIPreviewPointerPlan plan = tracker.step( input );
    SW_EXPECT_TRUE( plan._listEvent.empty() );
    SW_EXPECT_FALSE( plan._bMutates );
}

// 슬라이더를 끌다 이미지를 벗어나도 따라오고, 밖에서 놓으면 Up 이 가고 호버가 비워진다.
SW_TEST_CASE( UIPreviewPointerTrackerTest, PressKeepsTrackingOutsideAndReleasingThereClearsTheHover )
{
    UIPreviewPointerTracker tracker;
    UIPreviewPointerInput   input{};
    input._bHovered = true;
    (void)tracker.step( input );
    input._arrPressed[0]            = true;
    const UIPreviewPointerPlan down = tracker.step( input );
    SW_REQUIRE_EQUAL( size_t{ 1 }, down._listEvent.size() );
    SW_EXPECT_TRUE( down._listEvent[0]._kind == UIPointerEventKind::Down );
    SW_EXPECT_TRUE( down._bMutates );

    input._arrPressed[0]            = false;
    input._bHovered                 = false;
    input._position                 = float2{ 500.0f, 0.0f };
    const UIPreviewPointerPlan drag = tracker.step( input );
    SW_REQUIRE_EQUAL( size_t{ 1 }, drag._listEvent.size() );
    SW_EXPECT_TRUE( drag._listEvent[0]._kind == UIPointerEventKind::Move );
    SW_EXPECT_FALSE( drag._bClearHover ); // 잡은 동안은 호버를 둔다

    input._arrReleased[0]              = true;
    const UIPreviewPointerPlan release = tracker.step( input );
    SW_REQUIRE_EQUAL( size_t{ 1 }, release._listEvent.size() );
    SW_EXPECT_TRUE( release._listEvent[0]._kind == UIPointerEventKind::Up );
    SW_EXPECT_TRUE( release._bClearHover );
    SW_EXPECT_TRUE( release._bMutates );
}

// 두 버튼을 함께 누르면 예전에는 먼저 뗀 버튼이 "누른 채" 를 지워, 밖에서 뗀 둘째 버튼의 Up 이 가지 않았다.
SW_TEST_CASE( UIPreviewPointerTrackerTest, EachButtonIsTrackedOnItsOwn )
{
    UIPreviewPointerTracker tracker;
    UIPreviewPointerInput   input{};
    input._bHovered      = true;
    input._arrPressed[0] = true;
    input._arrPressed[1] = true;
    SW_EXPECT_EQUAL( size_t{ 3 }, tracker.step( input )._listEvent.size() ); // Move, Down, Down

    input                 = UIPreviewPointerInput{};
    input._bHovered       = true;
    input._arrReleased[0] = true;
    SW_REQUIRE_EQUAL( size_t{ 1 }, tracker.step( input )._listEvent.size() );

    input                              = UIPreviewPointerInput{};
    input._arrReleased[1]              = true;
    const UIPreviewPointerPlan outside = tracker.step( input );
    SW_REQUIRE_EQUAL( size_t{ 1 }, outside._listEvent.size() );
    SW_EXPECT_TRUE( outside._listEvent[0]._button == MouseButton::Right );
    SW_EXPECT_TRUE( outside._listEvent[0]._kind == UIPointerEventKind::Up );
}

SW_TEST_CASE( UIPreviewPointerTrackerTest, WheelMutatesAndResetForgetsThePress )
{
    UIPreviewPointerTracker tracker;
    UIPreviewPointerInput   input{};
    input._bHovered = true;
    (void)tracker.step( input );
    input._wheel                     = 1.0f;
    const UIPreviewPointerPlan wheel = tracker.step( input );
    SW_REQUIRE_EQUAL( size_t{ 1 }, wheel._listEvent.size() );
    SW_EXPECT_TRUE( wheel._listEvent[0]._kind == UIPointerEventKind::Wheel );
    SW_EXPECT_TRUE( wheel._bMutates );

    input                = UIPreviewPointerInput{};
    input._bHovered      = true;
    input._arrPressed[0] = true;
    (void)tracker.step( input );
    tracker.reset();
    input                 = UIPreviewPointerInput{};
    input._arrReleased[0] = true;
    SW_EXPECT_TRUE( tracker.step( input )._listEvent.empty() ); // 트리를 새로 지었으면 옛 누름의 Up 을 보내지 않는다
}

// ---------------------------------------------------------------------- 맵 점검

SW_TEST_CASE( MapCheckRowsTest, RowsAreFilteredAndErrorsComeFirst )
{
    const vector<ValidationIssue> listIssue = {
        Util::makeIssue( ValidationSeverity::Warning, "Alpha", "soft" ),
        Util::makeIssue( ValidationSeverity::Error, "Zeta", "broken" ),
        Util::makeIssue( ValidationSeverity::Error, "Beta", "broken" ),
    };
    const MapCheckCounts counts = MapCheckCounts::countIssues( listIssue );
    SW_EXPECT_EQUAL( 2u, counts._errorCount );
    SW_EXPECT_EQUAL( 1u, counts._warningCount );

    vector<ValidationIssue> listRow;
    MapCheckCounts          shownCounts;
    MapCheckCounts::populate( listIssue, MapCheckFilter{}, listRow, shownCounts );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._sourceLabel == "Beta" );
    SW_EXPECT_TRUE( listRow[1]._sourceLabel == "Zeta" );
    SW_EXPECT_TRUE( listRow[2]._sourceLabel == "Alpha" );

    MapCheckFilter onlyWarnings;
    onlyWarnings._bShowErrors = false;
    MapCheckCounts::populate( listIssue, onlyWarnings, listRow, shownCounts );
    SW_EXPECT_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_EQUAL( 2u, shownCounts._errorCount ); // 수는 거르기와 상관없이 전체다

    MapCheckFilter search;
    search._search = "zeta";
    MapCheckCounts::populate( listIssue, search, listRow, shownCounts );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._sourceLabel == "Zeta" );
}

/**
 * @brief [UIPreviewLogicTest] 견본을 돌리면 크기가 맞바뀌고 안전 여백이 시계 방향으로 돈다(세로의 위 노치 → 가로의 오른쪽)
 */
SW_TEST_CASE( UIPreviewLogicTest, RotatePresetSwapsSizeAndTurnsInsets )
{
    UIResolutionPreset portrait{};
    portrait._width                    = 1080;
    portrait._height                   = 2400;
    portrait._safeInsets               = float4{ 1.0f, 118.0f, 3.0f, 63.0f };
    const UIResolutionPreset landscape = UIPreviewLogic::rotatePreset( portrait );
    SW_EXPECT_EQUAL( 2400u, landscape._width );
    SW_EXPECT_EQUAL( 1080u, landscape._height );
    SW_EXPECT_EQUAL( 63.0f, landscape._safeInsets._x );
    SW_EXPECT_EQUAL( 1.0f, landscape._safeInsets._y );
    SW_EXPECT_EQUAL( 118.0f, landscape._safeInsets._z );
    SW_EXPECT_EQUAL( 3.0f, landscape._safeInsets._w );
}

/**
 * @brief [UIPreviewLogicTest] 뷰포트의 안전 영역은 변마다 플랫폼 여백과 흉내 비율 중 큰 것을 UI 단위로 담고, 터치 타깃 크기를 설정에서 싣는다
 */
SW_TEST_CASE( UIPreviewLogicTest, ViewportSafeInsetsTakeTheLargerPerEdge )
{
    UIScaleSettings settings{};
    settings._rule               = UIScaleRule::Fixed;
    settings._minTouchTargetSize = 48.0f;
    const UIViewport viewport    = UIScaleSettings::makeViewport( settings, float2{ 1000.0f, 2000.0f }, 1.0f, 1.0f, float4{ 0.0f, 120.0f, 0.0f, 10.0f }, 0.01f );
    SW_EXPECT_EQUAL( 10.0f, viewport._safeInsets._x );  // 흉내 1000 × 0.01
    SW_EXPECT_EQUAL( 120.0f, viewport._safeInsets._y ); // 노치가 흉내 20 보다 크다
    SW_EXPECT_EQUAL( 10.0f, viewport._safeInsets._z );
    SW_EXPECT_EQUAL( 20.0f, viewport._safeInsets._w ); // 흉내 2000 × 0.01 이 시스템 바 10 보다 크다
    SW_EXPECT_EQUAL( 48.0f, viewport._touchTargetSize );
}

/**
 * @brief [UIPreviewLogicTest] 기기 견본의 뷰포트가 고르는 화면 태그 — 게임 UI(`UISystem::syncScreenTags`)와 미리보기(`UIOffscreenScreens::setView`)가 같은 함수(`UIScaleSettings::makeScreenTags`)를 지난다.
 *        세로 휴대 기기는 dp 너비(물리 / density)가 경계보다 좁아 compact · portrait, 돌리면 regular · landscape, 크기 0(최소화)은 등급이 없다
 */
SW_TEST_CASE( UIPreviewLogicTest, DevicePresetPicksScreenTags )
{
    UIScaleSettings settings{};
    settings._rule                              = UIScaleRule::Fixed;
    settings._compactMaxWidth                   = 600.0f;
    constexpr float32              kDensity     = 2.75f;
    const UIViewport               portrait     = UIScaleSettings::makeViewport( settings, float2{ 1080.0f, 2400.0f }, 1.0f, kDensity, float4{}, 0.0f );
    const optional<ResourceTagSet> portraitTags = settings.makeScreenTags( portrait );
    SW_REQUIRE_TRUE( portraitTags.has_value() );
    SW_EXPECT_TRUE( portraitTags->contains( "screen.compact" ) );
    SW_EXPECT_TRUE( portraitTags->contains( "orientation.portrait" ) );

    const UIViewport               landscape     = UIScaleSettings::makeViewport( settings, float2{ 2400.0f, 1080.0f }, 1.0f, kDensity, float4{}, 0.0f );
    const optional<ResourceTagSet> landscapeTags = settings.makeScreenTags( landscape );
    SW_REQUIRE_TRUE( landscapeTags.has_value() );
    SW_EXPECT_TRUE( landscapeTags->contains( "screen.regular" ) ); // 2400 / 2.75 ≈ 873dp
    SW_EXPECT_TRUE( landscapeTags->contains( "orientation.landscape" ) );

    SW_EXPECT_FALSE( settings.makeScreenTags( UIViewport{} ).has_value() );
}
