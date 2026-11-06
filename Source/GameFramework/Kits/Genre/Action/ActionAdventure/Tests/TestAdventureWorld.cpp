#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureVitals.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureWeaponWear.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureWorldMap.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

// AdventureWorldMapTest — 탑과 사당: 탑만 깨우고 지역을 드러냄, 사당 찾기 · 마치기와 증표, 증표 교환 경계, 표지 id 겹침 거절(결함).
// AdventureWeaponWearTest — 무기 내구도: 부서지는 한 방의 배율, 경고 구간에 들어서는 때 한 번, 던지기, 닳지 않는 무기, 슬롯 범위. 키트 GF_ActionAdventure(nogpu).

namespace
{
    constexpr const utf8* kWorldXML = R"(<AdventureWorld orbsPerExchange="2">
  <Tower id="tower" region="Plateau"/>
  <Tower id="farTower" region="Nowhere"/>
  <Shrine id="shrineA" region="Plateau"/>
  <Shrine id="shrineB"/>
  <Shrine id="shrineC"/>
</AdventureWorld>)";

    constexpr const utf8* kWearItemXML = R"(<ItemCatalog>
  <Item id="sword" durability="4"/>
  <Item id="rock"/>
</ItemCatalog>)";

    const sw::hashed_string kTower{ "tower" };
    const sw::hashed_string kFarTower{ "farTower" };
    const sw::hashed_string kShrineA{ "shrineA" };
    const sw::hashed_string kShrineB{ "shrineB" };
    const sw::hashed_string kShrineC{ "shrineC" };
    const sw::hashed_string kSword{ "sword" };
    const sw::hashed_string kRock{ "rock" };

    constexpr float32 kWearTolerance = 1.0e-4f;

    struct AdventureWorldTestUtil
    {
        static bool addArea( sw::AreaGraph& graph, const utf8* pID, const utf8* pRegion )
        {
            sw::AreaDef area;
            area._id     = sw::hashed_string( pID );
            area._region = sw::hashed_string( pRegion );
            return graph.addArea( area );
        }
    };
} // namespace

/**
 * @brief [AdventureWorldMapTest] 탑을 깨우면 그 지역의 새 방 수를 돌려주고 순간 이동 지점이 된다. 다시 깨운 탑 · 사당 · 모르는 id 는 −1 이고, 드러낼 방이 없는 탑은 0 이다
 */
SW_TEST_CASE( AdventureWorldMapTest, TowersRevealTheirRegionOnce )
{
    sw::AdventureWorldMap world;
    SW_REQUIRE_TRUE( world.loadFromXMLText( kWorldXML, "AdventureWorldMapTest" ) );
    SW_EXPECT_EQUAL( 2, world.getOrbsPerExchange() );
    sw::AreaGraph graph;
    SW_REQUIRE_TRUE( AdventureWorldTestUtil::addArea( graph, "a", "Plateau" ) );
    SW_REQUIRE_TRUE( AdventureWorldTestUtil::addArea( graph, "b", "Plateau" ) );
    SW_REQUIRE_TRUE( AdventureWorldTestUtil::addArea( graph, "c", "Other" ) );

    SW_EXPECT_FALSE( world.canWarpTo( kTower ) );
    SW_EXPECT_EQUAL( 2, world.activateTower( kTower, graph ) );
    SW_EXPECT_TRUE( graph.isDiscovered( sw::hashed_string( "a" ) ) );
    SW_EXPECT_FALSE( graph.isDiscovered( sw::hashed_string( "c" ) ) );
    SW_EXPECT_TRUE( world.canWarpTo( kTower ) );

    SW_EXPECT_EQUAL( -1, world.activateTower( kTower, graph ) );
    SW_EXPECT_EQUAL( -1, world.activateTower( kShrineA, graph ) );
    SW_EXPECT_EQUAL( -1, world.activateTower( sw::hashed_string( "nothing" ), graph ) );
    SW_EXPECT_EQUAL( 0, world.activateTower( kFarTower, graph ) );
    SW_EXPECT_TRUE( world.canWarpTo( kFarTower ) );
}

