#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Panels/TileMapPaintUtil.h"

#include "TestFramework/TestFramework.h"

// TileMapPaintUtilTest — 타일맵 칠하기가 빠른 드래그에서 칸을 건너뛰지 않는다(패널 점검 D18). ImGui 없음.

/**
 * @brief [TileMapPaintUtilTest] 한 프레임에 지나간 두 칸 사이는 빈틈 없는 선분이다 — 두 끝 포함, 이웃 칸은 x · y 가 1 이하로 다르다
 * @details 패널이 마우스가 눌린 프레임마다 그 자리 칸 하나만 칠해, 빠르게 끌면 지운 칸 사이에 지워지지 않은 칸이 남았다.
 */
SW_TEST_CASE( TileMapPaintUtilTest, LineCellsHaveNoGaps )
{
    using sw::int2;
    using sw::editor::TileMapPaintUtil;
    sw::vector<int2> listCell;
    TileMapPaintUtil::collectLineCells( int2{ 0, 0 }, int2{ 7, 3 }, listCell );
    SW_ASSERT_EQUAL( 8u, static_cast<uint32>( listCell.size() ) ); // 긴 축의 칸 수 + 1
    SW_EXPECT_EQUAL( 0, listCell.front()._x );
    SW_EXPECT_EQUAL( 0, listCell.front()._y );
    SW_EXPECT_EQUAL( 7, listCell.back()._x );
    SW_EXPECT_EQUAL( 3, listCell.back()._y );
    for ( size_t cellIndex = 1; cellIndex < listCell.size(); ++cellIndex )
    {
        SW_EXPECT_TRUE( sw::MathUtil::abs( listCell[cellIndex]._x - listCell[cellIndex - 1]._x ) <= 1 );
        SW_EXPECT_TRUE( sw::MathUtil::abs( listCell[cellIndex]._y - listCell[cellIndex - 1]._y ) <= 1 );
    }

    // 거꾸로 끈 획(오른쪽 아래 → 왼쪽 위)과 세로 획.
    TileMapPaintUtil::collectLineCells( int2{ 7, 3 }, int2{ 0, 0 }, listCell );
    SW_EXPECT_EQUAL( 8u, static_cast<uint32>( listCell.size() ) );
    SW_EXPECT_EQUAL( 0, listCell.back()._x );
    TileMapPaintUtil::collectLineCells( int2{ 2, 5 }, int2{ 2, 0 }, listCell );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( listCell.size() ) );

    // 움직이지 않은 프레임은 그 칸 하나.
    TileMapPaintUtil::collectLineCells( int2{ 4, 4 }, int2{ 4, 4 }, listCell );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listCell.size() ) );
}

/**
 * @brief [TileMapPaintUtilTest] 사각형은 두 모서리를 포함하고 맵 밖은 잘린다
 */
SW_TEST_CASE( TileMapPaintUtilTest, RectCellsAreClippedToTheMap )
{
    using sw::int2;
    using sw::editor::TileMapPaintUtil;
    sw::vector<int2> listCell;
    TileMapPaintUtil::collectRectCells( int2{ 3, 2 }, int2{ 1, 1 }, 8, 8, listCell );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( listCell.size() ) ); // 3 x 2
    TileMapPaintUtil::collectRectCells( int2{ -2, -2 }, int2{ 1, 9 }, 4, 4, listCell );
    SW_EXPECT_EQUAL( 8u, static_cast<uint32>( listCell.size() ) ); // x 0..1, y 0..3
}

/**
 * @brief [TileMapPaintUtilTest] 채우기는 같은 값으로 상하좌우 이어진 칸만 고른다 — 벽 너머와 대각선은 넘지 않는다
 */
SW_TEST_CASE( TileMapPaintUtilTest, FloodFillStopsAtDifferentCells )
{
    using sw::int2;
    using sw::editor::TileMapPaintUtil;
    // 4 x 3, 가운데 세로 벽(1) — 왼쪽 2 열과 오른쪽 1 열이 나뉜다
    const sw::vector<uint64> listValue = {
        0,
        0,
        1,
        0,
        0,
        0,
        1,
        0,
        0,
        0,
        1,
        0,
    };
    sw::vector<int2> listCell;
    TileMapPaintUtil::collectFloodFillCells( listValue, 4, 3, int2{ 0, 0 }, listCell );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( listCell.size() ) );
    TileMapPaintUtil::collectFloodFillCells( listValue, 4, 3, int2{ 3, 1 }, listCell );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listCell.size() ) );
    TileMapPaintUtil::collectFloodFillCells( listValue, 4, 3, int2{ 4, 0 }, listCell );
    SW_EXPECT_TRUE( listCell.empty() );
}

/**
 * @brief [TileMapPaintUtilTest] 화면 위치 → 칸은 음수 쪽도 내림이고, 아틀라스 칸 번호와 UV 는 행 우선으로 오간다
 */
SW_TEST_CASE( TileMapPaintUtilTest, CellLookupRoundsDownAndAtlasCellsRoundTrip )
{
    using sw::int2;
    using sw::editor::AtlasGridUtil;
    using sw::editor::TileMapPaintUtil;
    const int2 inside = TileMapPaintUtil::findCellAt( 37.0f, 5.0f, 18.0f );
    SW_EXPECT_EQUAL( 2, inside._x );
    SW_EXPECT_EQUAL( 0, inside._y );
    const int2 outside = TileMapPaintUtil::findCellAt( -1.0f, -0.5f, 18.0f );
    SW_EXPECT_EQUAL( -1, outside._x );
    SW_EXPECT_EQUAL( -1, outside._y );

    const sw::float4 uvRect = AtlasGridUtil::computeCellUvRect( 6, 4, 4 ); // 열 2, 행 1
    SW_EXPECT_NEAR_EQUAL( 0.5f, uvRect._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, uvRect._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, uvRect._z, 1e-6f );
    SW_EXPECT_EQUAL( 6, AtlasGridUtil::findCellAtUv( uvRect._x + 0.01f, uvRect._y + 0.01f, 4, 4 ) );
    SW_EXPECT_EQUAL( -1, AtlasGridUtil::findCellAtUv( 1.2f, 0.5f, 4, 4 ) );
}
