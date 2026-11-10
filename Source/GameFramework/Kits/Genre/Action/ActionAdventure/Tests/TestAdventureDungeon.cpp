#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureDungeon.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

// AdventureDungeonTest — 던전 규칙: 작은 열쇠 · 보스 열쇠 · 조건 문, 던전마다 따로 세는 열쇠, 상자 보상과 지도 · 나침반, 스위치 · 눌림판 · 시간제 · 횃불 그룹, 상태 왕복과 깨진 바이트, 카탈로그 거절.
// 결함: 저장에 없는 던전이 지금 진행을 그대로 물려받았다(새 상태여야 한다) · 겹친 문 · 상자 · 장치 id 를 말없이 받았다. 키트 GF_ActionAdventure(nogpu).

namespace
{
    constexpr const utf8* kDungeonXML = R"(<AdventureDungeons>
  <Dungeon id="forest" region="ForestTemple">
    <Door id="d1" kind="SmallKey" flag="forest.d1"/>
    <Door id="d2" kind="SmallKey" flag="forest.d2"/>
    <Door id="boss" kind="BossKey" flag="forest.boss"/>
    <Door id="crack" kind="Condition" flag="forest.crack" requires="bombs>=1"/>
    <Treasure id="keys" area="hall" item="SmallKey" count="2" flag="forest.keys"/>
    <Treasure id="bossKey" item="BossKey" flag="forest.bossKey"/>
    <Treasure id="map" item="Map" flag="forest.map"/>
    <Treasure id="compass" item="Compass" flag="forest.compass"/>
    <Treasure id="gold" item="Rupee" count="50" flag="forest.gold"/>
    <Device id="eye" kind="Switch" flag="forest.eye"/>
    <Device id="clock" kind="TimedSwitch" duration="2" flag="forest.clock"/>
    <Device id="plate" kind="PressurePlate" flag="forest.plate"/>
    <Device id="latch" kind="PressurePlate" latch="true" flag="forest.latch"/>
    <Device id="torches" kind="TorchGroup" torches="2" duration="3" flag="forest.torches"/>
    <Device id="slowTorches" kind="TorchGroup" torches="2" flag="forest.slowTorches"/>
  </Dungeon>
  <Dungeon id="cave" region="Cave">
    <Door id="d1" kind="SmallKey" flag="cave.d1"/>
  </Dungeon>
</AdventureDungeons>)";

    const sw::hashed_string kForest{ "forest" };
    const sw::hashed_string kCave{ "cave" };
    const sw::hashed_string kNowhere{ "nowhere" };
    const sw::hashed_string kDoor1{ "d1" };
    const sw::hashed_string kDoor2{ "d2" };
    const sw::hashed_string kBossDoor{ "boss" };
    const sw::hashed_string kCrackDoor{ "crack" };
    const sw::hashed_string kKeysChest{ "keys" };
    const sw::hashed_string kBossKeyChest{ "bossKey" };
    const sw::hashed_string kMapChest{ "map" };
    const sw::hashed_string kCompassChest{ "compass" };
    const sw::hashed_string kGoldChest{ "gold" };
    const sw::hashed_string kEye{ "eye" };
    const sw::hashed_string kClock{ "clock" };
    const sw::hashed_string kPlate{ "plate" };
    const sw::hashed_string kLatch{ "latch" };
    const sw::hashed_string kTorches{ "torches" };
    const sw::hashed_string kSlowTorches{ "slowTorches" };
    const sw::hashed_string kFlagEye{ "forest.eye" };
    const sw::hashed_string kFlagClock{ "forest.clock" };
    const sw::hashed_string kFlagPlate{ "forest.plate" };
    const sw::hashed_string kFlagLatch{ "forest.latch" };
    const sw::hashed_string kFlagTorches{ "forest.torches" };

    struct AdventureDungeonTestUtil
    {
        /** @brief 던전 하나짜리 바이트를 씁니다(장치 없음 또는 @p deviceCount 개, 모두 @p litCount). */
        static void writeOneDungeon( sw::Archive& outArchive, const sw::hashed_string& dungeonID, int32 smallKeys, uint8 bBossKey, uint32 deviceCount, int32 litCount )
        {
            outArchive << static_cast<uint32>( 1 );
            sw::StateIO::writeName( outArchive, dungeonID );
            outArchive << smallKeys;
            outArchive << static_cast<int32>( 0 );
            outArchive << bBossKey;
            outArchive << static_cast<uint8>( SW_FALSE );
            outArchive << static_cast<uint8>( SW_FALSE );
            outArchive << deviceCount;
            for ( uint32 device = 0; device < deviceCount; ++device )
            {
                outArchive << 0.0f;
                outArchive << litCount;
                outArchive << static_cast<uint8>( SW_FALSE );
            }
        }
    };
} // namespace