/**
 * @brief [AdventureWorldMapTest] 사당은 찾기와 마치기가 각각 처음에만 참이고, 마치기는 찾은 것으로 치며 증표를 한 번만 준다. 탑은 사당처럼 다루지 못한다
 */
SW_TEST_CASE( AdventureWorldMapTest, ShrinesGiveOneOrbEach )
{
    sw::AdventureWorldMap world;
    SW_REQUIRE_TRUE( world.loadFromXMLText( kWorldXML, "AdventureWorldMapTest" ) );

    SW_EXPECT_TRUE( world.discoverShrine( kShrineA ) );
    SW_EXPECT_FALSE( world.discoverShrine( kShrineA ) );
    SW_EXPECT_TRUE( world.canWarpTo( kShrineA ) );
    SW_EXPECT_FALSE( world.isCompleted( kShrineA ) );
    SW_EXPECT_EQUAL( 0, world.getOrbCount() );

    SW_EXPECT_TRUE( world.completeShrine( kShrineA ) );
    SW_EXPECT_FALSE( world.completeShrine( kShrineA ) );
    SW_EXPECT_EQUAL( 1, world.getOrbCount() );

    SW_EXPECT_TRUE( world.completeShrine( kShrineB ) ); // 찾지 않고 바로 마쳐도
    SW_EXPECT_TRUE( world.canWarpTo( kShrineB ) );
    SW_EXPECT_FALSE( world.discoverShrine( kShrineB ) );
    SW_EXPECT_EQUAL( 2, world.getCompletedShrineCount() );

    SW_EXPECT_FALSE( world.discoverShrine( kTower ) );
    SW_EXPECT_FALSE( world.completeShrine( kTower ) );
    SW_EXPECT_FALSE( world.completeShrine( sw::hashed_string( "nothing" ) ) );
    SW_EXPECT_FALSE( world.canWarpTo( sw::hashed_string( "nothing" ) ) );
    SW_EXPECT_FALSE( world.isCompleted( sw::hashed_string( "nothing" ) ) );

    world.resetState();
    SW_EXPECT_EQUAL( 0, world.getOrbCount() );
    SW_EXPECT_FALSE( world.canWarpTo( kShrineA ) );
    SW_EXPECT_TRUE( world.completeShrine( kShrineA ) );
}

/**
 * @brief [AdventureWorldMapTest] 증표 교환은 모자라면 그대로, 최대에 닿은 그릇이면 증표를 지키고, 되면 정해진 수만 쓴다
 */
