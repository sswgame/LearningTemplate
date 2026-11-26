// NileCityPlannerTest — NileCity 의 땅과 자동 계획 진행(nogpu): 타일맵의 땅 칠하기와 모자란 데이터의 거절, 빈 순서표의 진행,
// 진행 상태 바이트의 왕복과 순서표 밖 단계 거절. 순서표 파일(`reset`)은 에셋을 읽어 여기서 다루지 않는다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/TileMap/TileMapXML.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

#include "Games/NileCity/NileCityPlanner.h"

using namespace sw;

namespace
{
    constexpr const utf8* kNileCityXML = R"(<CityCatalog roadCost="1">
  <Rules walkerSpeed="2.5" workerRatio="0.4" serviceDuration="30" immigrationInterval="1.5" immigrationJobMargin="4" immigrationMaxIdleRatio="0.25" floodFertilityStart="0.8" floodFertilityMin="0.4" floodFertilityMax="1" evolveDelay="3" wagePerWorkerPerMonth="0.5" fixedStep="0.25" serviceReach="2" goodsPerFourPeople="1" randomSeed="3100" currency="Deben"/>
  <Terrain id="Grass" name="Grass" color="1,1,1,1"/>
  <Terrain id="Sand" name="Sand" color="1,1,1,1"/>
  <Terrain id="Floodplain" name="Floodplain" color="1,1,1,1" flood="true"/>
  <Terrain id="Water" name="Water" color="1,1,1,1" blocked="true"/>
  <Terrain id="Rock" name="Rock" color="1,1,1,1" blocked="true"/>
  <Building modelTint="1,1,1,1" blockColor="0.5,0.4,0.3,1" id="hut" name="Hut" kind="House" size="1" cost="1"/>
  <HouseLevel name="Hut" population="5"/>
</CityCatalog>)";

    constexpr int32 kTestWidth     = 6;
    constexpr int32 kTestHeight    = 4;
    constexpr int32 kRoadCost      = 1;
    constexpr int32 kStartingMoney = 100;

    struct NileCityPlannerTest
    {
        CityCatalog    _catalog{};
        Wallet         _wallet{};
        CitySimulation _city{};
        TileMapXMLData _tileMap{};

        NileCityPlannerTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kNileCityXML, "NileCityPlannerTest" ) );
            _wallet.add( _catalog.getRules()._currency, kStartingMoney );
            SW_REQUIRE_TRUE( _city.initialize( &_catalog, kTestWidth, kTestHeight, _wallet ) );
            SW_REQUIRE_TRUE( _tileMap.resetTiles( kTestWidth, kTestHeight ) );
            SW_REQUIRE_TRUE( _tileMap.addDataLayer( NileCityPlanner::kTerrainLayer, 0 ) );
        }

        string findTerrain( int32 x, int32 y ) const { return string( _catalog.getTerrainRows()[_city.findTile( x, y )->_terrain]._id.view() ); }
    };
} // namespace

/**
 * @brief [NileCityPlannerTest] 칸 값은 목록의 번호다 — 같은 지도라도 목록의 순서를 바꾸면 땅이 바뀐다
 */
SW_TEST_CASE_F( NileCityPlannerTest, PaintTerrainFollowsTheMapLayer )
{
    SW_REQUIRE_TRUE( _tileMap.setCellData( 0, 2, 1, 1 ) );
    SW_REQUIRE_TRUE( _tileMap.setCellData( 0, 5, 3, 2 ) );
    SW_REQUIRE_TRUE( NileCityPlanner::paintTerrain( _city, _tileMap, { hashed_string( "Grass" ), hashed_string( "Water" ), hashed_string( "Rock" ) } ) );
    SW_EXPECT_EQUAL( string( "Grass" ), findTerrain( 0, 0 ) );
    SW_EXPECT_EQUAL( string( "Water" ), findTerrain( 2, 1 ) );
    SW_EXPECT_EQUAL( string( "Rock" ), findTerrain( 5, 3 ) );

    SW_REQUIRE_TRUE( NileCityPlanner::paintTerrain( _city, _tileMap, { hashed_string( "Sand" ), hashed_string( "Floodplain" ), hashed_string( "Grass" ) } ) );
    SW_EXPECT_EQUAL( string( "Sand" ), findTerrain( 0, 0 ) );
    SW_EXPECT_EQUAL( string( "Floodplain" ), findTerrain( 2, 1 ) );
    SW_EXPECT_EQUAL( string( "Grass" ), findTerrain( 5, 3 ) );
}

