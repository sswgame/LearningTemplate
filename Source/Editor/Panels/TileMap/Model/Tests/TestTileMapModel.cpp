#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/TileMap/Model/TileMapModel.h"
#include "Editor/Panels/TileMap/Model/TileMapPaintUtil.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TileMapModelTestUtil
    {
        static TileMapToolInput makeBrushInput( int32 x, int32 y )
        {
            TileMapToolInput input;
            input._hoverCell   = int2{ x, y };
            input._bPainting   = true;
            input._bButtonDown = true;
            return input;
        }

        /** @brief 걷기 플래그가 지금 켜져 있는 칸인지 봅니다. */
        static bool isWalkable( const TileMapModel& document, int32 x, int32 y )
        {
            const size_t index = static_cast<size_t>( y ) * static_cast<size_t>( document.getMap()._width ) + static_cast<size_t>( x );
            return document.getMap().getFlagLayer( TileFlagLayer::Walkable )[index] != 0;
        }

        static void selectLayer( TileMapModel& document, TileMapModel::PaintLayer layer ) { document.selectPaintLayer( static_cast<int32>( layer ) ); }
    };
    using Util = TileMapModelTestUtil;
} // namespace

// ---------------------------------------------------------------------- 칠하는 칸 계산

SW_TEST_CASE( TileMapPaintUtilTest, LineVisitsEveryCellBetweenTheEnds )
{
    vector<int2> listCell;
    TileMapPaintUtil::collectLineCells( int2{ 2, 2 }, int2{ 2, 2 }, listCell );
    SW_EXPECT_EQUAL( size_t{ 1 }, listCell.size() );

    TileMapPaintUtil::collectLineCells( int2{ 0, 0 }, int2{ 3, 0 }, listCell );
    SW_EXPECT_EQUAL( size_t{ 4 }, listCell.size() );

    TileMapPaintUtil::collectLineCells( int2{ 3, 3 }, int2{ 0, 0 }, listCell );
    SW_REQUIRE_EQUAL( size_t{ 4 }, listCell.size() );
    SW_EXPECT_EQUAL( 3, listCell[0]._x );
    SW_EXPECT_EQUAL( 0, listCell[3]._y );

    // 이웃한 칸끼리 이어져 구멍이 없다.
    TileMapPaintUtil::collectLineCells( int2{ -2, 5 }, int2{ 7, -1 }, listCell );
    SW_EXPECT_EQUAL( -2, listCell.front()._x );
    SW_EXPECT_EQUAL( -1, listCell.back()._y );
    for ( size_t index = 1; index < listCell.size(); ++index )
    {
        const int32 stepX = listCell[index]._x - listCell[index - 1]._x;
        const int32 stepY = listCell[index]._y - listCell[index - 1]._y;
        SW_EXPECT_TRUE( stepX >= -1 && stepX <= 1 && stepY >= -1 && stepY <= 1 );
    }
}

SW_TEST_CASE( TileMapPaintUtilTest, RectIsClippedToTheMapAndAcceptsAnyCorners )
{
    vector<int2> listCell;
    TileMapPaintUtil::collectRectCells( int2{ 2, 2 }, int2{ 1, 1 }, 8, 8, listCell );
    SW_EXPECT_EQUAL( size_t{ 4 }, listCell.size() );
    TileMapPaintUtil::collectRectCells( int2{ -5, -5 }, int2{ 100, 0 }, 3, 3, listCell );
    SW_EXPECT_EQUAL( size_t{ 3 }, listCell.size() );
    TileMapPaintUtil::collectRectCells( int2{ 9, 9 }, int2{ 10, 10 }, 3, 3, listCell );
    SW_EXPECT_TRUE( listCell.empty() );
}

SW_TEST_CASE( TileMapPaintUtilTest, FloodFillStopsAtOtherValuesAndEdges )
{
    // 1 1 0
    // 0 1 0
    // 1 0 0   왼쪽 아래 1 은 대각선이라 이어지지 않는다
    const vector<uint64> listValue = { 1, 1, 0, 0, 1, 0, 1, 0, 0 };
    vector<int2>         listCell;
    TileMapPaintUtil::collectFloodFillCells( listValue, 3, 3, int2{ 0, 0 }, listCell );
    SW_EXPECT_EQUAL( size_t{ 3 }, listCell.size() );
    TileMapPaintUtil::collectFloodFillCells( listValue, 3, 3, int2{ 2, 2 }, listCell );
    SW_EXPECT_EQUAL( size_t{ 4 }, listCell.size() );
    TileMapPaintUtil::collectFloodFillCells( listValue, 3, 3, int2{ 3, 0 }, listCell );
    SW_EXPECT_TRUE( listCell.empty() );
    TileMapPaintUtil::collectFloodFillCells( listValue, 4, 3, int2{ 0, 0 }, listCell ); // 크기가 맞지 않는 값 목록
    SW_EXPECT_TRUE( listCell.empty() );
}