SW_TEST_CASE( AdventureWorldMapTest, ExchangeKeepsOrbsWhenItCannotGive )
{
    sw::AdventureWorldMap world;
    SW_REQUIRE_TRUE( world.loadFromXMLText( kWorldXML, "AdventureWorldMapTest" ) );
    sw::AdventureVitalsSettings settings;
    settings._startHeartCount  = 1;
    settings._maxHeartCount    = 2;
    settings._staminaVessel    = 10.0f;
    settings._staminaVesselMax = 10.0f;
    sw::AdventureVitals vitals;
    SW_REQUIRE_TRUE( vitals.initialize( settings ) );

    SW_REQUIRE_TRUE( world.completeShrine( kShrineA ) );
    SW_EXPECT_TRUE( world.exchangeOrbs( sw::AdventureOrbReward::HeartContainer, vitals ) == sw::AdventureExchangeResult::NotEnoughOrbs );
    SW_EXPECT_EQUAL( 1, world.getOrbCount() );
    SW_EXPECT_EQUAL( 1, vitals.getHeartCount() );

    SW_REQUIRE_TRUE( world.completeShrine( kShrineB ) );
    SW_REQUIRE_TRUE( world.completeShrine( kShrineC ) );
    SW_EXPECT_TRUE( world.exchangeOrbs( sw::AdventureOrbReward::HeartContainer, vitals ) == sw::AdventureExchangeResult::Ok );
    SW_EXPECT_EQUAL( 2, vitals.getHeartCount() );
    SW_EXPECT_EQUAL( 1, world.getOrbCount() ); // 남는 하나는 그대로
    SW_EXPECT_EQUAL( 3, world.getCompletedShrineCount() );

    // 다시 모아 최대에 닿은 쪽과 아직 남은 쪽을 나눠 본다.
    sw::AdventureWorldMap second;
    SW_REQUIRE_TRUE( second.loadFromXMLText( kWorldXML, "AdventureWorldMapTest" ) );
    SW_REQUIRE_TRUE( second.completeShrine( kShrineA ) );
    SW_REQUIRE_TRUE( second.completeShrine( kShrineB ) );
    SW_EXPECT_TRUE( second.exchangeOrbs( sw::AdventureOrbReward::HeartContainer, vitals ) == sw::AdventureExchangeResult::AtLimit );
    SW_EXPECT_EQUAL( 2, second.getOrbCount() );
    SW_EXPECT_TRUE( second.exchangeOrbs( sw::AdventureOrbReward::StaminaVessel, vitals ) == sw::AdventureExchangeResult::Ok );
    SW_EXPECT_EQUAL( 0, second.getOrbCount() );
    SW_EXPECT_NEAR( 110.0f, vitals.getStamina().getMax(), kWearTolerance );
}

/**
 * @brief [AdventureWorldMapTest] 결함: 같은 id 의 사당이 탑을 말없이 덮어 그 탑이 사라졌다(읽는 순서에 좌우됨). 지역 없는 탑과 함께 그 표지만 거절하고 나머지는 읽는다
 */
SW_TEST_CASE( AdventureWorldMapTest, LandmarkDuplicatesAndTowersWithoutRegionAreRejected )
{
    SW_EXPECT_LOG( "AdventureWorldMap.DuplicateLandmark" );
    SW_EXPECT_LOG( "AdventureWorldMap.TowerNeedsRegionRegion" );
    SW_EXPECT_LOG( "AdventureWorldMap.SomeLandmarksNotRead" );

    sw::AdventureWorldMap world;
    SW_REQUIRE_TRUE( world.loadFromXMLText( R"(<AdventureWorld>
  <Tower id="same" region="Plateau"/>
  <Tower id="noRegion"/>
  <Shrine id="same"/>
  <Shrine id="fine"/>
</AdventureWorld>)",
                                            "duplicates" ) );
    const sw::AdventureLandmarkDef* pSame = world.findLandmark( sw::hashed_string( "same" ) );
    SW_REQUIRE_NOT_NULL( pSame );
    SW_EXPECT_TRUE( pSame->_kind == sw::AdventureLandmarkKind::Tower );
    SW_EXPECT_TRUE( world.findLandmark( sw::hashed_string( "noRegion" ) ) == nullptr );
    SW_EXPECT_TRUE( world.findLandmark( sw::hashed_string( "fine" ) ) != nullptr );
    SW_EXPECT_EQUAL( size_t( 2 ), world.getLandmarks().size() );
}

/**
 * @brief [AdventureWorldMapTest] 표지가 없는 글은 읽기 실패이고, 다시 읽으면 앞의 표지와 진행이 남지 않는다
 */