/**
 * @brief [AdventureDungeonTest] 작은 열쇠 문은 열쇠 하나를 쓰고 플래그를 세우며, 다시 열 때 열쇠를 또 쓰지 않는다. 열쇠는 던전마다 따로 센다
 */
SW_TEST_CASE( AdventureDungeonTest, SmallKeysOpenDoorsAndBelongToOneDungeon )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState state;
    state.initialize( &catalog );
    sw::GameFlags flags;

    SW_EXPECT_TRUE( state.openDoor( kForest, kDoor1, flags ) == sw::AdventureDoorResult::NeedSmallKey );
    SW_EXPECT_TRUE( state.addSmallKey( kForest, 2 ) );
    SW_EXPECT_FALSE( state.addSmallKey( kForest, 0 ) );
    SW_EXPECT_FALSE( state.addSmallKey( kForest, -3 ) );
    SW_EXPECT_FALSE( state.addSmallKey( kNowhere ) );
    SW_EXPECT_EQUAL( 2, state.findProgress( kForest )->_smallKeyCount );

    SW_EXPECT_TRUE( state.openDoor( kCave, kDoor1, flags ) == sw::AdventureDoorResult::NeedSmallKey ); // 다른 던전의 열쇠

    SW_EXPECT_TRUE( state.openDoor( kForest, kDoor1, flags ) == sw::AdventureDoorResult::Opened );
    SW_EXPECT_TRUE( flags.hasFlag( sw::hashed_string( "forest.d1" ) ) );
    SW_EXPECT_FALSE( flags.hasFlag( sw::hashed_string( "cave.d1" ) ) );
    SW_EXPECT_TRUE( state.openDoor( kForest, kDoor1, flags ) == sw::AdventureDoorResult::AlreadyOpen );
    SW_EXPECT_EQUAL( 1, state.findProgress( kForest )->_smallKeyCount );
    SW_EXPECT_EQUAL( 1, state.findProgress( kForest )->_smallKeyUsedCount );

    SW_EXPECT_TRUE( state.openDoor( kForest, kDoor2, flags ) == sw::AdventureDoorResult::Opened );
    SW_EXPECT_EQUAL( 0, state.findProgress( kForest )->_smallKeyCount );

    SW_EXPECT_TRUE( state.openDoor( kForest, sw::hashed_string( "ghost" ), flags ) == sw::AdventureDoorResult::UnknownDoor );
    SW_EXPECT_TRUE( state.openDoor( kNowhere, kDoor1, flags ) == sw::AdventureDoorResult::UnknownDoor );

    sw::vector<sw::AdventureDungeonEvent> listEvent;
    state.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 2, sw::VectorUtil::countBy<&sw::AdventureDungeonEvent::_type>( listEvent, sw::AdventureDungeonEventKind::DoorOpened ) );
}

/**
 * @brief [AdventureDungeonTest] 보스 문은 보스 열쇠를 쓰지 않고 작은 열쇠로 열리지 않으며, 조건 문은 플래그 식이 참일 때만 열린다
 */