SW_TEST_CASE( TileMapPaintUtilTest, CellUnderThePointerFloorsTowardNegative )
{
    const int2 inside = TileMapPaintUtil::findCellAt( 25.0f, 9.0f, 10.0f );
    SW_EXPECT_EQUAL( 2, inside._x );
    SW_EXPECT_EQUAL( 0, inside._y );
    const int2 before = TileMapPaintUtil::findCellAt( -1.0f, -11.0f, 10.0f ); // 맵 왼쪽 위 밖은 음수다(0 번 칸이 아니다)
    SW_EXPECT_EQUAL( -1, before._x );
    SW_EXPECT_EQUAL( -2, before._y );
    SW_EXPECT_EQUAL( -1, TileMapPaintUtil::findCellAt( 5.0f, 5.0f, 0.0f )._x );
}

SW_TEST_CASE( AtlasGridUtilTest, CellAndUVRoundTrip )
{
    const float4 rect = AtlasGridUtil::computeCellUVRect( 5, 4, 2 );
    SW_EXPECT_NEAR( 0.25f, rect._x, 1e-5f );
    SW_EXPECT_NEAR( 0.5f, rect._y, 1e-5f );
    SW_EXPECT_NEAR( 0.25f, rect._z, 1e-5f );
    SW_EXPECT_NEAR( 0.5f, rect._w, 1e-5f );
    SW_EXPECT_TRUE( AtlasGridUtil::findCellAtUV( rect._x + 0.01f, rect._y + 0.01f, 4, 2 ) == 5 );
    SW_EXPECT_FALSE( AtlasGridUtil::findCellAtUV( 1.0f, 0.5f, 4, 2 ).has_value() );
    SW_EXPECT_FALSE( AtlasGridUtil::findCellAtUV( 0.5f, -0.1f, 4, 2 ).has_value() );
    SW_EXPECT_TRUE( AtlasGridUtil::findCellAtUV( 0.9f, 0.9f, 0, 0 ) == 0 ); // 틀린 격자는 한 칸으로 본다
}

// ---------------------------------------------------------------------- 문서: 값이 바뀌었을 때만 고쳤다고 한다

// 끌어 칠할 때 같은 칸에 머물면 프레임마다 "고쳤다" 가 나오면 안 된다. 저장 표시와 되돌리기 기록이 그 답을 따른다.
SW_TEST_CASE( TileMapModelTest, FlagBrushReportsOnlyRealChanges )
{
    TileMapModel document;
    SW_EXPECT_TRUE( Util::isWalkable( document, 1, 1 ) );
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( 1, 1 ) ) ); // 이미 켜져 있다

    document.editErase() = true;
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 1, 1 ) ) );
    SW_EXPECT_FALSE( Util::isWalkable( document, 1, 1 ) );
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( 1, 1 ) ) ); // 같은 칸에 머문다
    SW_EXPECT_EQUAL( 63u, document.countSelectedFlagCells() );
}

SW_TEST_CASE( TileMapModelTest, VisualBrushReportsOnlyRealChanges )
{
    TileMapModel document;
    Util::selectLayer( document, TileMapModel::PaintLayer::Visual );
    document.editErase() = true;
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) ); // 이미 새 칸의 값이다

    document.editErase()       = false;
    document.editPaintHeight() = 3;
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( document.getMap()._listVisual[0]._height ) );
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) );

    document.editErase() = true; // 지우기는 새 칸의 값으로 되돌린다
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( document.getMap()._listVisual[0]._height ) );
}

// 칸은 0..255 를 받는다 - 그대로 자르면 -1 이 255, 300 이 44 가 됐다.
SW_TEST_CASE( TileMapModelTest, VisualBrushClampsHeightAndAtlas )
{
    TileMapModel document;
    Util::selectLayer( document, TileMapModel::PaintLayer::Visual );
    document.editPaintHeight() = 300;
    document.editAtlasID()     = -1;
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) );
    SW_EXPECT_EQUAL( 255, static_cast<int32>( document.getMap()._listVisual[0]._height ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( document.getMap()._listVisual[0]._atlasID ) );
}

