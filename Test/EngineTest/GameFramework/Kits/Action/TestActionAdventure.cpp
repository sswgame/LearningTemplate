// 액션 어드벤처 키트(젤다 장르) — 던전 열쇠 · 문 · 지도 · 나침반 · 장치, 하트 · 마법 · 스태미나, 주목 몸놀림, 원소 화학, 요리, 무기 내구도, 탑 · 사당.
#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Inventory/Crafting.h"
#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Inventory/ItemStackList.h"
#include "GameFramework/Base/World/AreaGraph.h"
#include "GameFramework/Base/World/GameFlags.h"
#include "GameFramework/Kits/Action/ActionAdventure/AdventureCooking.h"
#include "GameFramework/Kits/Action/ActionAdventure/AdventureDungeon.h"
#include "GameFramework/Kits/Action/ActionAdventure/AdventureElementGrid.h"
#include "GameFramework/Kits/Action/ActionAdventure/AdventureTargeting.h"
#include "GameFramework/Kits/Action/ActionAdventure/AdventureVitals.h"
#include "GameFramework/Kits/Action/ActionAdventure/AdventureWeaponWear.h"
#include "GameFramework/Kits/Action/ActionAdventure/AdventureWorldMap.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr const utf8* kAdventureAreaXml = R"(
<AreaGraph>
  <Area id="entrance" region="Forest"/>
  <Area id="hall" region="Forest"/>
  <Area id="cell" region="Forest"/>
  <Area id="lair" region="Forest"/>
  <Area id="vault" region="Forest"/>
  <Area id="lake" region="Water"/>
  <Area id="meadow" region="Plateau"/>
  <Area id="ruins" region="Plateau"/>
  <Link from="entrance" to="hall"/>
  <Link from="hall" to="cell" requires="forest.cell"/>
  <Link from="hall" to="lair" requires="forest.boss"/>
  <Link from="hall" to="vault" requires="forest.crack"/>
</AreaGraph>
)";

    constexpr const utf8* kAdventureDungeonXml = R"(
<AdventureDungeons>
  <Dungeon id="forest" region="Forest">
    <Door id="cellDoor" kind="SmallKey" flag="forest.cell"/>
    <Door id="bossDoor" kind="BossKey" flag="forest.boss"/>
    <Door id="crack" kind="Condition" requires="bombs>=1" flag="forest.crack"/>
    <Treasure id="keyChest" area="hall" item="SmallKey"/>
    <Treasure id="bigChest" area="cell" item="BossKey"/>
    <Treasure id="mapChest" area="hall" item="Map"/>
    <Treasure id="compassChest" area="cell" item="Compass"/>
    <Treasure id="rupeeChest" area="vault" item="rupee" count="20"/>
    <Treasure id="pieceChest" area="vault" item="heartPiece"/>
    <Device id="eye" kind="Switch" flag="forest.gate"/>
    <Device id="plate" kind="PressurePlate"/>
    <Device id="heavyPlate" kind="PressurePlate" latch="true"/>
    <Device id="timer" kind="TimedSwitch" duration="3"/>
    <Device id="torches" kind="TorchGroup" torches="3" duration="5"/>
    <Device id="mystery" kind="Lever"/>
  </Dungeon>
  <Dungeon id="water" region="Water">
    <Door id="waterDoor" kind="SmallKey"/>
  </Dungeon>
</AdventureDungeons>
)";

    struct AdventureDungeonScene
    {
        AreaGraph               _areaGraph;
        AdventureDungeonCatalog _catalog;
        AdventureDungeonState   _state;
        GameFlags               _flags;

        bool initialize()
        {
            if ( _areaGraph.loadFromXmlText( kAdventureAreaXml, "ActionAdventureTest" ) == false )
                return false;
            if ( _catalog.loadFromXmlText( kAdventureDungeonXml, "ActionAdventureTest" ) == false )
                return false;
            _state.initialize( &_catalog );
            return true;
        }

        void run( float32 seconds )
        {
            for ( float32 elapsed = 0.0f; elapsed < seconds - 0.001f; elapsed += 0.1f )
            {
                _state.update( 0.1f, _flags );
            }
        }
    };

    bool hasDungeonEvent( const vector<AdventureDungeonEvent>& listEvent, AdventureDungeonEventType type, const utf8* pId )
    {
        for ( const AdventureDungeonEvent& event : listEvent )
        {
            if ( event._type == type && event._id == hashed_string( pId ) )
                return true;
        }
        return false;
    }

    /** @brief 가로 8 × 세로 3 — 가운데 줄이 풀, 불 위는 얼음, 불 아래는 나무입니다. */
    void fillMeadow( AdventureElementGrid& grid, const int2& wind )
    {
        grid.initialize( 8, 3, AdventureElementSettings{} );
        for ( int32 column = 0; column < 8; ++column )
        {
            grid.setMaterial( int2{ column, 1 }, AdventureMaterial::Grass );
        }
        grid.setMaterial( int2{ 3, 0 }, AdventureMaterial::Ice );
        grid.setMaterial( int2{ 3, 2 }, AdventureMaterial::Wood );
        grid.setWind( wind );
    }

    constexpr const utf8* kAdventureItemXml = R"(
<ItemCatalog>
  <Item id="hydromelon" category="Food" maxStack="99"/>
  <Item id="chillshroom" category="Food" maxStack="99"/>
  <Item id="spicyPepper" category="Food" maxStack="99"/>
  <Item id="wildberry" category="Food" maxStack="99"/>
  <Item id="chillySorbet" category="Dish" maxStack="1"/>
  <Item id="simmeredDish" category="Dish" maxStack="1"/>
  <Item id="dubiousFood" category="Dish" maxStack="1"/>
  <Item id="treeBranch" category="Weapon" slot="MainHand" durability="4"/>
  <Item id="masterSword" category="Weapon" slot="MainHand"/>
</ItemCatalog>
)";

    constexpr const utf8* kAdventureCookingXml = R"(
<AdventureCooking maxIngredients="5" heartScale="2" durationPerIngredient="30" station="CookingPot" generic="simmeredDish" dubious="dubiousFood" dubiousHearts="4">
  <Effect id="Chilly" tier2="4" tier3="7" maxDuration="1800"/>
  <Effect id="Spicy" tier2="4" tier3="7" maxDuration="1800"/>
  <Ingredient id="hydromelon" effect="Chilly" potency="1" duration="150" hearts="2"/>
  <Ingredient id="chillshroom" effect="Chilly" potency="2" duration="150" hearts="1"/>
  <Ingredient id="spicyPepper" effect="Spicy" potency="1" duration="150" hearts="1"/>
  <Ingredient id="wildberry" hearts="2"/>
