#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Framework/GameStateRefs.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Kits/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Strategy/CityBuilder/CitySimulation.h"

#include "TestFramework/TestFramework.h"

// 도시 건설 키트(파라오 장르) — 카탈로그, 짓기 규칙과 도로 입구, 순회 일꾼의 서비스, 농장 → 수레 → 창고 → 시장 → 상인 → 집 사슬, 집 진화 · 퇴화,
// 노동 · 이민, 달 끝 세금 · 임금 · 소비와 해마다의 범람.

using namespace sw;

namespace
{
    constexpr const utf8* kCityTestXml = R"(
<CityCatalog roadCost="1">
  <Good id="grain" name="Grain" food="true"/>
  <Building id="house" kind="House" size="1" cost="5"/>
  <Building id="well" kind="Service" service="Water" delivery="Radius" range="3" size="1" cost="10"/>
  <Building id="shrine" kind="Service" service="Religion" range="30" walkerInterval="2" workers="2" size="1" cost="20" desirability="3" desirabilityRadius="2"/>
  <Building id="tax" kind="Service" service="Tax" range="30" walkerInterval="2" workers="2" size="1" cost="20"/>
  <Building id="farm" kind="Producer" goods="grain" productionTime="4" amount="8" terrain="Floodplain" workers="4" size="2" cost="20" desirability="-2" desirabilityRadius="1"/>
  <Building id="granary" kind="Storage" goods="grain" capacity="100" workers="2" size="2" cost="30"/>
  <Building id="bazaar" kind="Market" goods="grain" capacity="40" range="30" walkerInterval="2" workers="2" size="1" cost="20"/>
  <Building id="garden" kind="Decoration" desirability="5" desirabilityRadius="3" cost="5"/>
  <Building id="mystery" kind="Spaceport"/>
  <HouseLevel name="Hut" population="4" tax="1"/>
  <HouseLevel name="Shack" population="6" services="Water" tax="1"/>
  <HouseLevel name="Cottage" population="8" services="Water,Religion" goods="grain" tax="2"/>
  <HouseLevel name="Villa" population="12" services="Water,Religion" goods="grain" desirability="10" tax="3"/>
</CityCatalog>
)";

    struct CityTestScene
    {
        CityCatalog    _catalog;
        CitySimulation _city;
        Wallet         _wallet; ///< 도시가 빌린 금고

        bool initialize( int32 money = 5000, float32 wagePerWorkerPerMonth = 0.5f )
        {
            if ( _catalog.loadFromXmlText( kCityTestXml, "CityBuilderTest" ) == false )
                return false;
            CitySettings settings;
            settings._secondsPerMonth       = 20.0f;
            settings._wagePerWorkerPerMonth = wagePerWorkerPerMonth;
            _wallet.clear();
            _wallet.add( settings._currency, money );
            GameStateRefs refs;
            refs._pWallet = &_wallet;
            _city.initialize( &_catalog, 32, 24, settings, refs );
            return true;
        }

        int32 getMoney() const { return static_cast<int32>( _wallet.getBalance( _city.getCurrency() ) ); }

        /** @brief y = 10 의 동서 도로(x 2..29)와 x = 2 · 29 의 남북 도로(y 3..20) — 고리 하나. */
        void buildRoadLoop()
        {
            (void)_city.placeRoadLine( int2{ 2, 10 }, int2{ 29, 10 } );
            (void)_city.placeRoadLine( int2{ 2, 3 }, int2{ 2, 20 } );
            (void)_city.placeRoadLine( int2{ 29, 3 }, int2{ 29, 20 } );
        }

        void run( float32 seconds )
        {
            for ( float32 elapsed = 0.0f; elapsed < seconds; elapsed += 0.25f )
                _city.update( 0.25f );
        }
    };
} // namespace

/**
 * @brief [CityBuilderTest] 카탈로그 — 물자 · 건물 · 집 단계를 읽고 모르는 종류는 경고하고 장식으로 둔다
 */