// 스포이트로 집어 다시 칠해도 틴트가 줄어들지 않는다 - 내림이면 칠할 때마다 한 단계씩 어두워졌다.
SW_TEST_CASE( TileMapModelTest, PickedTintPaintsTheSameBytes )
{
    TileMapModel document;
    Util::selectLayer( document, TileMapModel::PaintLayer::Visual );
    for ( int32 byteValue = 0; byteValue <= 255; ++byteValue )
    {
        document.editTint()[0] = static_cast<float32>( byteValue ) / 255.0f;
        (void)document.applyToolInput( Util::makeBrushInput( 0, 0 ) );
        SW_REQUIRE_EQUAL( byteValue, static_cast<int32>( document.getMap()._listVisual[0]._tintR ) );
    }
}

SW_TEST_CASE( TileMapModelTest, WarpPaintReplacesAndErases )
{
    TileMapModel document;
    Util::selectLayer( document, TileMapModel::PaintLayer::Warp );
    document.editErase() = true;
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( 2, 2 ) ) ); // 지울 것이 없다

    document.editErase()      = false;
    document.editWarpTarget() = "maps/next.xml";
    document.editWarpTileX()  = 4;
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 2, 2 ) ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, document.getMap()._listWarp.size() );
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( 2, 2 ) ) ); // 같은 내용이다

    document.editWarpTileX() = 5;
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 2, 2 ) ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, document.getMap()._listWarp.size() ); // 칸마다 하나다
    SW_EXPECT_EQUAL( 5, document.getMap()._listWarp[0]._targetTileX );

    document.editErase() = true;
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 2, 2 ) ) );
    SW_EXPECT_TRUE( document.getMap()._listWarp.empty() );
}

// 빈 대상은 같은 맵 워프다(스키마). 비었다고 칠하면서 지우지 않는다.
SW_TEST_CASE( TileMapModelTest, WarpWithoutTargetIsASameMapWarp )
{
    TileMapModel document;
    Util::selectLayer( document, TileMapModel::PaintLayer::Warp );
    document.editWarpTarget().clear();
    document.editWarpTileX() = 6;
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 2, 2 ) ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, document.getMap()._listWarp.size() );
    SW_EXPECT_TRUE( document.getMap()._listWarp[0]._targetMap.empty() );
    SW_EXPECT_EQUAL( 6, document.getMap()._listWarp[0]._targetTileX );
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( 2, 2 ) ) );
}

// 다시 칠해도 파일의 짝 id 가 남는다 - 지우고 새로 만들면 양쪽 짝이 끊겼다.
SW_TEST_CASE( TileMapModelTest, RepaintingAWarpKeepsItsPairID )
{
    TileMapModel   document;
    TileMapXMLData data;
    SW_REQUIRE_TRUE( data.resetTiles( 4, 4 ) );
    TileMapXMLData::Warp warp{};
    warp._tileX     = 1;
    warp._targetMap = "maps/a.tilemap.xml";
    warp._pairID    = "door";
    data._listWarp.push_back( warp );
    document.applyText( data.toXML() );

    Util::selectLayer( document, TileMapModel::PaintLayer::Warp );
    document.editWarpTarget() = "maps/b.tilemap.xml";
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 1, 0 ) ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, document.getMap()._listWarp.size() );
    SW_EXPECT_TRUE( document.getMap()._listWarp[0]._pairID == "door" );
    SW_EXPECT_TRUE( document.getMap()._listWarp[0]._targetMap == "maps/b.tilemap.xml" );
}

// 빠르게 끌어도 지난 칸에서 이번 칸까지 끊기지 않는다.
SW_TEST_CASE( TileMapModelTest, BrushStrokeFillsTheGapBetweenFrames )
{
    TileMapModel document;
    document.editErase() = true;
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) );
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 4, 0 ) ) );
    for ( int32 x = 0; x <= 4; ++x )
    {
        SW_EXPECT_FALSE( Util::isWalkable( document, x, 0 ) );
    }
    // 눌렀다 뗀 뒤의 새 획은 지난 칸에서 잇지 않는다.
    TileMapToolInput release;
    release._hoverCell = int2{ 4, 0 };
    SW_EXPECT_FALSE( document.applyToolInput( release ) );
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 7, 7 ) ) );
    SW_EXPECT_TRUE( Util::isWalkable( document, 5, 3 ) );
}