SW_TEST_CASE( AdventureDungeonTest, BossAndConditionDoors )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState state;
    state.initialize( &catalog );
    sw::GameFlags     flags;
    sw::ItemStackList reward;

    SW_REQUIRE_TRUE( state.addSmallKey( kForest ) );
    SW_EXPECT_TRUE( state.openDoor( kForest, kBossDoor, flags ) == sw::AdventureDoorResult::NeedBossKey );
    SW_EXPECT_EQUAL( 1, state.findProgress( kForest )->_smallKeyCount );
    SW_REQUIRE_TRUE( state.openTreasure( kForest, kBossKeyChest, flags, reward ) );
    SW_EXPECT_TRUE( state.openDoor( kForest, kBossDoor, flags ) == sw::AdventureDoorResult::Opened );
    SW_EXPECT_TRUE( state.findProgress( kForest )->_bBossKey == SW_TRUE );

    SW_EXPECT_TRUE( state.openDoor( kForest, kCrackDoor, flags ) == sw::AdventureDoorResult::ConditionNotMet );
    flags.setFlag( sw::hashed_string( "bombs" ), 1 );
    SW_EXPECT_TRUE( state.openDoor( kForest, kCrackDoor, flags ) == sw::AdventureDoorResult::Opened );
}

/**
 * @brief [AdventureDungeonTest] 던전 아이템은 던전이 가져가고 나머지는 보상으로 나오며, 같은 상자는 한 번만 열린다. 모르는 상자 · 던전은 거짓이다
 */
SW_TEST_CASE( AdventureDungeonTest, TreasuresGoToTheDungeonOrTheReward )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState state;
    state.initialize( &catalog );
    sw::GameFlags     flags;
    sw::ItemStackList reward;

    SW_EXPECT_TRUE( state.openTreasure( kForest, kKeysChest, flags, reward ) );
    SW_EXPECT_EQUAL( 2, state.findProgress( kForest )->_smallKeyCount );
    SW_EXPECT_TRUE( reward.isEmpty() );
    SW_EXPECT_FALSE( state.openTreasure( kForest, kKeysChest, flags, reward ) );
    SW_EXPECT_EQUAL( 2, state.findProgress( kForest )->_smallKeyCount );

    SW_EXPECT_TRUE( state.openTreasure( kForest, kGoldChest, flags, reward ) );
    SW_EXPECT_EQUAL( 50, reward.getItemCount( sw::hashed_string( "Rupee" ) ) );

    SW_EXPECT_FALSE( state.openTreasure( kForest, sw::hashed_string( "ghost" ), flags, reward ) );
    SW_EXPECT_FALSE( state.openTreasure( kNowhere, kKeysChest, flags, reward ) );

    sw::vector<sw::AdventureDungeonEvent> listEvent;
    state.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t( 2 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[1]._type == sw::AdventureDungeonEventKind::TreasureOpened );
    SW_EXPECT_EQUAL( sw::hashed_string( "Rupee" ), listEvent[1]._item );
    SW_EXPECT_EQUAL( 50, listEvent[1]._count );
}

/**
 * @brief [AdventureDungeonTest] 지도가 있어야 던전 지역만 드러나고, 나침반이 있어야 아직 열지 않은 상자를 표시한다(이미 연 상자는 빠진다)
 */