SW_TEST_CASE( CityBuilderTest, CatalogReadsBuildingsGoodsAndHouseLevels )
{
    CityCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kCityTestXml, "CityBuilderTest" ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 9 ), catalog.getBuildings().size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), catalog.getHouseLevels().size() );
    SW_EXPECT_EQUAL( 1, catalog.getRoadCost() );
    const CityBuildingDef* pFarm = catalog.findBuilding( "farm" );
    SW_ASSERT_NOT_NULL( pFarm );
    SW_EXPECT_TRUE( pFarm->_kind == CityBuildingKind::Producer );
    SW_EXPECT_TRUE( pFarm->_bRequiresTerrain == SW_TRUE && pFarm->_requiredTerrain == CityTerrain::Floodplain );
    SW_EXPECT_TRUE( pFarm->_listGood.size() == 1 && pFarm->_listGood[0] == hashed_string( "grain" ) );
    SW_EXPECT_TRUE( catalog.findBuilding( "well" )->_delivery == CityDelivery::Radius );
    SW_EXPECT_TRUE( catalog.findBuilding( "mystery" )->_kind == CityBuildingKind::Decoration );
    SW_EXPECT_TRUE( catalog.findHouseBuilding() == catalog.findBuilding( "house" ) );
    const CityHouseLevelDef* pCottage = catalog.findHouseLevel( 2 );
    SW_ASSERT_NOT_NULL( pCottage );
    SW_EXPECT_EQUAL( static_cast<int32>( makeCityServiceBit( CityService::Water ) | makeCityServiceBit( CityService::Religion ) ),
                     static_cast<int32>( pCottage->_serviceMask ) );
    SW_EXPECT_TRUE( pCottage->_listRequiredGood.size() == 1 );
}

/**
 * @brief [CityBuilderTest] 짓기 — 겹침 · 물 · 요구 땅 · 돈을 본다 · 길이 붙어야 입구가 생긴다 · 허물면 자리가 빈다
 */
SW_TEST_CASE( CityBuilderTest, PlacementChecksTerrainMoneyAndRoadAccess )
{
    CityTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 60 ) );
    CitySimulation& city = scene._city;
    city.fillTerrain( 0, 0, 31, 1, CityTerrain::Water );
    city.fillTerrain( 10, 2, 13, 5, CityTerrain::Floodplain );

    SW_EXPECT_TRUE( city.placeBuilding( "nothing", 5, 5 ) == CityPlaceResult::UnknownBuilding );
    SW_EXPECT_TRUE( city.placeBuilding( "house", 5, 0 ) == CityPlaceResult::BadTerrain );
    SW_EXPECT_TRUE( city.placeBuilding( "farm", 5, 5 ) == CityPlaceResult::BadTerrain ); // 범람원이 아니다
    SW_EXPECT_TRUE( city.placeBuilding( "farm", 31, 23 ) == CityPlaceResult::OutOfBounds );
    SW_EXPECT_TRUE( city.placeBuilding( "farm", 10, 2 ) == CityPlaceResult::Ok );
    SW_EXPECT_TRUE( city.placeBuilding( "house", 11, 3 ) == CityPlaceResult::Occupied );
    SW_EXPECT_EQUAL( 40, scene.getMoney() );
    SW_EXPECT_FALSE( city.findBuildingAt( 10, 2 )->hasRoadAccess() );

    // 농장은 (10..11, 2..3). 도로는 건물 위에 못 깔고, 한 칸 띄운 도로는 입구가 아니다.
    SW_EXPECT_TRUE( city.placeRoad( 11, 3 ) == CityPlaceResult::Occupied );
    SW_EXPECT_TRUE( city.placeRoad( 10, 5 ) == CityPlaceResult::Ok );
    city.update( 0.25f ); // 도로가 바뀐 것은 다음 걸음에 반영된다
    SW_EXPECT_FALSE( city.findBuildingAt( 10, 2 )->hasRoadAccess() );
    SW_EXPECT_TRUE( city.placeRoad( 10, 4 ) == CityPlaceResult::Ok );
    city.update( 0.25f );
    SW_ASSERT_TRUE( city.findBuildingAt( 10, 2 )->hasRoadAccess() );
    SW_EXPECT_TRUE( city.findBuildingAt( 10, 2 )->_accessTile == ( int2{ 10, 4 } ) );

    SW_EXPECT_EQUAL( 38, scene.getMoney() ); // 도로 둘(1 씩)
    SW_EXPECT_TRUE( city.placeBuilding( "granary", 20, 20 ) == CityPlaceResult::Ok );
    SW_EXPECT_TRUE( city.placeBuilding( "granary", 24, 20 ) == CityPlaceResult::NotEnoughMoney ); // 8 남았다
    SW_EXPECT_TRUE( city.demolish( 11, 3 ) );
    SW_EXPECT_TRUE( city.findBuildingAt( 10, 2 ) == nullptr );
    SW_EXPECT_TRUE( city.placeBuilding( "house", 10, 2 ) == CityPlaceResult::Ok ); // 땅 제약은 농장만 — 집은 범람원에도
    SW_EXPECT_TRUE( city.demolish( 10, 5 ) );                                      // 도로도 허문다
    SW_EXPECT_FALSE( city.demolish( 25, 15 ) );                                    // 빈 땅
}