</AdventureCooking>
)";

    constexpr const utf8* kAdventureRecipeXml = R"(
<RecipeCatalog>
  <Recipe id="sorbet" station="CookingPot"><In item="hydromelon" count="2"/><Out item="chillySorbet" count="1"/></Recipe>
</RecipeCatalog>
)";

    /** @brief 상태 바이트를 꺼냅니다. */
    template <typename StateType>
    vector<uint8> captureAdventureBytes( const StateType& state )
    {
        Archive archive;
        state.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }

    /** @brief @p bytes 를 @p outState 에 읽고 끝까지 다 읽었으면 true 입니다. */
    template <typename StateType>
    [[nodiscard]] bool restoreAdventureBytes( const vector<uint8>& bytes, StateType& outState )
    {
        Archive reader( bytes.data(), bytes.size() );
        return outState.readState( reader ) && reader.getRemainingBytes() == 0;
    }

    /** @brief 마지막 한 바이트를 자른 바이트를 @p outState 에 읽습니다(거절되어야 한다). */
    template <typename StateType>
    [[nodiscard]] bool restoreTruncatedAdventureBytes( const vector<uint8>& bytes, StateType& outState )
    {
        Archive reader( bytes.data(), bytes.size() - 1 );
        return outState.readState( reader );
    }
} // namespace

/**
 * @brief [ActionAdventureTest] 던전 — 작은 열쇠는 그 던전에서만 하나씩 쓰이고, 보스 문 · 폭탄 문이 열리면 그래프 길이 열리며, 지도 · 나침반이 방 · 상자를 드러낸다
 */
SW_TEST_CASE( ActionAdventureTest, DungeonKeysDoorsMapAndCompass )
{
    AdventureDungeonScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    AdventureDungeonState& state = scene._state;
    GameFlags&             flags = scene._flags;
    ItemStackList          reward;

    SW_EXPECT_TRUE( state.openDoor( "forest", "cellDoor", flags ) == AdventureDoorResult::NeedSmallKey );
    SW_EXPECT_FALSE( scene._areaGraph.canTraverse( "hall", "cell", flags ) );
    SW_EXPECT_TRUE( state.openTreasure( "forest", "keyChest", flags, reward ) );
    SW_EXPECT_FALSE( state.openTreasure( "forest", "keyChest", flags, reward ) ); // 이미 연 상자
    SW_EXPECT_EQUAL( 1, state.findProgress( "forest" )->_smallKeyCount );
    // 숲의 열쇠로 물의 문은 열 수 없다.
    SW_EXPECT_TRUE( state.openDoor( "water", "waterDoor", flags ) == AdventureDoorResult::NeedSmallKey );
    SW_EXPECT_TRUE( state.openDoor( "forest", "cellDoor", flags ) == AdventureDoorResult::Opened );
    SW_EXPECT_EQUAL( 0, state.findProgress( "forest" )->_smallKeyCount );
    SW_EXPECT_EQUAL( 1, state.findProgress( "forest" )->_smallKeyUsedCount );
    SW_EXPECT_TRUE( scene._areaGraph.canTraverse( "hall", "cell", flags ) );
    SW_EXPECT_TRUE( state.openDoor( "forest", "cellDoor", flags ) == AdventureDoorResult::AlreadyOpen );
    SW_EXPECT_EQUAL( 0, state.findProgress( "forest" )->_smallKeyCount ); // 열린 문은 열쇠를 다시 먹지 않는다

    SW_EXPECT_TRUE( state.openDoor( "forest", "bossDoor", flags ) == AdventureDoorResult::NeedBossKey );
    SW_EXPECT_TRUE( state.openTreasure( "forest", "bigChest", flags, reward ) );
    SW_EXPECT_TRUE( state.openDoor( "forest", "bossDoor", flags ) == AdventureDoorResult::Opened );
    SW_EXPECT_TRUE( state.findProgress( "forest" )->_bBossKey == SW_TRUE ); // 보스 열쇠는 남는다

    SW_EXPECT_TRUE( state.openDoor( "forest", "crack", flags ) == AdventureDoorResult::ConditionNotMet );
    flags.setFlag( "bombs", 3 );
    SW_EXPECT_TRUE( state.openDoor( "forest", "crack", flags ) == AdventureDoorResult::Opened );
    SW_EXPECT_TRUE( scene._areaGraph.canTraverse( "hall", "vault", flags ) );
    SW_EXPECT_TRUE( state.openDoor( "forest", "nothing", flags ) == AdventureDoorResult::UnknownDoor );

    SW_EXPECT_TRUE( state.openTreasure( "forest", "rupeeChest", flags, reward ) );
    SW_EXPECT_EQUAL( 20, reward.getItemCount( "rupee" ) );
    SW_EXPECT_EQUAL( 0, reward.getItemCount( "SmallKey" ) ); // 던전 아이템은 보상 목록으로 가지 않는다

    // 지도 — 없으면 아무것도, 있으면 아직 모르는 숲의 방이 모두 드러난다(물의 방은 아니다).
    SW_EXPECT_TRUE( scene._areaGraph.enterArea( "entrance" ) );
    SW_EXPECT_EQUAL( 0, state.revealMap( "forest", scene._areaGraph ) );
    SW_EXPECT_FALSE( scene._areaGraph.isDiscovered( "vault" ) );
    SW_EXPECT_TRUE( state.openTreasure( "forest", "mapChest", flags, reward ) );
    SW_EXPECT_EQUAL( 3, state.revealMap( "forest", scene._areaGraph ) );
    SW_EXPECT_TRUE( scene._areaGraph.isDiscovered( "vault" ) );
    SW_EXPECT_FALSE( scene._areaGraph.isDiscovered( "lake" ) );

    // 나침반 — 아직 열지 않은 상자만.
    vector<const AdventureTreasureDef*> listMarker;
    state.collectCompassMarker( "forest", flags, listMarker );
    SW_EXPECT_TRUE( listMarker.empty() );
    SW_EXPECT_TRUE( state.openTreasure( "forest", "compassChest", flags, reward ) );
    state.collectCompassMarker( "forest", flags, listMarker );
    SW_ASSERT_TRUE( listMarker.size() == 1 );
    SW_EXPECT_TRUE( listMarker[0]->_id == hashed_string( "pieceChest" ) );
    SW_EXPECT_TRUE( listMarker[0]->_area == hashed_string( "vault" ) );

    vector<AdventureDungeonEvent> listEvent;
    state.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasDungeonEvent( listEvent, AdventureDungeonEventType::DoorOpened, "crack" ) );
    SW_EXPECT_TRUE( hasDungeonEvent( listEvent, AdventureDungeonEventType::TreasureOpened, "bigChest" ) );
}

