#include "pch.h"

#include "Engine/Utility/TileMap/TileGridUtil.h"
#include "Engine/Utility/TileMap/TileSetAsset.h"

#include "GameFramework/Navigation/GridPathfinder.h"
#include "GameFramework/Navigation/NavGrid.h"

#include "TestFramework/TestFramework.h"

// TileGridUtilTest — 타일 격자에서 충돌 사각형 병합 · 외곽선 · 이동 비용. 디바이스 없음(nogpu).

/**
 * @brief [TileGridUtilTest] 병합 사각형은 단단한 칸을 하나도 빠짐없이 꼭 한 번 덮고 빈 칸은 덮지 않으며, 칸마다 상자보다 훨씬 적다
 * @details ㄱ 자(위 한 줄 · 왼쪽 세로 · 오른아래 한 칸): 위 줄 3 칸이 하나, 왼쪽 아래 2 칸이 하나, 외딴 칸 하나 — 사각형 셋(칸 여섯). 벽 16 × 16 의 테두리는
 *          칸 60 개지만 사각형 넷이다.
 */
SW_TEST_CASE( TileGridUtilTest, MergedRectanglesCoverEverySolidCellOnce )
{
    // X X X
    // X . .
    // X . X
    const sw::vector<uint8>  listSolid = { 1, 1, 1, 1, 0, 0, 1, 0, 1 };
    sw::vector<sw::TileRect> listRect;
    sw::TileGridUtil::mergeSolidRectangles( listSolid, 3, 3, listRect );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listRect.size() ) );
    sw::vector<uint32> listCover( 9, 0 );
    for ( const sw::TileRect& rect : listRect )
    {
        for ( int32 y = rect._y; y < rect._y + rect._height; ++y )
        {
            for ( int32 x = rect._x; x < rect._x + rect._width; ++x )
                ++listCover[static_cast<size_t>( y * 3 + x )];
        }
    }
    for ( size_t cellIndex = 0; cellIndex < 9; ++cellIndex )
        SW_EXPECT_EQUAL( static_cast<uint32>( listSolid[cellIndex] ), listCover[cellIndex] );

    // 테두리 벽: 칸 60 개 → 사각형 4 개.
    constexpr int32   kSize = 16;
    sw::vector<uint8> listBorder( static_cast<size_t>( kSize * kSize ), 0 );
    for ( int32 index = 0; index < kSize; ++index )
    {
        listBorder[static_cast<size_t>( index )]                         = 1;
        listBorder[static_cast<size_t>( ( kSize - 1 ) * kSize + index )] = 1;
        listBorder[static_cast<size_t>( index * kSize )]                 = 1;
        listBorder[static_cast<size_t>( index * kSize + kSize - 1 )]     = 1;
    }
    sw::TileGridUtil::mergeSolidRectangles( listBorder, kSize, kSize, listRect );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( listRect.size() ) );
}

/**
 * @brief [TileGridUtilTest] 외곽선은 단단한 칸과 빈 칸(맵 밖 포함) 사이의 변을 같은 방향끼리 이어 붙이고 바깥쪽 방향을 단다
 * @details 2 × 3 덩어리는 토막 넷(위 · 아래 · 왼 · 오른, 각각 바깥쪽이 그 방향)이다. 붙어 있는 두 칸 사이의 변은 외곽선이 아니다. 오목한 ㄱ 자는 토막이 여섯이다.
 */