SW_TEST_CASE( TileMapModelTest, BrushOutsideTheMapEditsNothing )
{
    TileMapModel document;
    document.editErase() = true;
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( -3, -3 ) ) );
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( -3, 100 ) ) ); // 한 획으로 이어도 맵을 지나지 않는다
}

SW_TEST_CASE( TileMapModelTest, RectAppliesOnRelease )
{
    TileMapModel document;
    document.setTool( TileMapModel::PaintTool::Rect );
    document.editErase() = true;
    TileMapToolInput press;
    press._hoverCell   = int2{ 1, 1 };
    press._bClicked    = true;
    press._bButtonDown = true;
    SW_EXPECT_FALSE( document.applyToolInput( press ) );
    int2 start{};
    SW_EXPECT_TRUE( document.readDragStart( start ) );
    SW_EXPECT_EQUAL( 1, start._x );

    TileMapToolInput release;
    release._hoverCell = int2{ 2, 2 };
    SW_EXPECT_TRUE( document.applyToolInput( release ) );
    SW_EXPECT_FALSE( document.readDragStart( start ) );
    SW_EXPECT_EQUAL( 60u, document.countSelectedFlagCells() );

    SW_EXPECT_FALSE( document.applyToolInput( release ) ); // 놓은 뒤에는 다시 칠하지 않는다
}

SW_TEST_CASE( TileMapModelTest, LineAppliesOnReleaseAndSkipsOutsideCells )
{
    TileMapModel document;
    document.setTool( TileMapModel::PaintTool::Line );
    document.editErase() = true;
    TileMapToolInput press;
    press._hoverCell   = int2{ 0, 0 };
    press._bClicked    = true;
    press._bButtonDown = true;
    SW_EXPECT_FALSE( document.applyToolInput( press ) );

    TileMapToolInput release;
    release._hoverCell = int2{ 20, 0 }; // 맵 밖까지 그어도 맵 안 칸만 칠한다
    SW_EXPECT_TRUE( document.applyToolInput( release ) );
    SW_EXPECT_EQUAL( 56u, document.countSelectedFlagCells() );
    SW_EXPECT_FALSE( document.applyToolInput( release ) );
}

// 끌던 중 도구를 바꾸면 획을 버린다 - 남겨 두면 돌아왔을 때 옛 칸에서 이어 칠했다.
SW_TEST_CASE( TileMapModelTest, SwitchingToolDropsTheActiveDrag )
{
    TileMapModel document;
    document.setTool( TileMapModel::PaintTool::Rect );
    TileMapToolInput press;
    press._hoverCell   = int2{ 1, 1 };
    press._bClicked    = true;
    press._bButtonDown = true;
    (void)document.applyToolInput( press );
    document.setTool( TileMapModel::PaintTool::Fill );
    int2 start{};
    SW_EXPECT_FALSE( document.readDragStart( start ) );

    document.editErase() = true;
    document.setTool( TileMapModel::PaintTool::Brush );
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) );
    document.setTool( TileMapModel::PaintTool::Picker );
    document.setTool( TileMapModel::PaintTool::Brush );
    (void)document.applyToolInput( Util::makeBrushInput( 4, 0 ) );
    SW_EXPECT_TRUE( Util::isWalkable( document, 2, 0 ) ); // (0,0) 에서 잇지 않았다
}

SW_TEST_CASE( TileMapModelTest, EveryPressStartsANewStroke )
{
    TileMapModel document;
    const uint32 first = document.getStrokeSerial();
    (void)document.applyToolInput( Util::makeBrushInput( 0, 0 ) ); // 끌기만 해서는 그대로다
    SW_EXPECT_EQUAL( first, document.getStrokeSerial() );
    TileMapToolInput press = Util::makeBrushInput( 1, 0 );
    press._bClicked        = true;
    (void)document.applyToolInput( press );
    SW_EXPECT_EQUAL( first + 1u, document.getStrokeSerial() );
}