/**
 * @brief [CityBuilderTest] 우물은 반경 안의 집에 물을 · 신전 일꾼은 도로를 돌며 두 칸 안의 집에만 종교를 준다 · 일꾼은 걸음을 다 쓰면 돌아와 다시 나간다
 */
SW_TEST_CASE( CityBuilderTest, ServiceWalkersRoamRoadsAndServeNearbyHouses )
{
    CityTestScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    CitySimulation& city = scene._city;
    scene.buildRoadLoop();
    SW_ASSERT_TRUE( city.placeBuilding( "house", 6, 11 ) == CityPlaceResult::Ok );  // 길 바로 옆
    SW_ASSERT_TRUE( city.placeBuilding( "house", 20, 12 ) == CityPlaceResult::Ok ); // 두 칸
    SW_ASSERT_TRUE( city.placeBuilding( "house", 15, 20 ) == CityPlaceResult::Ok ); // 길에서 멀다(입구 없음 — 사람이 오지 않는다)
    SW_ASSERT_TRUE( city.placeBuilding( "well", 7, 12 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "shrine", 4, 9 ) == CityPlaceResult::Ok );
    scene.run( 20.0f );

    const CityBuilding* pNear = city.findBuildingAt( 6, 11 );
    const CityBuilding* pTwo  = city.findBuildingAt( 20, 12 );
    const CityBuilding* pFar  = city.findBuildingAt( 15, 20 );
    SW_ASSERT_TRUE( pNear != nullptr && pTwo != nullptr && pFar != nullptr );
    SW_EXPECT_TRUE( pNear->_population > 0 );
    SW_EXPECT_EQUAL( 0, pFar->_population );
    SW_EXPECT_TRUE( city.isHouseServed( *pNear, CityService::Water ) );
    SW_EXPECT_FALSE( city.isHouseServed( *pTwo, CityService::Water ) ); // 우물 반경 3 밖
    SW_EXPECT_TRUE( city.isHouseServed( *pNear, CityService::Religion ) );
    SW_EXPECT_TRUE( city.isHouseServed( *pTwo, CityService::Religion ) );
    SW_EXPECT_TRUE( pNear->_level >= 1 ); // 물을 받아 Shack 으로

    // 일꾼은 하나씩 — 돌아다니다 사라지고 다시 나온다(늘 둘 이상이 되지 않는다).
    const CityBuilding* pShrine      = city.findBuildingAt( 4, 9 );
    int32               maxWalkers   = 0;
    bool                bSawNoWalker = false;
    for ( int32 stepIndex = 0; stepIndex < 400; ++stepIndex )
    {
        city.update( 0.25f );
        maxWalkers   = MathUtil::max( maxWalkers, pShrine->_activeWalkerCount );
        bSawNoWalker = bSawNoWalker || pShrine->_activeWalkerCount == 0;
    }
    SW_EXPECT_EQUAL( 1, maxWalkers );
    SW_EXPECT_TRUE( bSawNoWalker );
    bool bWalkersOnRoad = true;
    for ( const CityWalker& walker : city.getWalkers() )
        bWalkersOnRoad = bWalkersOnRoad && city.findTile( walker._tile._x, walker._tile._y )->_bRoad == SW_TRUE;
    SW_EXPECT_TRUE( bWalkersOnRoad );
}

/**
 * @brief [CityBuilderTest] 범람원 농장 → 수레 → 곡물 창고 → 바자 → 상인 → 집 곡물 — 물 · 종교 · 곡물을 다 받은 집은 Cottage 로 · 우물을 허물면 내려간다
 */
