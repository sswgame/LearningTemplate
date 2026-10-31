// TileNavCostTest — 타일 격자의 이동 비용(`TileGridUtil::makeNavCosts`)이 NavGrid 와 같은 값이라 그대로 넣어 경로를 찾는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/TileMap/TileGridUtil.h"
#include "Engine/TileMap/TileSetAsset.h"

#include "GameFramework/Base/Character/Navigation/GridPathfinder.h"
#include "GameFramework/Base/Character/Navigation/NavGrid.h"

/**
 * @brief [TileNavCostTest] 이동 비용 격자는 NavGrid 와 같은 값이라 그대로 넣어 경로를 찾을 수 있다 — 벽(단단한 타일)을 돌아가고, 늪(비용 40)보다 풀(5)을 고른다
 */
SW_TEST_CASE( TileNavCostTest, NavCostsDriveTheGridPathfinder )
{
    sw::TileSetAsset tileSet;
    SW_REQUIRE_TRUE( tileSet.loadFromXMLText( "<TileSet columns=\"2\" rows=\"2\">"
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
    SW_REQUIRE_TRUE( grid.initialize( 5, 3, 1.0f, sw::float3{ 0.0f, 0.0f, 0.0f } ) );
    for ( int32 y = 0; y < 3; ++y )
    {
        for ( int32 x = 0; x < 5; ++x )
        {
            grid.setCost( x, y, listCost[static_cast<size_t>( y * 5 + x )] );
        }
    }
    sw::GridPathfinder pathfinder;
    sw::GridPathQuery  query{};
    query._start   = sw::int2{ 0, 1 };
    query._goal    = sw::int2{ 4, 1 };
    query._bSmooth = SW_FALSE;
    sw::vector<sw::int2> listCell;
    SW_REQUIRE_TRUE( pathfinder.findPath( grid, query, listCell ) == sw::GridPathResult::Found );
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