SW_TEST_CASE( TileMapModelTest, FillChangesTheConnectedRegionOnly )
{
    TileMapModel document;
    document.editErase() = true;
    for ( int32 y = 0; y < 8; ++y ) // 가운데 벽
    {
        (void)document.applyToolInput( Util::makeBrushInput( 4, y ) );
    }
    TileMapToolInput release;
    (void)document.applyToolInput( release );
    SW_EXPECT_EQUAL( 56u, document.countSelectedFlagCells() );

    document.setTool( TileMapModel::PaintTool::Fill );
    TileMapToolInput click;
    click._hoverCell = int2{ 0, 0 };
    click._bClicked  = true;
    SW_EXPECT_TRUE( document.applyToolInput( click ) );
    SW_EXPECT_EQUAL( 24u, document.countSelectedFlagCells() ); // 오른쪽 3 칸 × 8

    // 이미 같은 값으로 채운 영역은 바뀐 것이 없다.
    click._hoverCell = int2{ 4, 0 };
    SW_EXPECT_FALSE( document.applyToolInput( click ) );

    click._hoverCell = int2{ 50, 50 };
    SW_EXPECT_FALSE( document.applyToolInput( click ) );
}

SW_TEST_CASE( TileMapModelTest, PickerReadsTheCellAndReturnsToTheBrush )
{
    TileMapModel document;
    document.setTool( TileMapModel::PaintTool::Picker );
    TileMapToolInput click;
    click._hoverCell = int2{ 3, 3 };
    click._bClicked  = true;
    SW_EXPECT_FALSE( document.applyToolInput( click ) );
    SW_EXPECT_TRUE( document.getTool() == TileMapModel::PaintTool::Brush );
    SW_EXPECT_FALSE( document.isErase() ); // 켜진 칸을 집었다
}

// 집은 버튼을 누른 채 끌어도 놓을 때까지 칠하지 않는다.
SW_TEST_CASE( TileMapModelTest, PickerHoldDoesNotPaintUntilRelease )
{
    TileMapModel document;
    document.editErase() = true;
    SW_REQUIRE_TRUE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) );
    TileMapToolInput release;
    (void)document.applyToolInput( release );

    document.setTool( TileMapModel::PaintTool::Picker );
    TileMapToolInput press = Util::makeBrushInput( 0, 0 );
    press._bClicked        = true;
    (void)document.applyToolInput( press ); // 꺼진 칸을 집었으니 지우기다
    SW_EXPECT_TRUE( document.isErase() );
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( 3, 3 ) ) );
    SW_EXPECT_TRUE( Util::isWalkable( document, 3, 3 ) );
    (void)document.applyToolInput( release );
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 3, 3 ) ) );
}

SW_TEST_CASE( TileMapModelTest, DataLayerPaintsAndErasesToItsDefault )
{
    TileMapModel document;
    SW_REQUIRE_TRUE( document.addDataLayer( "Depth", 7 ) );
    SW_EXPECT_TRUE( document.getPaintLayer() == TileMapModel::PaintLayer::Data );
    SW_EXPECT_FALSE( document.addDataLayer( "Depth", 1 ) ); // 겹치는 이름
    SW_EXPECT_FALSE( document.addDataLayer( "", 1 ) );
    SW_EXPECT_FALSE( document.getStatus().empty() );

    document.editPaintDataValue() = 3;
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) );
    SW_EXPECT_EQUAL( 1u, document.countDataCells() );
    SW_EXPECT_FALSE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) );
    document.editErase() = true;
    SW_EXPECT_TRUE( document.applyToolInput( Util::makeBrushInput( 0, 0 ) ) );
    SW_EXPECT_EQUAL( 0u, document.countDataCells() );
}

SW_TEST_CASE( TileMapModelTest, LayerListAndIndexAgree )
{
    TileMapModel document;
    SW_REQUIRE_TRUE( document.addDataLayer( "Depth", 0 ) );
    vector<const utf8*> listName;
    document.collectPaintLayerNames( listName );
    for ( int32 index = 0; index < static_cast<int32>( listName.size() ); ++index )
    {
        document.selectPaintLayer( index );
        SW_EXPECT_EQUAL( index, document.getPaintLayerIndex() );
    }
    SW_EXPECT_TRUE( string_view( listName.back() ) == "Depth" );
}