/**
 * @brief [NileCityPlannerTest] 레이어가 없거나 크기가 다른 지도, 카탈로그에 없는 땅, 목록 밖 칸 값은 오류이고 칠하지 않는다
 */
SW_TEST_CASE_F( NileCityPlannerTest, IncompleteMapDataIsRefused )
{
    SW_EXPECT_LOG( "NileCityPlanner.MapLayerMissing" );
    SW_EXPECT_LOG( "NileCityPlanner.UnknownTerrain" );
    SW_EXPECT_LOG( "NileCityPlanner.TerrainValueUnknown" );
    TileMapXMLData withoutLayer;
    SW_REQUIRE_TRUE( withoutLayer.resetTiles( kTestWidth, kTestHeight ) );
    SW_EXPECT_FALSE( NileCityPlanner::paintTerrain( _city, withoutLayer, { hashed_string( "Sand" ) } ) );
    TileMapXMLData otherSize;
    SW_REQUIRE_TRUE( otherSize.resetTiles( kTestWidth + 1, kTestHeight ) );
    SW_REQUIRE_TRUE( otherSize.addDataLayer( NileCityPlanner::kTerrainLayer, 0 ) );
    SW_EXPECT_FALSE( NileCityPlanner::paintTerrain( _city, otherSize, { hashed_string( "Sand" ) } ) );
    SW_EXPECT_FALSE( NileCityPlanner::paintTerrain( _city, _tileMap, { hashed_string( "Sand" ), hashed_string( "Lava" ) } ) );
    SW_REQUIRE_TRUE( _tileMap.setCellData( 0, 3, 2, 1 ) );
    SW_EXPECT_FALSE( NileCityPlanner::paintTerrain( _city, _tileMap, { hashed_string( "Sand" ) } ) );
    SW_EXPECT_EQUAL( string( "Grass" ), findTerrain( 0, 0 ) );
}

/**
 * @brief [NileCityPlannerTest] 빈 순서표는 처음부터 끝났고 진행해도 짓는 것이 없다. 진행 상태는 왕복하고, 순서표 밖 단계는 거절해 그대로 둔다
 */
SW_TEST_CASE_F( NileCityPlannerTest, ProgressStateRoundTripsAndRefusesStepsOutsideThePlan )
{
    NileCityPlanner planner;
    SW_EXPECT_TRUE( planner.isFinished() );
    SW_EXPECT_EQUAL( 0, planner.advance( _city, _wallet, kRoadCost ) );
    SW_EXPECT_EQUAL( 0, planner.getStepCount() );

    Archive written;
    planner.writeState( written );
    NileCityPlanner restored;
    Archive         reading( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 0, restored.getNextStep() );

    // 다음 단계 3 · 건너뛴 수 0 — 빈 순서표에는 없는 단계다. 머리(판 · 칸 배치 해시)는 쓴 바이트에서 진행 칸 둘을 뺀 앞부분 그대로 둔다.
    const uint64 headerSize = written.getSize() - sizeof( int32 ) * 2;
    Archive      invalidWritten;
    invalidWritten.writeBytes( written.getData(), headerSize );
    invalidWritten << int32{ 3 } << int32{ 0 };
    Archive invalidReading{ invalidWritten.getData(), invalidWritten.getSize() };
    SW_EXPECT_FALSE( restored.readState( invalidReading ) );
    SW_EXPECT_EQUAL( 0, restored.getNextStep() );
    SW_EXPECT_EQUAL( 0, restored.getSkippedCount() );
}