SW_TEST_CASE( AdventureWorldMapTest, ReloadReplacesEverything )
{
    sw::AdventureWorldMap world;
    SW_EXPECT_FALSE( world.loadFromXMLText( R"(<AdventureWorld/>)", "empty" ) );
    SW_REQUIRE_TRUE( world.loadFromXMLText( kWorldXML, "AdventureWorldMapTest" ) );
    SW_REQUIRE_TRUE( world.completeShrine( kShrineA ) );

    SW_REQUIRE_TRUE( world.loadFromXMLText( R"(<AdventureWorld><Shrine id="other"/></AdventureWorld>)", "other" ) );
    SW_EXPECT_EQUAL( 0, world.getOrbCount() );
    SW_EXPECT_TRUE( world.findLandmark( kShrineA ) == nullptr );
    SW_EXPECT_EQUAL( 4, world.getOrbsPerExchange() ); // 적지 않으면 기본값이고 앞의 값을 이어받지 않는다
    SW_EXPECT_TRUE( world.completeShrine( sw::hashed_string( "other" ) ) );
}

/**
 * @brief [AdventureWeaponWearTest] 칠 때마다 닳고, 남은 내구도가 경고 수준에 처음 닿은 때만 경고하며, 부서지는 마지막 한 방은 배율을 받고 슬롯이 빈다
 */
SW_TEST_CASE( AdventureWeaponWearTest, WearsWarnsOnceAndBreaksWithBonus )
{
    sw::ItemCatalog items;
    SW_REQUIRE_TRUE( items.loadFromXMLText( kWearItemXML, "AdventureWeaponWearTest" ) );
    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &items, 2 ) );
    SW_REQUIRE_EQUAL( 1, inventory.addItem( kSword, 1 ) );
    const sw::AdventureWeaponWear wear;

    sw::AdventureStrikeResult result = wear.strike( inventory, 0, 10.0f ); // 4 → 3
    SW_EXPECT_EQUAL( kSword, result._itemID );
    SW_EXPECT_NEAR( 10.0f, result._damage, kWearTolerance );
    SW_EXPECT_NEAR( 3.0f, result._durabilityLeft, kWearTolerance );
    SW_EXPECT_TRUE( result._bWarning == SW_FALSE );
    SW_EXPECT_TRUE( result._bBroke == SW_FALSE );

    result = wear.strike( inventory, 0, 10.0f ); // 3 → 2
    SW_EXPECT_TRUE( result._bWarning == SW_FALSE );
    result = wear.strike( inventory, 0, 10.0f ); // 2 → 1, 경고 수준(최대의 0.25 = 1)에 닿았다
    SW_EXPECT_TRUE( result._bWarning == SW_TRUE );

    result = wear.strike( inventory, 0, 10.0f ); // 1 → 0
    SW_EXPECT_TRUE( result._bBroke == SW_TRUE );
    SW_EXPECT_TRUE( result._bWarning == SW_FALSE );
    SW_EXPECT_NEAR( 20.0f, result._damage, kWearTolerance );
    SW_EXPECT_NEAR( 0.0f, result._durabilityLeft, kWearTolerance );
    SW_EXPECT_TRUE( inventory.getSlot( 0 ).isEmpty() );

    result = wear.strike( inventory, 0, 10.0f ); // 빈 슬롯
    SW_EXPECT_NEAR( 0.0f, result._damage, kWearTolerance );
    SW_EXPECT_TRUE( result._itemID.empty() );
}

/**
 * @brief [AdventureWeaponWearTest] 던지면 더 닳아 경고 수준을 건너뛰고 바로 부서질 수 있다. 내구도가 없는 물건은 닳지 않고, 범위 밖 슬롯은 아무 일도 하지 않는다
 */