/**
 * @brief [ActionAdventureTest] 장치 — 스위치는 남고, 눌림판은 내려오면 꺼지며(걸쇠면 남는다), 시간제 스위치는 시간이 지나면 닫히고, 횃불은 시간 안에 다 켜야 풀린다
 */
SW_TEST_CASE( ActionAdventureTest, DungeonDevicesDriveFlags )
{
    AdventureDungeonScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    AdventureDungeonState& state = scene._state;
    GameFlags&             flags = scene._flags;

    SW_EXPECT_TRUE( state.hitSwitch( "forest", "eye", flags ) );
    scene.run( 10.0f );
    SW_EXPECT_TRUE( flags.hasFlag( "forest.gate" ) );
    SW_EXPECT_FALSE( state.hitSwitch( "forest", "plate", flags ) ); // 눌림판은 칠 수 없다

    state.setPlatePressed( "forest", "plate", true, flags );
    SW_EXPECT_TRUE( flags.hasFlag( "device.plate" ) );
    state.setPlatePressed( "forest", "plate", false, flags );
    SW_EXPECT_FALSE( flags.hasFlag( "device.plate" ) );
    state.setPlatePressed( "forest", "heavyPlate", true, flags );
    state.setPlatePressed( "forest", "heavyPlate", false, flags );
    SW_EXPECT_TRUE( flags.hasFlag( "device.heavyPlate" ) );

    SW_EXPECT_TRUE( state.hitSwitch( "forest", "timer", flags ) );
    scene.run( 2.8f );
    SW_EXPECT_TRUE( flags.hasFlag( "device.timer" ) );
    scene.run( 0.3f );
    SW_EXPECT_FALSE( flags.hasFlag( "device.timer" ) ); // 문이 닫혔다

    // 횃불 셋을 5 초 안에 — 둘만 켜면 모두 꺼진다.
    SW_EXPECT_TRUE( state.lightTorch( "forest", "torches", flags ) );
    SW_EXPECT_TRUE( state.lightTorch( "forest", "torches", flags ) );
    SW_EXPECT_EQUAL( 2, state.getLitTorchCount( "forest", "torches" ) );
    scene.run( 5.1f );
    SW_EXPECT_EQUAL( 0, state.getLitTorchCount( "forest", "torches" ) );
    SW_EXPECT_FALSE( flags.hasFlag( "device.torches" ) );
    for ( int32 torch = 0; torch < 3; ++torch )
    {
        SW_EXPECT_TRUE( state.lightTorch( "forest", "torches", flags ) );
        scene.run( 1.0f );
    }
    SW_EXPECT_TRUE( state.isDeviceActive( "forest", "torches" ) );
    SW_EXPECT_TRUE( flags.hasFlag( "device.torches" ) );
    SW_EXPECT_FALSE( state.lightTorch( "forest", "torches", flags ) );
    scene.run( 10.0f );
    SW_EXPECT_TRUE( flags.hasFlag( "device.torches" ) ); // 풀린 횃불은 꺼지지 않는다

    vector<AdventureDungeonEvent> listEvent;
    state.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasDungeonEvent( listEvent, AdventureDungeonEventType::TorchesFailed, "torches" ) );
    SW_EXPECT_TRUE( hasDungeonEvent( listEvent, AdventureDungeonEventType::DeviceDeactivated, "timer" ) );
}

/**
 * @brief [ActionAdventureTest] 플래그를 적지 않은 상자 · 장치 · 문은 `<종류>.<id>` 를 플래그로 쓴다 — 공유 플래그에서 다른 키트의 같은 id 와 갈린다
 */
SW_TEST_CASE( ActionAdventureTest, DefaultFlagNamesCarryTheirKind )
{
    AdventureDungeonScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    SW_ASSERT_FALSE( scene._catalog.getDungeons().empty() );
    const AdventureDungeonDef&  dungeon = scene._catalog.getDungeons().front();
    const AdventureTreasureDef* pChest  = dungeon.findTreasure( "keyChest" );
    SW_ASSERT_NOT_NULL( pChest );
    SW_EXPECT_TRUE( pChest->_flag == hashed_string( "treasure.keyChest" ) );
    const int32 plateIndex = dungeon.findDeviceIndex( "plate" );
    SW_ASSERT_TRUE( plateIndex >= 0 );
    SW_EXPECT_TRUE( dungeon._listDevice[static_cast<size_t>( plateIndex )]._flag == hashed_string( "device.plate" ) );
    const AdventureDoorDef* pDoor = dungeon.findDoor( "cellDoor" );
    SW_ASSERT_NOT_NULL( pDoor );
    SW_EXPECT_TRUE( pDoor->_flag == hashed_string( "forest.cell" ) ); // 적은 이름은 그대로
}

/**
 * @brief [ActionAdventureTest] 몸 — 하트 조각 넷이 그릇 하나(가득 참), 두 배 마법, 오르다 바닥나면 떨어지고 탈진 동안은 스태미나를 못 쓰며, 헤엄치다 바닥나면 빠져 피해를 입는다
 */