SW_TEST_CASE( AdventureDungeonTest, MapRevealsAndCompassMarks )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState state;
    state.initialize( &catalog );
    sw::GameFlags     flags;
    sw::ItemStackList reward;

    sw::AreaGraph graph;
    sw::AreaDef   hall;
    hall._id     = sw::hashed_string( "hall" );
    hall._region = sw::hashed_string( "ForestTemple" );
    SW_REQUIRE_TRUE( graph.addArea( hall ) );
    sw::AreaDef outside;
    outside._id     = sw::hashed_string( "outside" );
    outside._region = sw::hashed_string( "Field" );
    SW_REQUIRE_TRUE( graph.addArea( outside ) );

    SW_EXPECT_EQUAL( 0, state.revealMap( kForest, graph ) );
    SW_EXPECT_FALSE( graph.isDiscovered( sw::hashed_string( "hall" ) ) );
    SW_EXPECT_EQUAL( 0, state.revealMap( kNowhere, graph ) );
    SW_REQUIRE_TRUE( state.openTreasure( kForest, kMapChest, flags, reward ) );
    SW_EXPECT_EQUAL( 1, state.revealMap( kForest, graph ) );
    SW_EXPECT_TRUE( graph.isDiscovered( sw::hashed_string( "hall" ) ) );
    SW_EXPECT_FALSE( graph.isDiscovered( sw::hashed_string( "outside" ) ) );
    SW_EXPECT_EQUAL( 0, state.revealMap( kForest, graph ) );

    sw::vector<const sw::AdventureTreasureDef*> listMarker;
    listMarker.push_back( nullptr );
    state.collectCompassMarker( kForest, flags, listMarker );
    SW_EXPECT_TRUE( listMarker.empty() );
    SW_REQUIRE_TRUE( state.openTreasure( kForest, kCompassChest, flags, reward ) );
    state.collectCompassMarker( kForest, flags, listMarker );
    SW_REQUIRE_EQUAL( size_t( 3 ), listMarker.size() ); // keys · bossKey · gold
    SW_EXPECT_EQUAL( kKeysChest, listMarker[0]->_id );
    SW_EXPECT_EQUAL( sw::hashed_string( "hall" ), listMarker[0]->_area );
    SW_EXPECT_EQUAL( kGoldChest, listMarker[2]->_id );
    SW_EXPECT_TRUE( reward.isEmpty() );
}

/**
 * @brief [AdventureDungeonTest] 스위치는 켜진 채로 남고 플래그를 세운다. 스위치가 아닌 장치 · 모르는 장치는 치지 못한다
 */
SW_TEST_CASE( AdventureDungeonTest, SwitchesStayOn )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState state;
    state.initialize( &catalog );
    sw::GameFlags flags;

    SW_EXPECT_TRUE( state.hitSwitch( kForest, kEye, flags ) );
    SW_EXPECT_TRUE( state.isDeviceActive( kForest, kEye ) );
    SW_EXPECT_TRUE( flags.hasFlag( kFlagEye ) );
    SW_EXPECT_TRUE( state.hitSwitch( kForest, kEye, flags ) ); // 켜진 채 다시 쳐도 알림은 하나다
    state.update( 1000.0f, flags );
    SW_EXPECT_TRUE( state.isDeviceActive( kForest, kEye ) );

    SW_EXPECT_FALSE( state.hitSwitch( kForest, kPlate, flags ) );
    SW_EXPECT_FALSE( state.hitSwitch( kForest, kTorches, flags ) );
    SW_EXPECT_FALSE( state.hitSwitch( kForest, sw::hashed_string( "ghost" ), flags ) );
    SW_EXPECT_FALSE( state.hitSwitch( kNowhere, kEye, flags ) );
    SW_EXPECT_FALSE( state.isDeviceActive( kNowhere, kEye ) );
    SW_EXPECT_FALSE( state.isDeviceActive( kForest, sw::hashed_string( "ghost" ) ) );

    sw::vector<sw::AdventureDungeonEvent> listEvent;
    state.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::AdventureDungeonEvent::_type>( listEvent, sw::AdventureDungeonEventKind::DeviceActivated ) );
}

/**
 * @brief [AdventureDungeonTest] 시간제 스위치는 정해진 시간이 지나면 꺼지고, 다시 치면 시간이 처음부터 시작한다. 0 · 음수 시간은 흐르지 않는다
 */
