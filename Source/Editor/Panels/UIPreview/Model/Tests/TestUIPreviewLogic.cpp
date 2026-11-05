#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/MapCheck/Model/MapCheckRows.h"
#include "Editor/Panels/UIPreview/Model/UIPreviewLogic.h"

#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/GridPanel.h"
#include "Engine/UI/Layout/OverlayPanel.h"
#include "Engine/UI/Layout/UILayoutPass.h"
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

    SW_EXPECT_EQUAL( 2u, UIPreviewLogic::findWidgetOrdinal( tree, leaf.getID() ) );
    SW_EXPECT_EQUAL( UIPreviewLogic::kNoOrdinal, UIPreviewLogic::findWidgetOrdinal( tree, kInvalidWidgetID ) );
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

    const UIPreviewSlotFields canvasFields = UIPreviewLogic::findSlotFields( box );
    SW_EXPECT_TRUE( canvasFields._bCanvas );
    SW_EXPECT_FALSE( canvasFields._bPaddingAlignment );
    SW_EXPECT_TRUE( UIPreviewLogic::canEditSlotByHandle( box ) );

    const UIPreviewSlotFields boxFields = UIPreviewLogic::findSlotFields( inBox );
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

// ---------------------------------------------------------------------- 맵 점검

SW_TEST_CASE( MapCheckRowsTest, RowsAreFilteredAndErrorsComeFirst )
{
    const vector<ValidationIssue> listIssue = {
        Util::makeIssue( ValidationSeverity::Warning, "Alpha", "soft" ),
        Util::makeIssue( ValidationSeverity::Error, "Zeta", "broken" ),
        Util::makeIssue( ValidationSeverity::Error, "Beta", "broken" ),
    };
    const MapCheckCounts counts = MapCheckRows::countIssues( listIssue );
    SW_EXPECT_EQUAL( 2u, counts._errorCount );
    SW_EXPECT_EQUAL( 1u, counts._warningCount );

    vector<ValidationIssue> listRow;
    MapCheckCounts          shownCounts;
    MapCheckRows::populate( listIssue, MapCheckFilter{}, listRow, shownCounts );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._sourceLabel == "Beta" );
    SW_EXPECT_TRUE( listRow[1]._sourceLabel == "Zeta" );
    SW_EXPECT_TRUE( listRow[2]._sourceLabel == "Alpha" );

    MapCheckFilter onlyWarnings;
    onlyWarnings._bShowErrors = false;
    MapCheckRows::populate( listIssue, onlyWarnings, listRow, shownCounts );
    SW_EXPECT_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_EQUAL( 2u, shownCounts._errorCount ); // 수는 거르기와 상관없이 전체다

    MapCheckFilter search;
    search._search = "zeta";
    MapCheckRows::populate( listIssue, search, listRow, shownCounts );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._sourceLabel == "Zeta" );
}