SW_TEST_CASE( ActionAdventureTest, HeartsMagicAndStamina )
{
    AdventureVitalsSettings settings;
    settings._staminaVesselMax = 40.0f;
    AdventureVitals vitals;
    vitals.initialize( settings );
    SW_EXPECT_EQUAL( 12, vitals.getMaxHealthQuarters() );
    SW_EXPECT_FALSE( vitals.applyDamage( 5 ) );
    SW_EXPECT_EQUAL( 7, vitals.computeHealthQuarters() );
    for ( int32 piece = 0; piece < 3; ++piece )
    {
        SW_EXPECT_FALSE( vitals.addHeartPiece() );
    }
    SW_EXPECT_EQUAL( 3, vitals.getHeartPieceCount() );
    SW_EXPECT_TRUE( vitals.addHeartPiece() );
    SW_EXPECT_EQUAL( 4, vitals.getHeartCount() );
    SW_EXPECT_EQUAL( 0, vitals.getHeartPieceCount() );
    SW_EXPECT_EQUAL( 16, vitals.computeHealthQuarters() ); // 그릇은 가득 채운다

    SW_EXPECT_TRUE( vitals.trySpendMagic( 40.0f ) );
    SW_EXPECT_FALSE( vitals.trySpendMagic( 10.0f ) );
    SW_EXPECT_TRUE( vitals.upgradeMagic() );
    SW_EXPECT_FALSE( vitals.upgradeMagic() );
    SW_EXPECT_NEAR_EQUAL( 96.0f, vitals.getMagic().getMax(), 0.001f );

    // 오르기 10/초 · 100 — 열 번째 초에 바닥나 떨어진다.
    for ( int32 second = 0; second < 9; ++second )
    {
        SW_EXPECT_TRUE( vitals.updateStamina( AdventureStaminaAction::Climb, 1.0f ) == AdventureStaminaOutcome::Continue );
    }
    SW_EXPECT_TRUE( vitals.updateStamina( AdventureStaminaAction::Climb, 1.0f ) == AdventureStaminaOutcome::Fall );
    SW_EXPECT_TRUE( vitals.getStamina().isExhausted() );
    SW_EXPECT_TRUE( vitals.updateStamina( AdventureStaminaAction::Sprint, 0.1f ) == AdventureStaminaOutcome::Stop );
    SW_EXPECT_TRUE( vitals.updateStamina( AdventureStaminaAction::Glide, 0.1f ) == AdventureStaminaOutcome::Fall );
    for ( int32 tick = 0; tick < 15; ++tick )
    {
        (void)vitals.updateStamina( AdventureStaminaAction::Idle, 0.1f );
    }
    SW_EXPECT_TRUE( vitals.getStamina().isExhausted() ); // 반쯤 찼지만 아직 붉은 바퀴
    SW_EXPECT_FALSE( vitals.trySpendStamina( 5.0f ) );
    for ( int32 tick = 0; tick < 25; ++tick )
    {
        (void)vitals.updateStamina( AdventureStaminaAction::Idle, 0.1f );
    }
    SW_EXPECT_FALSE( vitals.getStamina().isExhausted() );
    SW_EXPECT_TRUE( vitals.trySpendStamina( 5.0f ) );

    // 헤엄 8/초 — 바닥나면 빠져 하트 하나를 잃고 스태미나는 다시 찬다.
    vitals.initialize( settings );
    AdventureStaminaOutcome outcome = AdventureStaminaOutcome::Continue;
    int32                   tick    = 0;
    for ( ; tick < 100 && outcome == AdventureStaminaOutcome::Continue; ++tick )
    {
        outcome = vitals.updateStamina( AdventureStaminaAction::Swim, 1.0f );
    }
    SW_EXPECT_TRUE( outcome == AdventureStaminaOutcome::Drown );
    SW_EXPECT_EQUAL( 13, tick );
    SW_EXPECT_EQUAL( 8, vitals.computeHealthQuarters() );
    SW_EXPECT_NEAR_EQUAL( 100.0f, vitals.getStamina().getValue(), 0.001f );

    SW_EXPECT_TRUE( vitals.addStaminaVessel() );
    SW_EXPECT_TRUE( vitals.addStaminaVessel() );
    SW_EXPECT_FALSE( vitals.addStaminaVessel() ); // 최대 보너스 40
    SW_EXPECT_NEAR_EQUAL( 140.0f, vitals.getStamina().getMax(), 0.001f );
    SW_EXPECT_TRUE( vitals.applyDamage( 100 ) );
    SW_EXPECT_TRUE( vitals.isDead() );
}

/**
 * @brief [ActionAdventureTest] 주목 — 잡은 대상 쪽이 앞이 되어 옆 입력이 대상을 도는 옆걸음, 점프 + 뒤는 공중제비(그동안 입력 무시 · 무적), 멀어지면 평행 주목
 */
SW_TEST_CASE( ActionAdventureTest, TargetingStrafeAndBackflip )
{
    AdventureTargeting targeting;
    targeting.initialize( AdventureTargetingSettings{} );
    vector<LockOnCandidate> listCandidate;
    LockOnCandidate         moblin;
    moblin._id       = 7;
    moblin._position = float3{ 5.0f, 0.0f, 0.0f };
    listCandidate.push_back( moblin );
    const float3 eye{};
    const float3 lookEast{ 1.0f, 0.0f, 0.0f };

    vector<LockOnCandidate> listEmpty;
    SW_EXPECT_FALSE( targeting.press( eye, lookEast, listEmpty ) );
    SW_EXPECT_TRUE( targeting.getState() == AdventureTargetingState::Parallel );
    targeting.release();
    SW_EXPECT_TRUE( targeting.getState() == AdventureTargetingState::Free );

    SW_ASSERT_TRUE( targeting.press( eye, lookEast, listCandidate ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 7 ), targeting.getTarget() );
    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{ 1.0f, 0.0f }, false, 0.1f ) == AdventureTargetingState::StrafeRight );
    // 대상이 동쪽이면 오른쪽 옆걸음은 남쪽(−Z) — 카메라 앞(+Z)은 보지 않는다.
    const float3 strafe = targeting.computeMoveDirection( eye, float3{ 0.0f, 0.0f, 1.0f }, float2{ 1.0f, 0.0f } );
    SW_EXPECT_NEAR_EQUAL( 0.0f, strafe._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, strafe._z, 0.001f );

    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{ 0.0f, 0.0f }, true, 0.1f ) == AdventureTargetingState::Locked ); // 점프만 — 회피가 아니다
    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{ 0.0f, -1.0f }, true, 0.1f ) == AdventureTargetingState::Backflip );
    SW_EXPECT_TRUE( targeting.isInvulnerable() );
    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{ 1.0f, 0.0f }, false, 0.3f ) == AdventureTargetingState::Backflip );
    SW_EXPECT_FALSE( targeting.isInvulnerable() );
    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{ 1.0f, 0.0f }, false, 0.4f ) == AdventureTargetingState::StrafeRight );
    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{ -1.0f, 0.2f }, true, 0.1f ) == AdventureTargetingState::SideHopLeft );
    SW_EXPECT_TRUE( targeting.isEvading() );
    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{}, false, 0.5f ) == AdventureTargetingState::Locked );

    listCandidate[0]._position = float3{ 40.0f, 0.0f, 0.0f }; // 끊는 거리 밖
    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{}, false, 0.1f ) == AdventureTargetingState::Parallel );
    SW_EXPECT_FALSE( targeting.hasTarget() );
    targeting.release();
    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{ 1.0f, 0.0f }, false, 0.1f ) == AdventureTargetingState::Free );
}