SW_TEST_CASE( CityBuilderTest, FoodChainFeedsHousesAndHousesEvolveAndDevolve )
{
    CityTestScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    CitySimulation& city = scene._city;
    city.fillTerrain( 12, 11, 15, 14, CityTerrain::Floodplain );
    scene.buildRoadLoop();
    SW_ASSERT_TRUE( city.placeBuilding( "farm", 12, 11 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "granary", 20, 8 ) == CityPlaceResult::Ok ); // (20..21, 8..9) — 아래가 도로
    SW_ASSERT_TRUE( city.placeBuilding( "bazaar", 8, 9 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "shrine", 9, 9 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "well", 6, 12 ) == CityPlaceResult::Ok );
    for ( int32 x = 3; x <= 7; ++x )
        SW_ASSERT_TRUE( city.placeBuilding( "house", x, 11 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "house", 3, 12 ) == CityPlaceResult::Ok );

    bool              bDelivered = false;
    vector<CityEvent> listEvent;
    for ( int32 stepIndex = 0; stepIndex < 320; ++stepIndex ) // 80 초
    {
        city.update( 0.25f );
        listEvent.clear();
        city.drainEvents( listEvent );
        for ( const CityEvent& event : listEvent )
            bDelivered = bDelivered || event._kind == CityEvent::Kind::GoodsDelivered;
    }
    SW_EXPECT_TRUE( bDelivered );
    SW_EXPECT_TRUE( city.getWorkforce() > 0 );
    const CityBuilding* pHouse = city.findBuildingAt( 4, 11 );
    SW_ASSERT_NOT_NULL( pHouse );
    SW_EXPECT_TRUE( pHouse->_stock.getItemCount( "grain" ) > 0 );
    SW_EXPECT_EQUAL( 2, pHouse->_level );
    SW_EXPECT_TRUE( city.computeAverageHouseLevel() > 1.0f );

    // 매력도 — 농장 옆은 낮다(Villa 10 이 필요한데 정원 없이는 못 오른다).
    SW_EXPECT_TRUE( city.findTile( 4, 11 )->_desirability < 10 );

    // 우물을 허물면 물을 잃어 Hut 쪽으로 내려간다(서비스 효과 30 초 + 퇴화 3 초).
    SW_ASSERT_TRUE( city.demolish( 6, 12 ) );
    scene.run( 40.0f );
    SW_EXPECT_EQUAL( 0, city.findBuildingAt( 4, 11 )->_level );
}

/**
 * @brief [CityBuilderTest] 달 끝 — 세리가 다녀간 집만 세금을 내고 일꾼 임금이 나간다 · 해가 바뀌면 범람이 비옥함을 정한다
 */
SW_TEST_CASE( CityBuilderTest, MonthEndCollectsTaxesPaysWagesAndYearFloods )
{
    CityTestScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    CitySimulation& city = scene._city;
    scene.buildRoadLoop();
    for ( int32 x = 5; x <= 9; ++x )
        SW_ASSERT_TRUE( city.placeBuilding( "house", x, 11 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "tax", 4, 9 ) == CityPlaceResult::Ok );
    const int32 moneyAfterBuilding = scene.getMoney();

    vector<CityEvent> listEvent;
    int32             monthIncome = 0;
    int32             floodCount  = 0;
    int32             floodValue  = 0;
    for ( int32 stepIndex = 0; stepIndex < 12 * 80 + 4; ++stepIndex ) // 한 해 하고 조금
    {
        city.update( 0.25f );
        listEvent.clear();
        city.drainEvents( listEvent );
        for ( const CityEvent& event : listEvent )
        {
            if ( event._kind == CityEvent::Kind::MonthEnded )
                monthIncome += event._value;
            if ( event._kind == CityEvent::Kind::Flood )
            {
                ++floodCount;
                floodValue = event._value;
            }
        }
    }
    SW_EXPECT_TRUE( monthIncome > 0 );
    SW_EXPECT_EQUAL( moneyAfterBuilding + monthIncome, scene.getMoney() );
    SW_EXPECT_EQUAL( 1, floodCount );
    SW_EXPECT_TRUE( floodValue >= 40 && floodValue <= 100 );
    SW_EXPECT_EQUAL( 2, city.getYear() );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( floodValue ) / 100.0f, city.getFloodFertility(), 0.011f );

    // 세리를 허물면 세금이 그치고 임금만 남는다(일꾼 없음 → 0).
    SW_ASSERT_TRUE( city.demolish( 4, 9 ) );
    scene.run( 60.0f ); // 세금 효과(30 초)가 다 빠지게
    const int32 before = scene.getMoney();
    scene.run( 20.0f );
    SW_EXPECT_TRUE( scene.getMoney() <= before );
}

/**
 * @brief [CityBuilderTest] 달 결산의 임금은 미룰 수 없다 — 빈 금고에서는 빌린 지갑이 빚을 지고, 빚이 있는 동안 짓기가 거절된다
 */