SW_TEST_CASE( AdventureDungeonTest, TimedSwitchExpiresAndRestarts )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState state;
    state.initialize( &catalog );
    sw::GameFlags flags;

    SW_REQUIRE_TRUE( state.hitSwitch( kForest, kClock, flags ) );
    state.update( 1.0f, flags );
    SW_EXPECT_TRUE( state.isDeviceActive( kForest, kClock ) );
    SW_REQUIRE_TRUE( state.hitSwitch( kForest, kClock, flags ) ); // 다시 2 초
    state.update( 0.0f, flags );
    state.update( -5.0f, flags );
    state.update( 1.5f, flags );
    SW_EXPECT_TRUE( state.isDeviceActive( kForest, kClock ) );
    SW_EXPECT_TRUE( flags.hasFlag( kFlagClock ) );
    state.update( 0.5f, flags );
    SW_EXPECT_FALSE( state.isDeviceActive( kForest, kClock ) );
    SW_EXPECT_FALSE( flags.hasFlag( kFlagClock ) );

    sw::vector<sw::AdventureDungeonEvent> listEvent;
    state.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::AdventureDungeonEvent::_type>( listEvent, sw::AdventureDungeonEventKind::DeviceActivated ) );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::AdventureDungeonEvent::_type>( listEvent, sw::AdventureDungeonEventKind::DeviceDeactivated ) );
}

/**
 * @brief [AdventureDungeonTest] 눌림판은 누르는 동안만 켜지고, 걸쇠 눌림판은 한 번 눌리면 남는다. 눌림판이 아닌 장치는 무시한다
 */
SW_TEST_CASE( AdventureDungeonTest, PressurePlatesAndLatches )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState state;
    state.initialize( &catalog );
    sw::GameFlags flags;

    state.setPlatePressed( kForest, kPlate, true, flags );
    SW_EXPECT_TRUE( flags.hasFlag( kFlagPlate ) );
    state.setPlatePressed( kForest, kPlate, false, flags );
    SW_EXPECT_FALSE( state.isDeviceActive( kForest, kPlate ) );
    SW_EXPECT_FALSE( flags.hasFlag( kFlagPlate ) );

    state.setPlatePressed( kForest, kLatch, false, flags ); // 눌리기 전에 떼는 것은 아무 일도 아니다
    SW_EXPECT_FALSE( state.isDeviceActive( kForest, kLatch ) );
    state.setPlatePressed( kForest, kLatch, true, flags );
    state.setPlatePressed( kForest, kLatch, false, flags );
    SW_EXPECT_TRUE( state.isDeviceActive( kForest, kLatch ) );
    SW_EXPECT_TRUE( flags.hasFlag( kFlagLatch ) );

    state.setPlatePressed( kForest, kEye, true, flags );
    SW_EXPECT_FALSE( state.isDeviceActive( kForest, kEye ) );
    state.setPlatePressed( kNowhere, kPlate, true, flags );
    state.setPlatePressed( kForest, sw::hashed_string( "ghost" ), true, flags );
}

/**
 * @brief [AdventureDungeonTest] 횃불 그룹은 시간 안에 다 켜야 풀리고, 못 켜면 모두 꺼져 처음부터이며, 풀린 뒤에는 시간이 지나도 꺼지지 않는다
 */