/**
 * @brief [ActionAdventureTest] 불 — 바람 쪽으로만 번지고 풀은 타서 사라지며, 타는 풀에는 상승 기류가 있고, 열이 얼음을 녹인다. 바람을 없애면 사방으로 번진다(규칙을 끄면 진다)
 */
SW_TEST_CASE( ActionAdventureTest, FireSpreadsDownwindAndBurnsOut )
{
    AdventureElementGrid grid;
    fillMeadow( grid, int2{ 1, 0 } );
    SW_EXPECT_TRUE( grid.applyFire( int2{ 3, 0 } ) ); // 얼음에 불 — 녹는다
    SW_EXPECT_TRUE( grid.getMaterial( int2{ 3, 0 } ) == AdventureMaterial::Water );
    grid.setMaterial( int2{ 3, 0 }, AdventureMaterial::Ice );
    SW_ASSERT_TRUE( grid.applyFire( int2{ 3, 1 } ) );
    SW_EXPECT_TRUE( grid.hasUpdraft( int2{ 3, 1 } ) );
    grid.step();
    SW_EXPECT_TRUE( grid.isBurning( int2{ 4, 1 } ) );
    SW_EXPECT_FALSE( grid.isBurning( int2{ 2, 1 } ) );
    SW_EXPECT_TRUE( grid.getMaterial( int2{ 3, 0 } ) == AdventureMaterial::Water ); // 이웃 얼음이 녹았다
    for ( int32 stepIndex = 0; stepIndex < 20; ++stepIndex )
    {
        grid.step();
    }
    SW_EXPECT_EQUAL( 0, grid.countBurning() );
    SW_EXPECT_TRUE( grid.getMaterial( int2{ 7, 1 } ) == AdventureMaterial::Empty );
    SW_EXPECT_TRUE( grid.getMaterial( int2{ 3, 1 } ) == AdventureMaterial::Empty );
    SW_EXPECT_TRUE( grid.getMaterial( int2{ 2, 1 } ) == AdventureMaterial::Grass ); // 바람을 거슬러서는 번지지 않는다
    SW_EXPECT_TRUE( grid.getMaterial( int2{ 3, 2 } ) == AdventureMaterial::Wood );  // 옆바람 쪽 나무도 그대로

    // 같은 조작이면 같은 상태(결정적) — 실시간 update 와 걸음 직접 호출이 같은 결과.
    AdventureElementGrid gridA;
    AdventureElementGrid gridB;
    fillMeadow( gridA, int2{ 1, 0 } );
    fillMeadow( gridB, int2{ 1, 0 } );
    (void)gridA.applyFire( int2{ 0, 1 } ); // 시험 준비 — 결과는 아래 상태 해시 비교가 본다
    (void)gridB.applyFire( int2{ 0, 1 } ); // 시험 준비 — 결과는 아래 상태 해시 비교가 본다
    for ( int32 stepIndex = 0; stepIndex < 6; ++stepIndex )
    {
        gridA.step();
    }
    for ( int32 tick = 0; tick < 12; ++tick )
    {
        (void)gridB.update( 0.125f );
    }
    SW_EXPECT_EQUAL( gridA.getStepCount(), gridB.getStepCount() );
    SW_EXPECT_EQUAL( gridA.computeStateHash(), gridB.computeStateHash() );

    // 바람이 없으면 불은 사방 — 거슬러 오는 쪽 풀도 옆의 나무도 탄다.
    AdventureElementGrid calm;
    fillMeadow( calm, int2{ 0, 0 } );
    (void)calm.applyFire( int2{ 3, 1 } ); // 시험 준비 — 결과는 아래 isBurning 단언이 본다
    calm.step();
    SW_EXPECT_TRUE( calm.isBurning( int2{ 2, 1 } ) );
    SW_EXPECT_TRUE( calm.isBurning( int2{ 3, 2 } ) );
    SW_EXPECT_FALSE( calm.hasUpdraft( int2{ 3, 2 } ) ); // 나무는 기류를 만들지 않는다
    vector<int2> listUpdraft;
    calm.collectUpdraft( listUpdraft );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listUpdraft.size() );
}

/**
 * @brief [ActionAdventureTest] 전기 · 얼음 — 전기는 이어진 금속 · 물로만 전도되고 얼음이 끊으며, 냉기는 물을 얼리고 불을 끈다
 */
SW_TEST_CASE( ActionAdventureTest, ElectricityConductsAndIceBlocks )
{
    AdventureElementGrid grid;
    grid.initialize( 6, 2, AdventureElementSettings{} );
    grid.setMaterial( int2{ 0, 0 }, AdventureMaterial::Metal );
    grid.setMaterial( int2{ 1, 0 }, AdventureMaterial::Metal );
    grid.setMaterial( int2{ 2, 0 }, AdventureMaterial::Water );
    grid.setMaterial( int2{ 3, 0 }, AdventureMaterial::Water );
    grid.setMaterial( int2{ 5, 0 }, AdventureMaterial::Metal );
    grid.setMaterial( int2{ 2, 1 }, AdventureMaterial::Ice );
    grid.setMaterial( int2{ 0, 1 }, AdventureMaterial::Grass );

    SW_EXPECT_EQUAL( 4, grid.applyElectric( int2{ 0, 0 } ) );
    SW_EXPECT_TRUE( grid.isCharged( int2{ 3, 0 } ) );
    SW_EXPECT_FALSE( grid.isCharged( int2{ 5, 0 } ) ); // 맨땅이 끊었다
    SW_EXPECT_FALSE( grid.isCharged( int2{ 2, 1 } ) ); // 얼음은 전기를 막는다
    SW_EXPECT_FALSE( grid.isCharged( int2{ 0, 1 } ) );
    grid.step();
    SW_EXPECT_TRUE( grid.isCharged( int2{ 0, 0 } ) );
    grid.step();
    SW_EXPECT_FALSE( grid.isCharged( int2{ 0, 0 } ) ); // 두 걸음 뒤 사라진다

    SW_EXPECT_TRUE( grid.applyIce( int2{ 2, 0 } ) );
    SW_EXPECT_TRUE( grid.getMaterial( int2{ 2, 0 } ) == AdventureMaterial::Ice );
    SW_EXPECT_EQUAL( 2, grid.applyElectric( int2{ 0, 0 } ) ); // 언 물이 사슬을 끊었다
    SW_EXPECT_FALSE( grid.isCharged( int2{ 3, 0 } ) );

    SW_EXPECT_FALSE( grid.applyFire( int2{ 1, 0 } ) ); // 금속은 타지 않는다
    SW_EXPECT_TRUE( grid.applyFire( int2{ 0, 1 } ) );
    SW_EXPECT_TRUE( grid.applyIce( int2{ 0, 1 } ) ); // 불이 꺼진다
    SW_EXPECT_FALSE( grid.isBurning( int2{ 0, 1 } ) );
    SW_EXPECT_TRUE( grid.getMaterial( int2{ 0, 1 } ) == AdventureMaterial::Grass );
    vector<AdventureElementEvent> listEvent;
    grid.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._type == AdventureElementEventType::Extinguished );
}