SW_TEST_CASE( AdventureWeaponWearTest, ThrowWearsMoreAndOutOfRangeIsSafe )
{
    sw::ItemCatalog items;
    SW_REQUIRE_TRUE( items.loadFromXMLText( kWearItemXML, "AdventureWeaponWearTest" ) );
    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &items, 2 ) );
    SW_REQUIRE_EQUAL( 1, inventory.addItem( kSword, 1 ) );
    SW_REQUIRE_EQUAL( 1, inventory.addItem( kRock, 1 ) );
    sw::AdventureWeaponWear wear;

    sw::AdventureStrikeResult result = wear.strike( inventory, 0, 10.0f, true ); // 4 → 2
    SW_EXPECT_NEAR( 2.0f, result._durabilityLeft, kWearTolerance );
    result = wear.strike( inventory, 0, 10.0f, true ); // 2 → 0
    SW_EXPECT_TRUE( result._bBroke == SW_TRUE );
    SW_EXPECT_TRUE( result._bWarning == SW_FALSE );

    for ( int32 hit = 0; hit < 3; ++hit )
    {
        result = wear.strike( inventory, 1, 7.0f );
        SW_EXPECT_NEAR( 7.0f, result._damage, kWearTolerance );
        SW_EXPECT_TRUE( result._bBroke == SW_FALSE );
    }
    SW_EXPECT_FALSE( inventory.getSlot( 1 ).isEmpty() );

    SW_EXPECT_TRUE( wear.strike( inventory, -1, 7.0f )._itemID.empty() );
    SW_EXPECT_TRUE( wear.strike( inventory, 2, 7.0f )._itemID.empty() );

    sw::AdventureWeaponWearSettings settings;
    settings._breakMultiplier = 3.0f;
    wear.setSettings( settings );
    SW_REQUIRE_EQUAL( 1, inventory.addItem( kSword, 1 ) );
    settings._wearPerHit = 100.0f;
    wear.setSettings( settings );
    result = wear.strike( inventory, 0, 10.0f ); // 내구도보다 크게 닳아도 부서짐 한 번이다
    SW_EXPECT_TRUE( result._bBroke == SW_TRUE );
    SW_EXPECT_NEAR( 30.0f, result._damage, kWearTolerance );
}

/**
 * @brief [AdventureWorldMapTest] 진행 저장: 탑 · 사당 · 증표가 왕복한다. 잘린 바이트는 받지 않고 그대로 둔다
 */
SW_TEST_CASE( AdventureWorldMapTest, StateRoundTripsAndRefusesBrokenBytes )
{
    sw::AdventureWorldMap world;
    SW_REQUIRE_TRUE( world.loadFromXMLText( kWorldXML, "AdventureWorldMapTest" ) );
    sw::AreaGraph graph;
    SW_REQUIRE_TRUE( AdventureWorldTestUtil::addArea( graph, "a", "Plateau" ) );
    SW_EXPECT_TRUE( world.activateTower( kTower, graph ) >= 0 );
    SW_EXPECT_TRUE( world.completeShrine( kShrineA ) );

    sw::Archive writer;
    world.writeState( writer );
    const sw::vector<uint8> listSaved( writer.getData(), writer.getData() + writer.getSize() );

    sw::AdventureWorldMap restored;
    SW_REQUIRE_TRUE( restored.loadFromXMLText( kWorldXML, "AdventureWorldMapTest" ) );
    sw::Archive reader( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored.readState( reader ) );
    SW_EXPECT_TRUE( restored.canWarpTo( kTower ) );
    SW_EXPECT_TRUE( restored.isCompleted( kShrineA ) );
    SW_EXPECT_EQUAL( 1, restored.getOrbCount() );
    SW_EXPECT_EQUAL( 1, restored.getCompletedShrineCount() );
    SW_EXPECT_FALSE( restored.completeShrine( kShrineA ) ); // 다시 받지 못한다

    sw::AdventureWorldMap fresh;
    SW_REQUIRE_TRUE( fresh.loadFromXMLText( kWorldXML, "AdventureWorldMapTest" ) );
    sw::Archive truncated( listSaved.data(), listSaved.size() - 1 );
    SW_EXPECT_FALSE( fresh.readState( truncated ) );
    SW_EXPECT_EQUAL( 0, fresh.getOrbCount() );
    SW_EXPECT_FALSE( fresh.canWarpTo( kTower ) );
}