SW_TEST_CASE( TileMapModelTest, EdgeWarpCoversTheEdgeWithoutDuplicates )
{
    TileMapModel document;
    SW_EXPECT_FALSE( document.paintEdgeWarp( 0 ) ); // 대상이 없다
    SW_EXPECT_FALSE( document.paintEdgeWarp( -1 ) );
    SW_EXPECT_FALSE( document.paintEdgeWarp( 99 ) );
    document.editEdgePreset( 1 )._target = "maps/east.xml";
    document.editErase()                 = true;
    (void)document.applyToolInput( Util::makeBrushInput( 7, 3 ) ); // 동쪽 끝 한 칸을 막아 둔다
    SW_REQUIRE_TRUE( document.paintEdgeWarp( 1 ) );
    SW_EXPECT_EQUAL( size_t{ 8 }, document.getMap()._listWarp.size() );
    SW_EXPECT_TRUE( Util::isWalkable( document, 7, 3 ) ); // 넘어갈 칸은 걸을 수 있다
    SW_EXPECT_FALSE( document.paintEdgeWarp( 1 ) );       // 바뀐 칸이 없다
    SW_EXPECT_EQUAL( size_t{ 8 }, document.getMap()._listWarp.size() );
    document.editEdgePreset( 2 )._target = "maps/south.xml";
    SW_REQUIRE_TRUE( document.paintEdgeWarp( 2 ) );
    SW_EXPECT_EQUAL( size_t{ 15 }, document.getMap()._listWarp.size() ); // 남동쪽 모서리는 겹친다
}

SW_TEST_CASE( TileMapModelTest, SnapshotRoundTripRestoresTheMap )
{
    TileMapModel document;
    document.editErase() = true;
    (void)document.applyToolInput( Util::makeBrushInput( 1, 1 ) );
    document.editName()   = "Cave";
    const string snapshot = document.captureText();

    TileMapToolInput release;
    (void)document.applyToolInput( release );
    (void)document.applyToolInput( Util::makeBrushInput( 2, 2 ) );
    document.editName() = "Other";
    document.applyText( snapshot );
    SW_EXPECT_FALSE( Util::isWalkable( document, 1, 1 ) );
    SW_EXPECT_TRUE( Util::isWalkable( document, 2, 2 ) );
    SW_EXPECT_TRUE( string_view( document.editName().c_str() ) == "Cave" );
}

// 옛 번호로 다른 레이어를 칠하지 않는다.
SW_TEST_CASE( TileMapModelTest, SnapshotWithoutTheDataLayerGoesBackToFlags )
{
    TileMapModel document;
    const string before = document.captureText();
    SW_REQUIRE_TRUE( document.addDataLayer( "Depth", 0 ) );
    document.applyText( before );
    SW_EXPECT_TRUE( document.getPaintLayer() == TileMapModel::PaintLayer::Flag );
}

SW_TEST_CASE( TileMapModelTest, OversizedResizeIsRefused )
{
    SW_EXPECT_LOG( "TileMapModel.TileMapResizeXBeyond" );
    TileMapModel document;
    document.editInputWidth()  = 100000;
    document.editInputHeight() = 100000;
    SW_EXPECT_FALSE( document.applyInputSize() );
    SW_EXPECT_EQUAL( 8, document.getMap()._width );
    SW_EXPECT_FALSE( document.getStatus().empty() );

    document.editInputWidth()  = 0; // 0 은 1 로 올린다
    document.editInputHeight() = 3;
    SW_EXPECT_TRUE( document.applyInputSize() );
    SW_EXPECT_EQUAL( 1, document.getMap()._width );
    SW_EXPECT_EQUAL( 3, document.getMap()._height );
}

// 크기를 바꿔도 겹치는 칸 · 안에 남는 워프가 남는다(유니티 · Godot 와 같다) - 예전에는 모든 레이어를 지웠다.
SW_TEST_CASE( TileMapModelTest, ResizeKeepsOverlappingCellsAndWarps )
{
    TileMapModel document;
    document.editErase() = true;
    (void)document.applyToolInput( Util::makeBrushInput( 1, 1 ) ); // 플래그
    TileMapToolInput release;
    (void)document.applyToolInput( release );
    document.editErase() = false;
    SW_REQUIRE_TRUE( document.addDataLayer( "Cost", 2 ) );
    document.editPaintDataValue() = 9;
    (void)document.applyToolInput( Util::makeBrushInput( 2, 1 ) ); // 데이터
    (void)document.applyToolInput( release );
    document.editEdgePreset( 0 )._target = "maps/north.xml";
    SW_REQUIRE_TRUE( document.paintEdgeWarp( 0 ) ); // y = 0 줄의 8 칸

    document.editInputWidth()  = 3;
    document.editInputHeight() = 12;
    SW_REQUIRE_TRUE( document.applyInputSize() );
    SW_EXPECT_FALSE( Util::isWalkable( document, 1, 1 ) );
    SW_EXPECT_TRUE( Util::isWalkable( document, 0, 11 ) ); // 새 칸은 기본값이다
    int32 value = 0;
    SW_REQUIRE_TRUE( document.getMap().getCellData( 0, 2, 1, value ) );
    SW_EXPECT_EQUAL( 9, value );
    SW_REQUIRE_TRUE( document.getMap().getCellData( 0, 2, 11, value ) );
    SW_EXPECT_EQUAL( 2, value );
    SW_EXPECT_EQUAL( size_t{ 3 }, document.getMap()._listWarp.size() ); // 맵 밖 워프는 빠진다

    SW_EXPECT_FALSE( document.applyInputSize() ); // 크기가 그대로다
}