SW_TEST_CASE( AdventureDungeonTest, TorchGroupsNeedAllTorchesInTime )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState state;
    state.initialize( &catalog );
    sw::GameFlags flags;

    SW_EXPECT_FALSE( state.lightTorch( kForest, kEye, flags ) );
    SW_EXPECT_FALSE( state.lightTorch( kNowhere, kTorches, flags ) );

    SW_EXPECT_TRUE( state.lightTorch( kForest, kTorches, flags ) );
    SW_EXPECT_EQUAL( 1, state.getLitTorchCount( kForest, kTorches ) );
    SW_EXPECT_FALSE( state.isDeviceActive( kForest, kTorches ) );
    state.update( 2.0f, flags );
    SW_EXPECT_EQUAL( 1, state.getLitTorchCount( kForest, kTorches ) );
    state.update( 1.0f, flags ); // 3 초가 다 됐다
    SW_EXPECT_EQUAL( 0, state.getLitTorchCount( kForest, kTorches ) );
    sw::vector<sw::AdventureDungeonEvent> listEvent;
    state.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::AdventureDungeonEvent::_type>( listEvent, sw::AdventureDungeonEventKind::TorchesFailed ) );

    SW_EXPECT_TRUE( state.lightTorch( kForest, kTorches, flags ) );
    state.update( 2.0f, flags ); // 첫 불에서 시간이 다시 시작한다
    SW_EXPECT_TRUE( state.lightTorch( kForest, kTorches, flags ) );
    SW_EXPECT_TRUE( state.isDeviceActive( kForest, kTorches ) );
    SW_EXPECT_TRUE( flags.hasFlag( kFlagTorches ) );
    SW_EXPECT_FALSE( state.lightTorch( kForest, kTorches, flags ) );
    state.update( 100.0f, flags );
    SW_EXPECT_TRUE( state.isDeviceActive( kForest, kTorches ) );
    SW_EXPECT_EQUAL( 2, state.getLitTorchCount( kForest, kTorches ) );

    // 시간이 없는 그룹은 기다려도 꺼지지 않는다.
    SW_EXPECT_TRUE( state.lightTorch( kForest, kSlowTorches, flags ) );
    state.update( 1000.0f, flags );
    SW_EXPECT_EQUAL( 1, state.getLitTorchCount( kForest, kSlowTorches ) );
    SW_EXPECT_EQUAL( 0, state.getLitTorchCount( kNowhere, kTorches ) );
}

/**
 * @brief [AdventureDungeonTest] 카탈로그 없는 상태는 모든 호출이 안전한 거짓이다. 초기화하면 진행과 알림을 지운다
 */
SW_TEST_CASE( AdventureDungeonTest, UninitializedStateIsSafe )
{
    sw::AdventureDungeonState state;
    sw::GameFlags             flags;
    sw::ItemStackList         reward;
    SW_EXPECT_FALSE( state.addSmallKey( kForest ) );
    SW_EXPECT_TRUE( state.openDoor( kForest, kDoor1, flags ) == sw::AdventureDoorResult::UnknownDoor );
    SW_EXPECT_FALSE( state.openTreasure( kForest, kKeysChest, flags, reward ) );
    SW_EXPECT_FALSE( state.hitSwitch( kForest, kEye, flags ) );
    SW_EXPECT_TRUE( state.findProgress( kForest ) == nullptr );
    state.update( 1.0f, flags );

    sw::Archive written;
    state.writeState( written );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( state.readState( reading ) );

    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    state.initialize( &catalog );
    SW_REQUIRE_TRUE( state.addSmallKey( kForest ) );
    SW_REQUIRE_TRUE( state.hitSwitch( kForest, kEye, flags ) );
    state.initialize( &catalog );
    SW_EXPECT_EQUAL( 0, state.findProgress( kForest )->_smallKeyCount );
    SW_EXPECT_FALSE( state.isDeviceActive( kForest, kEye ) );
    sw::vector<sw::AdventureDungeonEvent> listEvent;
    state.pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
}

/**
 * @brief [AdventureDungeonTest] 쓰고 읽으면 열쇠 · 시간제 스위치 남은 시간 · 횃불 진행까지 이어져 원본과 같은 때 꺼지고, 쌓인 알림은 비운다
 */