SW_TEST_CASE( TileGridUtilTest, OutlineJoinsEdgesAndPointsOutward )
{
    // . . . .
    // X X X .
    // X X X .
    const sw::vector<uint8>  listBlock = { 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0 };
    sw::vector<sw::TileEdge> listEdge;
    sw::TileGridUtil::traceSolidOutline( listBlock, 4, 3, listEdge );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( listEdge.size() ) );
    bool bTop   = false;
    bool bRight = false;
    for ( const sw::TileEdge& edge : listEdge )
    {
        if ( edge._outward._y == -1 )
            bTop = edge._start._x == 0 && edge._start._y == 1 && edge._end._x == 3 && edge._end._y == 1;
        if ( edge._outward._x == 1 )
            bRight = edge._start._x == 3 && edge._start._y == 1 && edge._end._x == 3 && edge._end._y == 3;
    }
    SW_EXPECT_TRUE( bTop );
    SW_EXPECT_TRUE( bRight );

    // X X
    // X .   — 오목한 ㄱ 자: 위 · 왼 · 아래 둘 · 오른 둘 = 여섯.
    const sw::vector<uint8> listCorner = { 1, 1, 1, 0 };
    sw::TileGridUtil::traceSolidOutline( listCorner, 2, 2, listEdge );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( listEdge.size() ) );
}

/**
 * @brief [TileGridUtilTest] 이동 비용 격자는 NavGrid 와 같은 값이라 그대로 넣어 경로를 찾을 수 있다 — 벽(단단한 타일)을 돌아가고, 늪(비용 40)보다 풀(5)을 고른다
 */
SW_TEST_CASE( TileGridUtilTest, NavCostsDriveTheGridPathfinder )
{
    sw::TileSetAsset tileSet;
    SW_ASSERT_TRUE( tileSet.loadFromXmlText( "<TileSet columns=\"2\" rows=\"2\">"
                                             "  <Tile name=\"wall\" cell=\"0\" solid=\"true\"/>"
                                             "  <Tile name=\"swamp\" cell=\"1\" navCost=\"40\"/>"
                                             "  <Tile name=\"grass\" cell=\"2\" navCost=\"5\"/>"
                                             "</TileSet>",
                                             "<test>" ) );
    constexpr uint16 kWall  = 1;
    constexpr uint16 kSwamp = 2;
    constexpr uint16 kGrass = 3;
    // 5 × 3, 가운데 열은 벽(아래 줄만 열림). 위 줄은 늪, 아래 줄은 풀.
    const sw::vector<uint16> listBrush = { kSwamp, kSwamp, kWall, kSwamp, kSwamp, //
                                           0, 0, kWall, 0, 0,                     //
                                           kGrass, kGrass, kGrass, kGrass, kGrass };
    sw::vector<uint8>        listCost;
    sw::TileGridUtil::makeNavCosts( tileSet, listBrush, 10, listCost );
    SW_EXPECT_EQUAL( 255, static_cast<int32>( listCost[2] ) );
    SW_EXPECT_EQUAL( 40, static_cast<int32>( listCost[0] ) );
    SW_EXPECT_EQUAL( 10, static_cast<int32>( listCost[5] ) );
    SW_EXPECT_EQUAL( 5, static_cast<int32>( listCost[10] ) );

    sw::NavGrid grid;
    grid.initialize( 5, 3, 1.0f, sw::float3{ 0.0f, 0.0f, 0.0f } );
    for ( int32 y = 0; y < 3; ++y )
    {
        for ( int32 x = 0; x < 5; ++x )
            grid.setCost( x, y, listCost[static_cast<size_t>( y * 5 + x )] );
    }
    sw::GridPathfinder pathfinder;
    sw::GridPathQuery  query{};
    query._start   = sw::int2{ 0, 1 };
    query._goal    = sw::int2{ 4, 1 };
    query._bSmooth = SW_FALSE;
    sw::vector<sw::int2> listCell;
    SW_ASSERT_TRUE( pathfinder.findPath( grid, query, listCell ) == sw::GridPathResult::Found );
    bool bCrossesWall   = false;
    bool bUsesBottomRow = false;
    for ( const sw::int2& cell : listCell )
    {
        bCrossesWall   = bCrossesWall || ( cell._x == 2 && cell._y != 2 );
        bUsesBottomRow = bUsesBottomRow || ( cell._x == 2 && cell._y == 2 );
    }
    SW_EXPECT_FALSE( bCrossesWall );
    SW_EXPECT_TRUE( bUsesBottomRow );
}