// 스폰이 맵 밖이면 읽기가 거절한다 - 줄인 맵은 저장도 되돌리기도 실패했다.
SW_TEST_CASE( TileMapModelTest, ShrinkingKeepsTheMapReadable )
{
    TileMapModel document;
    document.editInputWidth()  = 1;
    document.editInputHeight() = 1;
    SW_REQUIRE_TRUE( document.applyInputSize() );
    SW_EXPECT_EQUAL( 0, document.getMap()._spawnX );
    SW_EXPECT_EQUAL( 0, document.getMap()._spawnY );
    TileMapXMLData readBack;
    SW_EXPECT_TRUE( readBack.loadFromXML( document.captureText() ) );
}

SW_TEST_CASE( TileMapModelTest, SpawnInputRefusesCellsOutsideTheMap )
{
    TileMapModel document;
    document.editInputSpawnX() = 8;
    SW_EXPECT_FALSE( document.applyInputSpawn() );
    SW_EXPECT_EQUAL( 1, document.editInputSpawnX() ); // 칸을 되돌린다
    SW_EXPECT_FALSE( document.getStatus().empty() );

    document.editInputSpawnX() = 7;
    document.editInputSpawnY() = 0;
    SW_EXPECT_TRUE( document.applyInputSpawn() );
    SW_EXPECT_EQUAL( 7, document.getMap()._spawnX );
    SW_EXPECT_FALSE( document.applyInputSpawn() ); // 그대로다

    TileMapXMLData readBack;
    SW_REQUIRE_TRUE( readBack.loadFromXML( document.captureText() ) );
    SW_EXPECT_EQUAL( 7, readBack._spawnX );
    document.applyText( document.captureText() );
    SW_EXPECT_EQUAL( 7, document.editInputSpawnX() );
}

SW_TEST_CASE( TileMapModelTest, RemovingDataLayersFallsBackToFlags )
{
    TileMapModel document;
    SW_EXPECT_FALSE( document.removeSelectedDataLayer() ); // 플래그 레이어를 고른 상태다
    SW_REQUIRE_TRUE( document.addDataLayer( "A", 0 ) );
    SW_REQUIRE_TRUE( document.addDataLayer( "B", 0 ) );
    SW_EXPECT_TRUE( document.removeSelectedDataLayer() );
    SW_EXPECT_TRUE( document.getPaintLayer() == TileMapModel::PaintLayer::Data );
    SW_EXPECT_EQUAL( 0, document.getDataLayerIndex() );
    SW_EXPECT_TRUE( document.removeSelectedDataLayer() );
    SW_EXPECT_TRUE( document.getPaintLayer() == TileMapModel::PaintLayer::Flag );
    SW_EXPECT_TRUE( document.getMap()._listDataLayer.empty() );
}

// 없는 파일은 새 문서다 - 앞 문서의 내용으로 시작하면 저장할 때 그 내용이 새 파일에 쓰였고, "저장 안 함" 도 편집을 버리지 못했다.
SW_TEST_CASE( TileMapModelTest, MissingFileStartsANewMap )
{
    TileMapModel document;
    document.editInputWidth() = 12;
    SW_REQUIRE_TRUE( document.applyInputSize() );
    SW_REQUIRE_TRUE( document.addDataLayer( "Depth", 0 ) );

    const string missingPath = test::makeScratchPath( "SwTileMapDocumentMissing.tilemap.xml" );
    SW_EXPECT_TRUE( document.loadXML( missingPath ) == ToolAssetLoadResult::Missing );
    SW_EXPECT_EQUAL( TileMapModel::kNewMapSide, document.getMap()._width );
    SW_EXPECT_TRUE( document.getMap()._listDataLayer.empty() );
    SW_EXPECT_TRUE( document.getPaintLayer() == TileMapModel::PaintLayer::Flag );
}