SW_TEST_CASE( AdventureDungeonTest, StateRoundTripResumes )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState original;
    original.initialize( &catalog );
    sw::GameFlags flags;
    SW_REQUIRE_TRUE( original.addSmallKey( kCave, 3 ) );
    SW_REQUIRE_TRUE( original.hitSwitch( kForest, kClock, flags ) );
    SW_REQUIRE_TRUE( original.lightTorch( kForest, kTorches, flags ) );
    original.update( 1.0f, flags );

    sw::Archive written;
    original.writeState( written );
    sw::AdventureDungeonState restored;
    restored.initialize( &catalog );
    SW_REQUIRE_TRUE( restored.addSmallKey( kForest ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );

    SW_EXPECT_EQUAL( 3, restored.findProgress( kCave )->_smallKeyCount );
    SW_EXPECT_EQUAL( 0, restored.findProgress( kForest )->_smallKeyCount );
    SW_EXPECT_EQUAL( 1, restored.getLitTorchCount( kForest, kTorches ) );
    SW_EXPECT_TRUE( restored.isDeviceActive( kForest, kClock ) );
    sw::vector<sw::AdventureDungeonEvent> listEvent;
    restored.pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );

    sw::GameFlags restoredFlags = flags;
    restored.update( 1.0f, restoredFlags ); // 원본에서 1 초 지났으니 2 초 시간제가 여기서 끝난다
    SW_EXPECT_FALSE( restored.isDeviceActive( kForest, kClock ) );
    SW_EXPECT_FALSE( restoredFlags.hasFlag( kFlagClock ) );
    restored.update( 1.0f, restoredFlags ); // 3 초 횃불도 끝난다
    SW_EXPECT_EQUAL( 0, restored.getLitTorchCount( kForest, kTorches ) );
}

/**
 * @brief [AdventureDungeonTest] 결함: 저장에 없는 던전이 지금 진행(열쇠)을 그대로 물려받아 다른 판의 진행이 섞였다. 저장에 있는 던전만 읽고 나머지는 새 상태다
 */
SW_TEST_CASE( AdventureDungeonTest, DungeonsMissingFromTheSaveStartFresh )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState state;
    state.initialize( &catalog );
    sw::GameFlags flags;
    SW_REQUIRE_TRUE( state.addSmallKey( kForest, 5 ) );
    SW_REQUIRE_TRUE( state.hitSwitch( kForest, kEye, flags ) );

    sw::Archive written;
    AdventureDungeonTestUtil::writeOneDungeon( written, kCave, 2, SW_FALSE, 0, 0 );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( state.readState( reading ) );
    SW_EXPECT_EQUAL( 2, state.findProgress( kCave )->_smallKeyCount );
    SW_EXPECT_EQUAL( 0, state.findProgress( kForest )->_smallKeyCount );
    SW_EXPECT_FALSE( state.isDeviceActive( kForest, kEye ) );
}

/**
 * @brief [AdventureDungeonTest] 모르는 던전 · 장치 수 불일치 · 음수 열쇠 · 깨진 표시 · 횃불 수 초과 · 잘린 바이트를 거절하고 지금 상태를 그대로 둔다
 */
SW_TEST_CASE( AdventureDungeonTest, ReadRejectsBrokenBytes )
{
    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDungeonXML, "AdventureDungeonTest" ) );
    sw::AdventureDungeonState state;
    state.initialize( &catalog );
    SW_REQUIRE_TRUE( state.addSmallKey( kForest, 5 ) );

    const auto expectRejected = [&state]( const sw::Archive& written )
    {
        sw::Archive reading( written.getData(), written.getSize() );
        SW_EXPECT_FALSE( state.readState( reading ) );
        SW_EXPECT_EQUAL( 5, state.findProgress( kForest )->_smallKeyCount );
    };

    sw::Archive unknownDungeon;
    AdventureDungeonTestUtil::writeOneDungeon( unknownDungeon, kNowhere, 0, SW_FALSE, 0, 0 );
    expectRejected( unknownDungeon );

    sw::Archive deviceMismatch;
    AdventureDungeonTestUtil::writeOneDungeon( deviceMismatch, kCave, 0, SW_FALSE, 1, 0 );
    expectRejected( deviceMismatch );

    sw::Archive negativeKeys;
    AdventureDungeonTestUtil::writeOneDungeon( negativeKeys, kCave, -1, SW_FALSE, 0, 0 );
    expectRejected( negativeKeys );

    sw::Archive badFlag;
    AdventureDungeonTestUtil::writeOneDungeon( badFlag, kCave, 0, 2, 0, 0 );
    expectRejected( badFlag );

    sw::Archive tooManyLit; // 스위치 · 눌림판의 _torchCount 는 1, 횃불 그룹은 2 다
    AdventureDungeonTestUtil::writeOneDungeon( tooManyLit, kForest, 0, SW_FALSE, 6, 3 );
    expectRejected( tooManyLit );

    sw::Archive negativeLit;
    AdventureDungeonTestUtil::writeOneDungeon( negativeLit, kForest, 0, SW_FALSE, 6, -1 );
    expectRejected( negativeLit );

    sw::AdventureDungeonState source;
    source.initialize( &catalog );
    sw::Archive written;
    source.writeState( written );
    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( state.readState( truncated ) );
    SW_EXPECT_EQUAL( 5, state.findProgress( kForest )->_smallKeyCount );
}