/**
 * @brief [ActionAdventureTest] 요리 — 같은 효과 재료는 세기 · 시간이 더해져 단계가 오르고, 다른 효과가 섞이면 수상한 요리, 재료가 꼭 같은 레시피는 기반 제작의 이름 요리
 */
SW_TEST_CASE( ActionAdventureTest, CookingSumsSameEffectAndSpoilsMixed )
{
    AdventureCooking cooking;
    SW_ASSERT_TRUE( cooking.loadFromXmlText( kAdventureCookingXml, "ActionAdventureTest" ) );
    AdventureDish dish;

    vector<hashed_string> listPot{ "hydromelon", "hydromelon", "hydromelon", "wildberry" };
    SW_ASSERT_TRUE( cooking.evaluate( listPot, dish ) == AdventureCookResult::Ok );
    SW_EXPECT_TRUE( dish._effect == hashed_string( "Chilly" ) );
    SW_EXPECT_EQUAL( 1, dish._effectTier );
    SW_EXPECT_NEAR_EQUAL( 570.0f, dish._duration, 0.01f ); // 150 × 3 + 30 × 4
    SW_EXPECT_EQUAL( 16, dish._heartQuarters );            // (2 × 3 + 2) × 2

    listPot = { "hydromelon", "hydromelon", "chillshroom" };
    SW_ASSERT_TRUE( cooking.evaluate( listPot, dish ) == AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( 2, dish._effectTier ); // 세기 1 + 1 + 2 = 4

    // 같은 재료에 다른 효과 하나를 섞으면 효과가 사라진다 — 섞임 규칙이 없으면 2 단계 냉기가 된다.
    listPot = { "hydromelon", "hydromelon", "chillshroom", "spicyPepper" };
    SW_ASSERT_TRUE( cooking.evaluate( listPot, dish ) == AdventureCookResult::Ok );
    SW_EXPECT_TRUE( dish._bDubious == SW_TRUE );
    SW_EXPECT_EQUAL( 0, dish._effectTier );
    SW_EXPECT_TRUE( dish._effect.empty() );
    SW_EXPECT_EQUAL( 4, dish._heartQuarters );
    SW_EXPECT_TRUE( dish._itemId == hashed_string( "dubiousFood" ) );

    listPot = { "wildberry", "wildberry", "wildberry", "wildberry", "wildberry", "wildberry" };
    SW_EXPECT_TRUE( cooking.evaluate( listPot, dish ) == AdventureCookResult::TooManyIngredients );
    listPot = { "rock" };
    SW_EXPECT_TRUE( cooking.evaluate( listPot, dish ) == AdventureCookResult::UnknownIngredient );
    listPot.clear();
    SW_EXPECT_TRUE( cooking.evaluate( listPot, dish ) == AdventureCookResult::EmptyPot );

    ItemCatalog items;
    SW_ASSERT_TRUE( items.loadFromXmlText( kAdventureItemXml, "ActionAdventureTest" ) );
    RecipeCatalog recipes;
    SW_ASSERT_TRUE( recipes.loadFromXmlText( kAdventureRecipeXml, "ActionAdventureTest" ) );
    Crafter crafter;
    crafter.initialize( &recipes );
    Inventory inventory;
    inventory.initialize( &items, 8 );
    SW_EXPECT_EQUAL( 3, inventory.addItem( "hydromelon", 3 ) );
    SW_EXPECT_EQUAL( 1, inventory.addItem( "wildberry", 1 ) );

    listPot = { "hydromelon", "hydromelon" };
    SW_ASSERT_TRUE( cooking.cook( listPot, inventory, crafter, recipes, 0, dish ) == AdventureCookResult::Ok );
    SW_EXPECT_TRUE( dish._bNamedRecipe == SW_TRUE );
    SW_EXPECT_TRUE( dish._itemId == hashed_string( "chillySorbet" ) );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( "chillySorbet" ) );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( "hydromelon" ) );

    listPot = { "hydromelon", "wildberry" };
    SW_ASSERT_TRUE( cooking.cook( listPot, inventory, crafter, recipes, 0, dish ) == AdventureCookResult::Ok );
    SW_EXPECT_TRUE( dish._bNamedRecipe == SW_FALSE );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( "simmeredDish" ) );
    SW_EXPECT_EQUAL( 0, inventory.getItemCount( "hydromelon" ) );
    SW_EXPECT_TRUE( cooking.cook( listPot, inventory, crafter, recipes, 0, dish ) == AdventureCookResult::MissingIngredients );
}

/**
 * @brief [ActionAdventureTest] 무기 내구도 · 탑 · 사당 — 닳다 경고하고 부서지는 한 방은 두 배, 탑은 지역 지도를 드러내고, 사당 증표 넷이 하트 그릇 하나
 */
