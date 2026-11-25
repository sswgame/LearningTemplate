// NileCityPlannerTest — NileCity 의 땅과 자동 계획 진행(nogpu): 강 곡선, 강 · 범람원 · 풀 · 사막 띠의 칠, 빈 순서표의 진행,
// 진행 상태 바이트의 왕복과 순서표 밖 단계 거절. 순서표 파일(`reset`)은 에셋을 읽어 여기서 다루지 않는다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

#include "Games/NileCity/NileCityPlanner.h"

using namespace sw;

namespace
{
    constexpr const utf8* kNileCityXML = R"(<CityCatalog roadCost="1">
  <Rules walkerSpeed="2.5" workerRatio="0.4" serviceDuration="30" immigrationInterval="1.5" immigrationJobMargin="4" immigrationMaxIdleRatio="0.25" floodFertilityStart="0.8" floodFertilityMin="0.4" floodFertilityMax="1" evolveDelay="3" wagePerWorkerPerMonth="0.5" fixedStep="0.25" serviceReach="2" goodsPerFourPeople="1" randomSeed="3100" currency="Deben"/>
  <Building id="hut" name="Hut" kind="House" size="1" cost="1"/>
  <HouseLevel name="Hut" population="5"/>
</CityCatalog>)";

    constexpr int32   kTestWidth     = 40;
    constexpr int32   kTestHeight    = 4;
    constexpr float32 kTolerance     = 1.0e-4f;
    constexpr int32   kRoadCost      = 1;
    constexpr int32   kStartingMoney = 100;

    /** @brief 굽이 · 모래 언덕이 없는 곧은 강입니다: 강 가운데 x 10, 반폭 2, 동쪽 범람원 3, 서쪽 범람원 2, 사막 x 30 부터, 바위 능선 없음. */
    NileCityLayout makeStraightLayout()
    {
        NileCityLayout layout;
        layout._riverCenterX   = 10.0f;
        layout._riverHalfWidth = 2.0f;
        layout._eastFloodWidth = 3.0f;
        layout._westFloodWidth = 2.0f;
        layout._desertStartX   = 30;
        layout._rockRidgeX     = 1000;
        return layout;
    }

    struct NileCityPlannerTest
    {
        CityCatalog    _catalog{};
        Wallet         _wallet{};
        CitySimulation _city{};

        NileCityPlannerTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kNileCityXML, "NileCityPlannerTest" ) );
            _wallet.add( _catalog.getRules()._currency, kStartingMoney );
            SW_REQUIRE_TRUE( _city.initialize( &_catalog, kTestWidth, kTestHeight, _wallet ) );
        }

        CityTerrain findTerrain( int32 x, int32 y ) const { return _city.findTile( x, y )->_terrain; }
    };
} // namespace

/**
 * @brief [NileCityPlannerTest] 강 가운데는 평균 x 에서 굽이 폭만큼 사인 곡선으로 흔들린다(y 0 은 평균, 위상 π/2 는 평균 + 굽이 폭)
 */
SW_TEST_CASE( NileCityPlannerTest, RiverCenterFollowsTheMeander )
{
    NileCityLayout layout    = makeStraightLayout();
    layout._riverMeander     = 3.0f;
    layout._riverMeanderRate = MathUtil::kHalfPi;
    SW_EXPECT_NEAR( 10.0f, NileCityPlanner::computeRiverCenter( layout, 0 ), kTolerance );
    SW_EXPECT_NEAR( 13.0f, NileCityPlanner::computeRiverCenter( layout, 1 ), kTolerance );
    SW_EXPECT_NEAR( 7.0f, NileCityPlanner::computeRiverCenter( layout, 3 ), kTolerance );
}

/**
 * @brief [NileCityPlannerTest] 곧은 강의 칠: 서쪽 사막 · 서쪽 범람원 · 강 · 동쪽 범람원 · 풀 · 동쪽 사막 띠가 칸 가운데 기준으로 나뉜다
 */
SW_TEST_CASE_F( NileCityPlannerTest, PaintTerrainLaysTheBands )
{
    NileCityPlanner::paintTerrain( _city, makeStraightLayout() );
    constexpr int32 kRow = 1;                                             // 흩어진 바위 무늬((x*7 + y*3) % 13 == 0)가 아래 동쪽 사막 칸에 걸리지 않는 줄
    SW_EXPECT_TRUE( findTerrain( 5, kRow ) == CityTerrain::Sand );        // 강 서쪽 4.5 칸 — 범람원(4) 밖은 사막
    SW_EXPECT_TRUE( findTerrain( 7, kRow ) == CityTerrain::Floodplain );  // 서쪽 2.5 칸
    SW_EXPECT_TRUE( findTerrain( 9, kRow ) == CityTerrain::Water );       // 서쪽 0.5 칸
    SW_EXPECT_TRUE( findTerrain( 10, kRow ) == CityTerrain::Water );      // 동쪽 0.5 칸
    SW_EXPECT_TRUE( findTerrain( 14, kRow ) == CityTerrain::Floodplain ); // 동쪽 4.5 칸 — 반폭 2 + 범람원 3 안
    SW_EXPECT_TRUE( findTerrain( 15, kRow ) == CityTerrain::Grass );      // 동쪽 5.5 칸
    SW_EXPECT_TRUE( findTerrain( 29, kRow ) == CityTerrain::Grass );      // 사막 경계 30 앞
    SW_EXPECT_TRUE( findTerrain( 31, kRow ) == CityTerrain::Sand );       // 사막 경계 뒤
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