SW_TEST_CASE( CityBuilderTest, MonthlyWagesCanRunTheBorrowedWalletIntoDebt )
{
    CityTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 5000, 50.0f ) ); // 일꾼 하나에 달마다 50 — 세금으로는 못 메운다
    CitySimulation& city = scene._city;
    scene.buildRoadLoop();
    for ( int32 x = 5; x <= 9; ++x )
        SW_ASSERT_TRUE( city.placeBuilding( "house", x, 11 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "tax", 4, 9 ) == CityPlaceResult::Ok );
    scene._wallet.setBalance( city.getCurrency(), 0 );
    for ( int32 month = 0; month < 3 && scene.getMoney() >= 0; ++month )
        scene.run( 20.0f );
    SW_ASSERT_TRUE( city.getEmployed() > 0 );
    SW_EXPECT_TRUE( scene.getMoney() < 0 );
    SW_EXPECT_TRUE( city.placeRoad( 3, 3 ) == CityPlaceResult::NotEnoughMoney );
}

/**
 * @brief [CityBuilderTest] 도시 상태를 쓰고 같은 카탈로그 · 크기로 시작한 새 시뮬레이션에 읽으면 바이트가 같고, 같은 시간을 더 돌려도 같다
 * @details 핫 리로드 · 세이브가 디렉터의 도시를 이 바이트로 옮긴다. 더 돌린 뒤까지 같아야 숨은 상태(일꾼 길 · 난수 · 걸음 타이머)도 옮겨졌다.
 *          크기가 다르거나 잘린 바이트는 거절하고 그대로 둔다.
 */
SW_TEST_CASE( CityBuilderTest, StateRoundTripContinuesTheSameCity )
{
    CityTestScene original;
    SW_ASSERT_TRUE( original.initialize() );
    CitySimulation& city = original._city;
    city.fillTerrain( 12, 11, 15, 14, CityTerrain::Floodplain );
    original.buildRoadLoop();
    SW_ASSERT_TRUE( city.placeBuilding( "farm", 12, 11 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "granary", 20, 8 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "bazaar", 8, 9 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "shrine", 9, 9 ) == CityPlaceResult::Ok );
    SW_ASSERT_TRUE( city.placeBuilding( "well", 6, 12 ) == CityPlaceResult::Ok );
    for ( int32 x = 3; x <= 7; ++x )
        SW_ASSERT_TRUE( city.placeBuilding( "house", x, 11 ) == CityPlaceResult::Ok );
    original.run( 37.3f );
    SW_ASSERT_TRUE( city.getWalkers().empty() == false );

    Archive written;
    city.writeState( written );

    CityTestScene restored;
    SW_ASSERT_TRUE( restored.initialize( 1 ) );
    Archive reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( restored._city.readState( reader ) );
    SW_EXPECT_EQUAL( uint64( 0 ), reader.getRemainingBytes() );
    SW_EXPECT_EQUAL( city.getPopulation(), restored._city.getPopulation() );
    SW_EXPECT_EQUAL( city.getWalkers().size(), restored._city.getWalkers().size() );

    Archive rewritten;
    restored._city.writeState( rewritten );
    SW_ASSERT_EQUAL( written.getSize(), rewritten.getSize() );
    SW_EXPECT_TRUE( Memory::compare( written.getData(), rewritten.getData(), written.getSize() ) == 0 );

    original.run( 41.0f );
    restored.run( 41.0f );
    Archive laterOriginal;
    Archive laterRestored;
    city.writeState( laterOriginal );
    restored._city.writeState( laterRestored );
    SW_ASSERT_EQUAL( laterOriginal.getSize(), laterRestored.getSize() );
    SW_EXPECT_TRUE( Memory::compare( laterOriginal.getData(), laterRestored.getData(), laterOriginal.getSize() ) == 0 );
    SW_EXPECT_EQUAL( city.computeAverageHouseLevel(), restored._city.computeAverageHouseLevel() );

    BLOCK( "크기가 다른 도시 · 잘린 바이트는 거절하고 그대로 둔다" )
    {
        CityCatalog    catalog;
        CitySimulation smallCity;
        SW_ASSERT_TRUE( catalog.loadFromXmlText( kCityTestXml, "CityBuilderTest" ) );
        smallCity.initialize( &catalog, 8, 8, CitySettings{}, GameStateRefs{} );
        Archive smallReader( written.getData(), written.getSize() );
        SW_EXPECT_FALSE( smallCity.readState( smallReader ) );
        SW_EXPECT_TRUE( smallCity.getBuildings().empty() );

        CityTestScene cutScene;
        SW_ASSERT_TRUE( cutScene.initialize( 55 ) );
        Archive cut( written.getData(), written.getSize() - 3 );
        SW_EXPECT_FALSE( cutScene._city.readState( cut ) );
        SW_EXPECT_EQUAL( 55, cutScene.getMoney() );
        SW_EXPECT_TRUE( cutScene._city.getBuildings().empty() );
    }
}