SW_TEST_CASE( ActionAdventureTest, WeaponWearTowersAndShrines )
{
    ItemCatalog items;
    SW_ASSERT_TRUE( items.loadFromXmlText( kAdventureItemXml, "ActionAdventureTest" ) );
    Inventory inventory;
    inventory.initialize( &items, 4 );
    SW_ASSERT_TRUE( inventory.addItem( "treeBranch", 1 ) == 1 );
    SW_ASSERT_TRUE( inventory.addItem( "masterSword", 1 ) == 1 );
    const int32         branchSlot = inventory.findFirstSlot( "treeBranch" );
    const int32         swordSlot  = inventory.findFirstSlot( "masterSword" );
    AdventureWeaponWear wear;

    AdventureStrikeResult strike = wear.strike( inventory, branchSlot, 10.0f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, strike._damage, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, strike._durabilityLeft, 0.001f );
    SW_EXPECT_TRUE( strike._bWarning == SW_FALSE );
    strike = wear.strike( inventory, branchSlot, 10.0f );
    strike = wear.strike( inventory, branchSlot, 10.0f );
    SW_EXPECT_TRUE( strike._bWarning == SW_TRUE ); // 1 / 4 남았다
    strike = wear.strike( inventory, branchSlot, 10.0f );
    SW_EXPECT_TRUE( strike._bBroke == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 20.0f, strike._damage, 0.001f );
    SW_EXPECT_EQUAL( 0, inventory.getItemCount( "treeBranch" ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, wear.strike( inventory, branchSlot, 10.0f )._damage, 0.001f ); // 빈 손
    for ( int32 hit = 0; hit < 50; ++hit )
    {
        (void)wear.strike( inventory, swordSlot, 30.0f );
    }
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( "masterSword" ) ); // 내구도 없는 검은 닳지 않는다

    AreaGraph areaGraph;
    SW_ASSERT_TRUE( areaGraph.loadFromXmlText( kAdventureAreaXml, "ActionAdventureTest" ) );
    AdventureWorldMap worldMap;
    SW_ASSERT_TRUE( worldMap.loadFromXmlText( R"(<AdventureWorld orbsPerExchange="4"><Tower id="plateauTower" region="Plateau"/>
        <Shrine id="s1" region="Plateau"/><Shrine id="s2"/><Shrine id="s3"/><Shrine id="s4"/></AdventureWorld>)",
                                              "ActionAdventureTest" ) );
    SW_EXPECT_EQUAL( 2, worldMap.activateTower( "plateauTower", areaGraph ) );
    SW_EXPECT_TRUE( areaGraph.isDiscovered( "ruins" ) );
    SW_EXPECT_EQUAL( -1, worldMap.activateTower( "plateauTower", areaGraph ) );
    SW_EXPECT_EQUAL( -1, worldMap.activateTower( "s1", areaGraph ) ); // 사당은 탑이 아니다
    SW_EXPECT_TRUE( worldMap.canWarpTo( "plateauTower" ) );
    SW_EXPECT_FALSE( worldMap.canWarpTo( "s1" ) );
    SW_EXPECT_TRUE( worldMap.discoverShrine( "s1" ) );
    SW_EXPECT_TRUE( worldMap.canWarpTo( "s1" ) );

    AdventureVitals vitals;
    vitals.initialize( AdventureVitalsSettings{} );
    SW_EXPECT_TRUE( worldMap.completeShrine( "s1" ) );
    SW_EXPECT_FALSE( worldMap.completeShrine( "s1" ) ); // 같은 사당은 한 번
    SW_EXPECT_TRUE( worldMap.completeShrine( "s2" ) );
    SW_EXPECT_TRUE( worldMap.completeShrine( "s3" ) );
    SW_EXPECT_TRUE( worldMap.exchangeOrbs( AdventureOrbReward::HeartContainer, vitals ) == AdventureExchangeResult::NotEnoughOrbs );
    SW_EXPECT_TRUE( worldMap.completeShrine( "s4" ) );
    SW_EXPECT_EQUAL( 4, worldMap.getOrbCount() );
    SW_EXPECT_TRUE( worldMap.exchangeOrbs( AdventureOrbReward::HeartContainer, vitals ) == AdventureExchangeResult::Ok );
    SW_EXPECT_EQUAL( 0, worldMap.getOrbCount() );
    SW_EXPECT_EQUAL( 4, vitals.getHeartCount() );
    SW_EXPECT_EQUAL( 4, worldMap.getCompletedShrineCount() );
}

/**
 * @brief [ActionAdventureTest] 상태 바이트 — 던전(열쇠 · 시간제 스위치 · 횃불) · 원소 격자 · 주목 · 몸이 그대로 오고, 같은 걸음을 더 돌려도 바이트가 같다.
 *        잘린 바이트 · 크기가 다른 격자는 거절하고 그대로 둔다
 */