SW_TEST_CASE( TileMapModelTest, SaveAndLoadRoundTripWithStatus )
{
    const string path = test::makeScratchPath( "SwTileMapDocumentRoundTrip.tilemap.xml" );
    {
        TileMapModel document;
        document.editPath()  = path.c_str();
        document.editErase() = true;
        (void)document.applyToolInput( Util::makeBrushInput( 1, 1 ) );
        SW_REQUIRE_TRUE( document.saveDocument() );
        SW_EXPECT_FALSE( document.getStatus().empty() );
    }
    TileMapModel document;
    SW_EXPECT_TRUE( document.loadXML( path ) == ToolAssetLoadResult::Loaded );
    SW_EXPECT_FALSE( Util::isWalkable( document, 1, 1 ) );
    (void)FileUtil::removeFile( path );
}

SW_TEST_CASE( TileMapModelTest, DescribeCellReadsTheSelectedLayer )
{
    TileMapModel document;
    SW_EXPECT_TRUE( document.describeCell( 99, 0 ).empty() );
    SW_EXPECT_TRUE( document.describeCell( 1, 2 ) == "1, 2: 1" ); // 걷기 플래그
    SW_REQUIRE_TRUE( document.addDataLayer( "Depth", 7 ) );
    SW_EXPECT_TRUE( document.describeCell( 1, 2 ) == "1, 2: 7" );
    Util::selectLayer( document, TileMapModel::PaintLayer::Warp );
    SW_EXPECT_TRUE( document.describeCell( 1, 2 ) == "1, 2" );
}

// ---------------------------------------------------------------------- 캔버스 보기

SW_TEST_CASE( TileMapCanvasViewTest, WheelKeepsTheCellUnderThePointer )
{
    TileMapCanvasView view;
    const float2      pointer{ 100.0f, 60.0f };
    const float32     oldCell = view.computeCellSize( 1.0f );
    const float32     cellX   = ( pointer._x - view.getPan()._x ) / oldCell;
    view.applyWheel( 3.0f, pointer );
    SW_EXPECT_NEAR( 1.3f, view.getZoom(), 1e-5f );
    SW_EXPECT_NEAR( cellX, ( pointer._x - view.getPan()._x ) / view.computeCellSize( 1.0f ), 1e-3f );

    view.applyWheel( -1000.0f, pointer ); // 범위 안에서 멈춘다
    SW_EXPECT_NEAR( TileMapCanvasView::kMinZoom, view.getZoom(), 1e-5f );
    view.reset();
    SW_EXPECT_NEAR( 1.0f, view.getZoom(), 1e-5f );
    SW_EXPECT_NEAR( TileMapCanvasView::kMargin, view.getPan()._x, 1e-5f );
}

// 큰 맵은 보이는 칸만 그린다.
SW_TEST_CASE( TileMapPaintUtilTest, VisibleCellsAreClippedToCanvasAndMap )
{
    TileCellRange range = TileMapPaintUtil::computeVisibleCells( float2{ -25.0f, 5.0f }, float2{ 100.0f, 30.0f }, 10.0f, 2048, 2048 );
    SW_EXPECT_EQUAL( 2, range._min._x );
    SW_EXPECT_EQUAL( 13, range._max._x );
    SW_EXPECT_EQUAL( 0, range._min._y );
    SW_EXPECT_EQUAL( 3, range._max._y );

    range = TileMapPaintUtil::computeVisibleCells( float2{ 0.0f, 0.0f }, float2{ 1000.0f, 1000.0f }, 10.0f, 4, 4 );
    SW_EXPECT_EQUAL( 4, range._max._x );

    range = TileMapPaintUtil::computeVisibleCells( float2{ -5000.0f, 0.0f }, float2{ 100.0f, 100.0f }, 10.0f, 4, 4 ); // 맵이 화면 밖이다
    SW_EXPECT_EQUAL( range._min._x, range._max._x );

    range = TileMapPaintUtil::computeVisibleCells( float2{ 0.0f, 0.0f }, float2{ 100.0f, 100.0f }, 0.0f, 4, 4 );
    SW_EXPECT_EQUAL( 0, range._max._x );
}