/**
 * @brief [AdventureDungeonCatalogTest] 조건 없는 조건 문, 시간 없는 시간제 스위치, 필수 칸이 빠진 원소는 그 던전을 통째로 넣지 않고 다른 던전은 읽는다
 */
SW_TEST_CASE( AdventureDungeonCatalogTest, BrokenDungeonsAreSkipped )
{
    SW_EXPECT_LOG( "AdventureDungeon.ConditionDoorNeedsRequires" );
    SW_EXPECT_LOG( "AdventureDungeon.TimedSwitchNeedsDuration" );
    SW_EXPECT_LOG( "AdventureDungeon.DungeonIdErrorsNot" );
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );

    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<AdventureDungeons>
  <Dungeon id="noRequires" region="r"><Door id="d" kind="Condition" flag="f"/></Dungeon>
  <Dungeon id="noDuration" region="r"><Device id="d" kind="TimedSwitch" flag="f"/></Dungeon>
  <Dungeon id="noFlag" region="r"><Door id="d"/></Dungeon>
  <Dungeon id="fine" region="r"><Door id="d" flag="f"/></Dungeon>
</AdventureDungeons>)",
                                              "broken" ) );
    SW_EXPECT_TRUE( catalog.find( sw::hashed_string( "noRequires" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.find( sw::hashed_string( "noDuration" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.find( sw::hashed_string( "noFlag" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.find( sw::hashed_string( "fine" ) ) != nullptr );
}

/**
 * @brief [AdventureDungeonCatalogTest] 결함: 같은 id 의 문 · 상자 · 장치를 말없이 받아 뒤의 것에 영영 닿지 못했다. 종류가 다르면 같은 이름을 써도 된다
 */
SW_TEST_CASE( AdventureDungeonCatalogTest, DuplicateIdsInsideADungeonAreRejected )
{
    SW_EXPECT_LOG( "AdventureDungeon.DuplicateId" );
    SW_EXPECT_LOG( "AdventureDungeon.DungeonIdErrorsNot" );

    sw::AdventureDungeonCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<AdventureDungeons>
  <Dungeon id="doors" region="r"><Door id="d" flag="a"/><Door id="d" flag="b"/></Dungeon>
  <Dungeon id="chests" region="r"><Treasure id="t" item="x" flag="a"/><Treasure id="t" item="y" flag="b"/></Dungeon>
  <Dungeon id="devices" region="r"><Device id="s" flag="a"/><Device id="s" flag="b"/></Dungeon>
  <Dungeon id="fine" region="r"><Door id="n" flag="a"/><Treasure id="n" item="x" flag="b"/><Device id="n" flag="c"/></Dungeon>
</AdventureDungeons>)",
                                              "duplicates" ) );
    SW_EXPECT_TRUE( catalog.find( sw::hashed_string( "doors" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.find( sw::hashed_string( "chests" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.find( sw::hashed_string( "devices" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.find( sw::hashed_string( "fine" ) ) != nullptr );
}