SW_TEST_CASE( ActionAdventureTest, StateRoundTripContinuesTheSameAdventure )
{
    // 던전 — 열쇠 셋, 시간제 스위치가 2 초 남고 횃불 둘이 켜져 있다.
    AdventureDungeonScene dungeon;
    SW_ASSERT_TRUE( dungeon.initialize() );
    ItemStackList reward;
    SW_EXPECT_TRUE( dungeon._state.openTreasure( "forest", "keyChest", dungeon._flags, reward ) );
    SW_EXPECT_TRUE( dungeon._state.addSmallKey( "forest", 2 ) );
    SW_EXPECT_TRUE( dungeon._state.hitSwitch( "forest", "timer", dungeon._flags ) );
    SW_EXPECT_TRUE( dungeon._state.lightTorch( "forest", "torches", dungeon._flags ) );
    SW_EXPECT_TRUE( dungeon._state.lightTorch( "forest", "torches", dungeon._flags ) );
    dungeon.run( 1.0f );
    const vector<uint8>   dungeonBytes = captureAdventureBytes( dungeon._state );
    AdventureDungeonScene restoredDungeon;
    SW_ASSERT_TRUE( restoredDungeon.initialize() );
    SW_ASSERT_TRUE( restoreAdventureBytes( dungeonBytes, restoredDungeon._state ) );
    SW_EXPECT_EQUAL( 3, restoredDungeon._state.findProgress( "forest" )->_smallKeyCount );
    SW_EXPECT_EQUAL( 2, restoredDungeon._state.getLitTorchCount( "forest", "torches" ) );
    SW_EXPECT_TRUE( restoredDungeon._state.isDeviceActive( "forest", "timer" ) );
    SW_EXPECT_TRUE( dungeonBytes == captureAdventureBytes( restoredDungeon._state ) );
    dungeon.run( 2.5f );
    restoredDungeon.run( 2.5f );
    SW_EXPECT_FALSE( restoredDungeon._state.isDeviceActive( "forest", "timer" ) ); // 남은 2 초가 이어져 닫혔다
    SW_EXPECT_TRUE( captureAdventureBytes( dungeon._state ) == captureAdventureBytes( restoredDungeon._state ) );
    AdventureDungeonScene truncatedDungeon;
    SW_ASSERT_TRUE( truncatedDungeon.initialize() );
    SW_EXPECT_FALSE( restoreTruncatedAdventureBytes( dungeonBytes, truncatedDungeon._state ) );
    SW_EXPECT_EQUAL( 0, truncatedDungeon._state.findProgress( "forest" )->_smallKeyCount );

    // 원소 격자 — 불이 번지는 중간. 읽는 쪽은 바람 없는 초원에서 시작해도 바람까지 이어받는다.
    AdventureElementGrid grid;
    fillMeadow( grid, int2{ 1, 0 } );
    SW_ASSERT_TRUE( grid.applyFire( int2{ 0, 1 } ) );
    for ( int32 stepIndex = 0; stepIndex < 3; ++stepIndex )
    {
        grid.step();
    }
    const vector<uint8>  gridBytes = captureAdventureBytes( grid );
    AdventureElementGrid restoredGrid;
    fillMeadow( restoredGrid, int2{ 0, 0 } );
    SW_ASSERT_TRUE( restoreAdventureBytes( gridBytes, restoredGrid ) );
    SW_EXPECT_EQUAL( grid.computeStateHash(), restoredGrid.computeStateHash() );
    SW_EXPECT_EQUAL( 1, restoredGrid.getWind()._x );
    SW_EXPECT_TRUE( gridBytes == captureAdventureBytes( restoredGrid ) );
    for ( int32 stepIndex = 0; stepIndex < 3; ++stepIndex )
    {
        grid.step();
        restoredGrid.step();
    }
    SW_EXPECT_TRUE( captureAdventureBytes( grid ) == captureAdventureBytes( restoredGrid ) );
    AdventureElementGrid smallerGrid;
    smallerGrid.initialize( 4, 3, AdventureElementSettings{} );
    Archive smallerReader( gridBytes.data(), gridBytes.size() );
    SW_EXPECT_FALSE( smallerGrid.readState( smallerReader ) );
    AdventureElementGrid truncatedGrid;
    fillMeadow( truncatedGrid, int2{ 0, 0 } );
    SW_EXPECT_FALSE( restoreTruncatedAdventureBytes( gridBytes, truncatedGrid ) );
    SW_EXPECT_EQUAL( 0, truncatedGrid.countBurning() );

    // 주목 — 공중제비 한가운데(회피 · 무적이 남았다).
    AdventureTargeting      targeting;
    vector<LockOnCandidate> listCandidate;
    LockOnCandidate         moblin;
    moblin._id       = 7;
    moblin._position = float3{ 5.0f, 0.0f, 0.0f };
    listCandidate.push_back( moblin );
    const float3 eye{};
    targeting.initialize( AdventureTargetingSettings{} );
    SW_ASSERT_TRUE( targeting.press( eye, float3{ 1.0f, 0.0f, 0.0f }, listCandidate ) );
    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{ 1.0f, 0.0f }, false, 0.1f ) == AdventureTargetingState::StrafeRight );
    SW_EXPECT_TRUE( targeting.update( eye, listCandidate, float2{ 0.0f, -1.0f }, true, 0.1f ) == AdventureTargetingState::Backflip );
    const vector<uint8> targetingBytes = captureAdventureBytes( targeting );
    AdventureTargeting  restoredTargeting;
    restoredTargeting.initialize( AdventureTargetingSettings{} );
    SW_ASSERT_TRUE( restoreAdventureBytes( targetingBytes, restoredTargeting ) );
    SW_EXPECT_TRUE( restoredTargeting.getState() == AdventureTargetingState::Backflip );
    SW_EXPECT_EQUAL( static_cast<uint64>( 7 ), restoredTargeting.getTarget() );
    SW_EXPECT_TRUE( restoredTargeting.isInvulnerable() );
    SW_EXPECT_TRUE( restoredTargeting.isHeld() );
    SW_EXPECT_TRUE( targetingBytes == captureAdventureBytes( restoredTargeting ) );
    for ( int32 tick = 0; tick < 2; ++tick )
    {
        (void)targeting.update( eye, listCandidate, float2{ 1.0f, 0.0f }, false, 0.3f );
        (void)restoredTargeting.update( eye, listCandidate, float2{ 1.0f, 0.0f }, false, 0.3f );
    }
    SW_EXPECT_TRUE( targeting.getState() == restoredTargeting.getState() );
    SW_EXPECT_TRUE( captureAdventureBytes( targeting ) == captureAdventureBytes( restoredTargeting ) );
    AdventureTargeting truncatedTargeting;
    truncatedTargeting.initialize( AdventureTargetingSettings{} );
    SW_EXPECT_FALSE( restoreTruncatedAdventureBytes( targetingBytes, truncatedTargeting ) );
    SW_EXPECT_TRUE( truncatedTargeting.getState() == AdventureTargetingState::Free );

    // 몸 — 하트 넷 + 조각 하나, 두 배 마법을 조금 썼고, 오르다 지구력이 줄었다.
    AdventureVitals vitals;
    vitals.initialize( AdventureVitalsSettings{} );
    for ( int32 piece = 0; piece < 5; ++piece )
    {
        (void)vitals.addHeartPiece();
    }
    SW_EXPECT_FALSE( vitals.applyDamage( 3 ) );
    SW_EXPECT_TRUE( vitals.upgradeMagic() );
    SW_EXPECT_TRUE( vitals.trySpendMagic( 30.0f ) );
    for ( int32 second = 0; second < 3; ++second )
    {
        (void)vitals.updateStamina( AdventureStaminaAction::Climb, 1.0f );
    }
    const vector<uint8> vitalsBytes = captureAdventureBytes( vitals );
    AdventureVitals     restoredVitals;
    restoredVitals.initialize( AdventureVitalsSettings{} );
    SW_ASSERT_TRUE( restoreAdventureBytes( vitalsBytes, restoredVitals ) );
    SW_EXPECT_EQUAL( 4, restoredVitals.getHeartCount() );
    SW_EXPECT_EQUAL( 1, restoredVitals.getHeartPieceCount() );
    SW_EXPECT_EQUAL( 13, restoredVitals.computeHealthQuarters() );
    SW_EXPECT_EQUAL( 16, restoredVitals.getMaxHealthQuarters() );
    SW_EXPECT_NEAR_EQUAL( 96.0f, restoredVitals.getMagic().getMax(), 0.001f );
    SW_EXPECT_TRUE( vitalsBytes == captureAdventureBytes( restoredVitals ) );
    for ( int32 tick = 0; tick < 4; ++tick )
    {
        (void)vitals.updateStamina( AdventureStaminaAction::Idle, 0.5f );
        (void)restoredVitals.updateStamina( AdventureStaminaAction::Idle, 0.5f );
    }
    SW_EXPECT_TRUE( captureAdventureBytes( vitals ) == captureAdventureBytes( restoredVitals ) );
    AdventureVitals truncatedVitals;
    truncatedVitals.initialize( AdventureVitalsSettings{} );
    SW_EXPECT_FALSE( restoreTruncatedAdventureBytes( vitalsBytes, truncatedVitals ) );
    SW_EXPECT_EQUAL( 3, truncatedVitals.getHeartCount() );
}
