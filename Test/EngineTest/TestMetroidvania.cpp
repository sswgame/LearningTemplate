// 메트로배니아 · 2D 소울라이크 키트 — 능력 → 몸 설정 · 길 잠금, 지도 구매 · 아이템 표시 · 빠른 이동, 시체 · 영구 손실, 휴식 · 물약 · 적 부활 · 보스, 패리 · 막기 · 강인도, 부적 슬롯.
#include "pch.h"

#include "GameFramework/Data/StatBlock.h"
#include "GameFramework/Inventory/ItemBag.h"
#include "GameFramework/Inventory/LootTable.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroAbilitySet.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroCharmLoadout.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroDuelist.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroMapState.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroSoulsState.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroidvaniaCatalog.h"
#include "GameFramework/Movement/PlatformerMotor2D.h"
#include "GameFramework/Utility/GameRandom.h"
#include "GameFramework/World/AreaGraph.h"
#include "GameFramework/World/GameFlags.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr float32 kStep = 1.0f / 60.0f;

    constexpr const utf8* kCatalogXml = R"(
<Metroidvania currency="geo">
  <Rules flaskCharges="2" flaskMaxCharges="3" flaskHeal="30" flaskHealPerUpgrade="10" riposteMultiplier="3" riposteTime="1.0"
         attackStamina="20" dodgeStamina="25" guardStaminaPerDamage="1" guardChip="0.1" corpseRadius="1.5" charmNotches="3" overcharm="true" overcharmDamageScale="2"/>
  <Health max="100" poise="30" poiseRegenRate="0"/>
  <Stamina max="100" regenRate="50" regenDelay="0.5"/>
  <Parry><Window grade="Perfect" early="0.04" late="0.02"/><Window grade="Parry" early="0.12" late="0.04"/></Parry>
  <Ability id="dash" name="Mothwing Cloak" flag="has_dash"><Motor dash="1" airDashCount="1"/></Ability>
  <Ability id="doubleJump" name="Monarch Wings"><Motor extraJumpCount="1"/></Ability>
  <Ability id="wallJump" name="Mantis Claw"><Motor wallJump="1"/></Ability>
  <Ability id="grapple" name="Grapple"/>
  <Charm id="strength" cost="2"><Stats damage="0.5"/></Charm>
  <Charm id="quickSlash" cost="2"><Stats attackSpeed="0.3"/></Charm>
  <Charm id="compass" cost="1"><Stats mapPin="1"/></Charm>
  <Map region="crossroads" price="30"/>
  <Map region="greenpath" price="60"/>
  <Site id="bench_town" area="town" rest="true"/>
  <Site id="stag_town" area="town" fastTravel="true"/>
  <Site id="stag_cross" area="cross2" fastTravel="true"/>
  <Pickup id="mask_cross3" area="cross3"/>
  <Pickup id="mask_cross1" area="cross1"/>
  <Pickup id="wings" area="green1" ability="doubleJump"/>
  <Enemy id="husk" currency="5" loot="husk"/>
  <Enemy id="falseKnight" boss="true" currency="200"/>
</Metroidvania>
)";

    constexpr const utf8* kAreaXml = R"(
<AreaGraph>
  <Area id="town" region="town"/>
  <Area id="cross1" region="crossroads"/>
  <Area id="cross2" region="crossroads"/>
  <Area id="cross3" region="crossroads"/>
  <Area id="green1" region="greenpath"/>
  <Link from="town" to="cross1"/>
  <Link from="cross1" to="cross2"/>
  <Link from="cross2" to="cross3"/>
  <Link from="cross1" to="green1" requires="has_dash"/>
</AreaGraph>
)";

    constexpr const utf8* kLootXml = R"(
<LootCatalog><Table id="husk" rolls="1" none="1"><Entry item="shard" weight="1" min="1" max="3"/></Table></LootCatalog>
)";

    //         x: 0123456789012345678901
    constexpr const utf8* kLevelText = R"(
######################
#                    #
#                    #
#                    #
#                    #
#                    #
#                    #
#                    #
######################
)";

    struct MetroScene
    {
        MetroidvaniaCatalog _catalog;
        AreaGraph           _graph;
        GameFlags           _flags;
        bool                _bLoaded{ false };

        MetroScene() { _bLoaded = _catalog.loadFromXmlText( kCatalogXml, "metro" ) && _graph.loadFromXmlText( kAreaXml, "areas" ); }
    };

    uint32 runMotor( PlatformerMotor2D& motor, const PlatformTileMap& map, const MetroAbilitySet& abilities, const PlatformerInput& input, int32 frameCount )
    {
        uint32 events = 0;
        for ( int32 frame = 0; frame < frameCount; ++frame )
        {
            motor.update( map, abilities.filterInput( input ), kStep );
            events |= motor.getEvents();
        }
        return events;
    }
} // namespace

SW_TEST_CASE( MetroidvaniaTest, AbilitiesRewriteMotorSettingsAndOpenAreaLocks )
{
    MetroScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );

    PlatformTileMap map;
    map.loadFromText( kLevelText, 1.0f, float2{ 0.0f, 0.0f } );
    MetroAbilitySet abilities;
    abilities.initialize( &scene._catalog, PlatformerSettings{} );
    PlatformerMotor2D motor;
    abilities.applyToMotor( motor );

    // 잠긴 기본 몸 — 벽 점프 · 공중 점프 · 대시가 없다(기반 기본값은 벽 점프 · 공중 대시 1 이다).
    SW_EXPECT_FALSE( motor.getSettings()._bWallJump != SW_FALSE );
    SW_EXPECT_EQUAL( 0, motor.getSettings()._extraJumpCount );
    SW_EXPECT_EQUAL( 0, motor.getSettings()._airDashCount );
    motor.setPosition( float2{ 5.5f, 1.45f } );
    (void)runMotor( motor, map, abilities, PlatformerInput{}, 10 );
    PlatformerInput dash;
    dash._bDashPressed = SW_TRUE;
    SW_EXPECT_FALSE( ( runMotor( motor, map, abilities, dash, 1 ) & PlatformerEvent::kDashed ) != 0 ); // 땅 대시도 막힌다
    SW_EXPECT_FALSE( scene._graph.canTraverse( "cross1", "green1", scene._flags ) );

    // 2단 점프 — 공중 점프가 열린다.
    SW_EXPECT_TRUE( abilities.grantAbility( "doubleJump", scene._flags ) );
    SW_EXPECT_FALSE( abilities.grantAbility( "doubleJump", scene._flags ) );
    SW_EXPECT_FALSE( abilities.grantAbility( "noSuchAbility", scene._flags ) );
    abilities.applyToMotor( motor );
    SW_EXPECT_EQUAL( 1, motor.getSettings()._extraJumpCount );
    PlatformerInput jump;
    jump._bJumpPressed = SW_TRUE;
    jump._bJumpHeld    = SW_TRUE;
    PlatformerInput hold;
    hold._bJumpHeld = SW_TRUE;
    (void)runMotor( motor, map, abilities, PlatformerInput{}, 5 );
    (void)runMotor( motor, map, abilities, jump, 1 );
    (void)runMotor( motor, map, abilities, hold, 15 );
    SW_EXPECT_TRUE( ( runMotor( motor, map, abilities, jump, 1 ) & PlatformerEvent::kAirJumped ) != 0 );

    // 대시 — 플래그(has_dash)가 켜져 길 잠금이 풀리고 몸이 대시한다.
    SW_EXPECT_TRUE( abilities.grantAbility( "dash", scene._flags ) );
    SW_EXPECT_TRUE( scene._flags.hasFlag( "has_dash" ) );
    SW_EXPECT_TRUE( scene._graph.canTraverse( "cross1", "green1", scene._flags ) );
    abilities.applyToMotor( motor );
    SW_EXPECT_EQUAL( 1, motor.getSettings()._airDashCount );
    (void)runMotor( motor, map, abilities, PlatformerInput{}, 60 );
    SW_EXPECT_TRUE( ( runMotor( motor, map, abilities, dash, 1 ) & PlatformerEvent::kDashed ) != 0 );

    // 세이브 — 플래그만으로 같은 능력 · 같은 설정이 되살아난다.
    MetroAbilitySet restored;
    restored.initialize( &scene._catalog, PlatformerSettings{} );
    SW_EXPECT_EQUAL( 2, restored.restoreFromFlags( scene._flags ) );
    PlatformerSettings lhs;
    PlatformerSettings rhs;
    abilities.applyToSettings( lhs );
    restored.applyToSettings( rhs );
    SW_EXPECT_EQUAL( lhs._extraJumpCount, rhs._extraJumpCount );
    SW_EXPECT_EQUAL( lhs._airDashCount, rhs._airDashCount );
    SW_EXPECT_TRUE( restored.canDash() );
    SW_EXPECT_FALSE( restored.hasAbility( "wallJump" ) );

    // 벽 점프 능력은 켜는 칸.
    SW_EXPECT_TRUE( restored.grantAbility( "wallJump", scene._flags ) );
    restored.applyToSettings( rhs );
    SW_EXPECT_TRUE( rhs._bWallJump != SW_FALSE );
}

SW_TEST_CASE( MetroidvaniaTest, RegionMapPurchaseRevealsRoomsAndUnvisitedItemMarkers )
{
    MetroScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    MetroMapState mapState;
    mapState.initialize( &scene._catalog, &scene._graph );

    SW_EXPECT_TRUE( mapState.enterArea( "town" ) );
    SW_EXPECT_TRUE( mapState.enterArea( "cross1" ) );
    SW_EXPECT_FALSE( mapState.enterArea( "cross1" ) );
    // 지도가 없으면 지나온 방도 그려지지 않는다.
    SW_EXPECT_FALSE( mapState.isShownOnMap( "cross1" ) );
    vector<const MetroPickupDef*> listMarker;
    mapState.collectItemMarkers( listMarker );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( listMarker.size() ) );

    // 돈이 모자라면 아무것도 바뀌지 않는다.
    int32 currency = 20;
    SW_EXPECT_TRUE( mapState.buyRegionMap( "crossroads", currency ) == MetroMapPurchase::NotEnoughCurrency );
    SW_EXPECT_EQUAL( 20, currency );
    SW_EXPECT_TRUE( mapState.buyRegionMap( "nowhere", currency ) == MetroMapPurchase::UnknownRegion );
    currency = 50;
    SW_EXPECT_TRUE( mapState.buyRegionMap( "crossroads", currency ) == MetroMapPurchase::Bought );
    SW_EXPECT_EQUAL( 20, currency );
    SW_EXPECT_TRUE( mapState.buyRegionMap( "crossroads", currency ) == MetroMapPurchase::AlreadyOwned );

    // 지역의 방이 모두 그려진다(가 보지 않은 cross3 까지). 다른 지역은 아니다.
    SW_EXPECT_TRUE( mapState.isShownOnMap( "cross1" ) );
    SW_EXPECT_TRUE( mapState.isShownOnMap( "cross3" ) );
    SW_EXPECT_FALSE( mapState.isShownOnMap( "green1" ) );
    // 아이템 표시 — 가 보지 않은 cross3 의 것만(방문한 cross1 의 것, 지도 없는 green1 의 것은 아니다).
    mapState.collectItemMarkers( listMarker );
    SW_ASSERT_TRUE( listMarker.size() == 1 );
    SW_EXPECT_TRUE( listMarker[0]->_id == hashed_string( "mask_cross3" ) );

    // 주우면 표시가 사라지고, 능력을 주는 것은 정의를 돌려준다.
    SW_EXPECT_NOT_NULL( mapState.collectPickup( "mask_cross3" ) );
    SW_EXPECT_NULL( mapState.collectPickup( "mask_cross3" ) );
    mapState.collectItemMarkers( listMarker );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( listMarker.size() ) );
    const MetroPickupDef* pWings = mapState.collectPickup( "wings" );
    SW_ASSERT_NOT_NULL( pWings );
    SW_EXPECT_TRUE( pWings->_ability == hashed_string( "doubleJump" ) );

    // 탐색률 2/5 방, 수집률 2/3, 완료율 (2+2)/(5+3).
    SW_EXPECT_NEAR_EQUAL( 0.4f, mapState.computeExplorationRatio(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f / 3.0f, mapState.computeCollectionRatio(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, mapState.computeCompletionPercent(), 1.0e-3f );

    // 지점 — 가 본 방의 것만 열리고, 빠른 이동은 열린 정거장끼리만.
    SW_EXPECT_TRUE( mapState.activateSite( "stag_town" ) );
    SW_EXPECT_FALSE( mapState.activateSite( "stag_cross" ) ); // cross2 를 아직 가 보지 않았다
    SW_EXPECT_FALSE( mapState.canFastTravel( "stag_town", "stag_cross" ) );
    SW_EXPECT_TRUE( mapState.enterArea( "cross2" ) );
    SW_EXPECT_TRUE( mapState.activateSite( "stag_cross" ) );
    SW_EXPECT_TRUE( mapState.canFastTravel( "stag_town", "stag_cross" ) );
    SW_EXPECT_TRUE( mapState.activateSite( "bench_town" ) );
    SW_EXPECT_FALSE( mapState.canFastTravel( "bench_town", "stag_cross" ) ); // 벤치는 정거장이 아니다
    SW_EXPECT_FALSE( mapState.canFastTravel( "stag_town", "stag_town" ) );

    // 세이브 → 새 그래프에 되살리기.
    vector<hashed_string> listRegionMap;
    vector<hashed_string> listSite;
    vector<hashed_string> listPickup;
    mapState.fillSaveState( listRegionMap, listSite, listPickup );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listRegionMap.size() ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listSite.size() ) );
    SW_EXPECT_TRUE( listPickup.size() == 2 && listPickup[0] == hashed_string( "mask_cross3" ) && listPickup[1] == hashed_string( "wings" ) );
    AreaGraph graph;
    SW_ASSERT_TRUE( graph.loadFromXmlText( kAreaXml, "areas" ) );
    MetroMapState loaded;
    loaded.initialize( &scene._catalog, &graph );
    loaded.restoreSaveState( listRegionMap, listSite, listPickup );
    SW_EXPECT_TRUE( loaded.isShownOnMap( "cross3" ) );
    SW_EXPECT_TRUE( loaded.canFastTravel( "stag_cross", "stag_town" ) );
    SW_EXPECT_TRUE( loaded.isCollected( "wings" ) );
}

SW_TEST_CASE( MetroidvaniaTest, DeathDropsCurrencyAtCorpseAndSecondDeathLosesIt )
{
    MetroScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    MetroSoulsState souls;
    souls.initialize( &scene._catalog );
    MetroDuelist player;
    player.initialize( &scene._catalog );
    SW_EXPECT_TRUE( souls.rest( "bench_town", player.getVitality() ) );

    souls.addCurrency( 100 );
    const hashed_string respawn = souls.die( "cross2", float2{ 10.0f, 2.0f }, player.getVitality() );
    SW_EXPECT_TRUE( respawn == hashed_string( "bench_town" ) );
    SW_EXPECT_EQUAL( 0, souls.getCurrency() );
    SW_EXPECT_TRUE( souls.getCorpse()._bActive != SW_FALSE );
    SW_EXPECT_EQUAL( 100, souls.getCorpse()._currency );
    SW_EXPECT_TRUE( player.getVitality().isAlive() );

    // 되찾기 — 다른 방, 먼 거리는 안 되고 반경 안이면 된다.
    SW_EXPECT_FALSE( souls.tryRecoverCorpse( "cross1", float2{ 10.0f, 2.0f } ) );
    SW_EXPECT_FALSE( souls.tryRecoverCorpse( "cross2", float2{ 12.0f, 2.0f } ) );
    SW_EXPECT_TRUE( souls.tryRecoverCorpse( "cross2", float2{ 11.0f, 2.5f } ) );
    SW_EXPECT_EQUAL( 100, souls.getCurrency() );
    SW_EXPECT_FALSE( souls.getCorpse()._bActive != SW_FALSE );

    // 되찾기 전에 또 죽으면 — 첫 시체의 통화는 영영 사라지고 지금 통화가 새 시체가 된다.
    (void)souls.die( "cross3", float2{ 3.0f, 1.0f }, player.getVitality() );
    souls.addCurrency( 30 );
    (void)souls.die( "cross1", float2{ 5.0f, 1.0f }, player.getVitality() );
    SW_EXPECT_EQUAL( 100, souls.getLostCurrency() );
    SW_EXPECT_EQUAL( 30, souls.getCorpse()._currency );
    SW_EXPECT_TRUE( souls.getCorpse()._area == hashed_string( "cross1" ) );
    SW_EXPECT_FALSE( souls.tryRecoverCorpse( "cross3", float2{ 3.0f, 1.0f } ) ); // 옛 자리는 비었다

    vector<MetroSoulsEvent> listEvent;
    souls.drainEvents( listEvent );
    int32 lostEventCount = 0;
    for ( const MetroSoulsEvent& event : listEvent )
    {
        if ( event._type == MetroSoulsEventType::CurrencyLost )
        {
            ++lostEventCount;
            SW_EXPECT_EQUAL( 100, event._amount );
        }
    }
    SW_EXPECT_EQUAL( 1, lostEventCount );

    // 통화가 0 이면 시체를 남기지 않는다(남은 시체도 그대로 사라진다).
    SW_EXPECT_TRUE( souls.tryRecoverCorpse( "cross1", float2{ 5.0f, 1.0f } ) );
    SW_EXPECT_TRUE( souls.trySpendCurrency( 30 ) );
    SW_EXPECT_FALSE( souls.trySpendCurrency( 1 ) );
    (void)souls.die( "cross1", float2{ 5.0f, 1.0f }, player.getVitality() );
    SW_EXPECT_FALSE( souls.getCorpse()._bActive != SW_FALSE );
}

SW_TEST_CASE( MetroidvaniaTest, RestRefillsFlasksAndRespawnsEnemiesButNotBosses )
{
    MetroScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    LootCatalog loot;
    SW_ASSERT_TRUE( loot.loadFromXmlText( kLootXml, "loot" ) );
    MetroSoulsState souls;
    souls.initialize( &scene._catalog );
    MetroDuelist player;
    player.initialize( &scene._catalog );

    // 물약 — 두 번 마시면 끝, 죽은 몸에는 듣지 않는다.
    (void)player.getVitality().applyDamage( 80.0f );
    SW_EXPECT_TRUE( souls.drinkFlask( player.getVitality() ) );
    SW_EXPECT_NEAR_EQUAL( 50.0f, player.getVitality().getHealth(), 1.0e-3f );
    souls.upgradeFlaskPotency();
    SW_EXPECT_TRUE( souls.drinkFlask( player.getVitality() ) );
    SW_EXPECT_NEAR_EQUAL( 90.0f, player.getVitality().getHealth(), 1.0e-3f );
    SW_EXPECT_FALSE( souls.drinkFlask( player.getVitality() ) );
    SW_EXPECT_EQUAL( 0, souls.getFlaskCharges() );

    // 적 · 보스 처치 — 같은 자리는 두 번 쓰러뜨릴 수 없다.
    GameRandom random( 7u );
    ItemBag    drops;
    SW_EXPECT_EQUAL( 5, souls.registerKill( "cross1.husk_a", "husk", scene._flags, &loot, random, drops ) );
    SW_EXPECT_EQUAL( -1, souls.registerKill( "cross1.husk_a", "husk", scene._flags, &loot, random, drops ) );
    SW_EXPECT_EQUAL( 200, souls.registerKill( "cross3.boss", "falseKnight", scene._flags, &loot, random, drops ) );
    SW_EXPECT_TRUE( scene._flags.hasFlag( "boss.falseKnight" ) );
    SW_EXPECT_FALSE( souls.isSpawnAlive( "cross1.husk_a" ) );
    SW_EXPECT_EQUAL( 205, souls.getCurrency() );

    // 쉬는 곳이 아니면 쉬지 못한다.
    SW_EXPECT_FALSE( souls.rest( "stag_town", player.getVitality() ) );
    SW_EXPECT_EQUAL( 0, souls.getFlaskCharges() );
    // 쉬면 물약이 차고 체력이 가득, 졸개는 다시 나오고 보스는 그대로 죽어 있다.
    SW_EXPECT_TRUE( souls.rest( "bench_town", player.getVitality() ) );
    SW_EXPECT_EQUAL( 2, souls.getFlaskCharges() );
    SW_EXPECT_NEAR_EQUAL( 100.0f, player.getVitality().getHealth(), 1.0e-3f );
    SW_EXPECT_TRUE( souls.isSpawnAlive( "cross1.husk_a" ) );
    SW_EXPECT_FALSE( souls.isSpawnAlive( "cross3.boss" ) );
    SW_EXPECT_EQUAL( 5, souls.registerKill( "cross1.husk_a", "husk", scene._flags, &loot, random, drops ) );

    // 충전 수 업그레이드 — 상한(3)까지.
    SW_EXPECT_TRUE( souls.upgradeFlaskCharges() );
    SW_EXPECT_FALSE( souls.upgradeFlaskCharges() );
    SW_EXPECT_EQUAL( 3, souls.getFlaskMaxCharges() );

    // 전리품은 씨앗이 같으면 같다(결정적).
    int32 arrShard[2] = { 0, 0 };
    for ( int32 runIndex = 0; runIndex < 2; ++runIndex )
    {
        MetroSoulsState run;
        run.initialize( &scene._catalog );
        GameFlags  flags;
        GameRandom runRandom( 1234u );
        ItemBag    runDrops;
        for ( int32 spawnIndex = 0; spawnIndex < 20; ++spawnIndex )
            (void)run.registerKill( hashed_string( string( "husk_" ) + static_cast<utf8>( 'a' + spawnIndex ) ), "husk", flags, &loot, runRandom, runDrops );
        arrShard[runIndex] = runDrops.getItemCount( "shard" );
        SW_EXPECT_EQUAL( 100, run.getCurrency() );
    }
    SW_EXPECT_EQUAL( arrShard[0], arrShard[1] );
    SW_EXPECT_TRUE( arrShard[0] > 0 );
}

SW_TEST_CASE( MetroidvaniaTest, ParryWindowOpensRiposteAndGuardSpendsStamina )
{
    MetroScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    MetroDuelist player;
    player.initialize( &scene._catalog );
    MetroDuelist boss;
    boss.initialize( &scene._catalog );

    // 창 안(0.05 초 일찍) — 피해 없이 패리, 반격 배율은 한 번만.
    player.pressParry();
    for ( int32 frame = 0; frame < 3; ++frame )
        player.update( kStep );
    MetroDefenseOutcome outcome = player.receiveAttack( 25.0f, 10.0f );
    SW_EXPECT_TRUE( outcome._result == MetroDefenseResult::Parried );
    SW_EXPECT_TRUE( outcome._parryGrade == hashed_string( "Parry" ) );
    SW_EXPECT_NEAR_EQUAL( 100.0f, player.getVitality().getHealth(), 1.0e-3f );
    SW_EXPECT_TRUE( player.isRiposteReady() );
    SW_EXPECT_NEAR_EQUAL( 30.0f, player.computeAttackDamage( 10.0f, boss ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, player.computeAttackDamage( 10.0f, boss ), 1.0e-3f );

    // 창 밖(0.3 초 일찍) — 그냥 맞는다. 한 번 쓴 누름은 다음 공격에 다시 쓰이지 않는다.
    player.pressParry();
    for ( int32 frame = 0; frame < 18; ++frame )
        player.update( kStep );
    outcome = player.receiveAttack( 25.0f, 10.0f );
    SW_EXPECT_TRUE( outcome._result == MetroDefenseResult::Hit );
    SW_EXPECT_NEAR_EQUAL( 25.0f, outcome._healthDamage, 1.0e-3f );

    // 반격 창은 시간이 지나면 닫힌다.
    player.pressParry();
    player.update( kStep );
    SW_EXPECT_TRUE( player.receiveAttack( 5.0f, 0.0f )._result == MetroDefenseResult::Parried );
    for ( int32 frame = 0; frame < 70; ++frame )
        player.update( kStep );
    SW_EXPECT_FALSE( player.isRiposteReady() );

    // 막기 — 피해만큼 스태미나를 쓰고 체력은 조금. 스태미나가 모자라면 가드 붕괴.
    MetroDuelist guard;
    guard.initialize( &scene._catalog );
    guard.setGuarding( true );
    outcome = guard.receiveAttack( 40.0f, 0.0f );
    SW_EXPECT_TRUE( outcome._result == MetroDefenseResult::Blocked );
    SW_EXPECT_NEAR_EQUAL( 4.0f, outcome._healthDamage, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 60.0f, guard.getStamina().getValue(), 1.0e-3f );
    SW_EXPECT_TRUE( guard.receiveAttack( 40.0f, 0.0f )._result == MetroDefenseResult::Blocked );
    outcome = guard.receiveAttack( 40.0f, 0.0f );
    SW_EXPECT_TRUE( outcome._result == MetroDefenseResult::GuardBroken );
    SW_EXPECT_NEAR_EQUAL( 40.0f, outcome._healthDamage, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, guard.getStamina().getValue(), 1.0e-3f );
    SW_EXPECT_FALSE( guard.tryAttack() );
    SW_EXPECT_FALSE( guard.tryDodge() );

    // 강인도 — 30 을 넘게 깎이면 비틀거리고, 그 상대에게는 패리 없이도 반격 배율.
    MetroDuelist attacker;
    attacker.initialize( &scene._catalog );
    SW_EXPECT_FALSE( boss.receiveAttack( 10.0f, 20.0f )._bStaggered != SW_FALSE );
    SW_EXPECT_NEAR_EQUAL( 10.0f, attacker.computeAttackDamage( 10.0f, boss ), 1.0e-3f );
    SW_EXPECT_TRUE( boss.receiveAttack( 10.0f, 20.0f )._bStaggered != SW_FALSE );
    SW_EXPECT_NEAR_EQUAL( 30.0f, attacker.computeAttackDamage( 10.0f, boss ), 1.0e-3f );

    // 스태미나 — 공격 다섯 번(20 씩)이면 바닥, 쉬면 다시 찬다.
    for ( int32 attackIndex = 0; attackIndex < 5; ++attackIndex )
        SW_EXPECT_TRUE( attacker.tryAttack() );
    SW_EXPECT_FALSE( attacker.tryAttack() );
    for ( int32 frame = 0; frame < 60; ++frame )
        attacker.update( kStep );
    SW_EXPECT_TRUE( attacker.tryAttack() );
}

SW_TEST_CASE( MetroidvaniaTest, CharmNotchesOvercharmOnceAndStatsMerge )
{
    MetroScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    MetroCharmLoadout loadout;
    loadout.initialize( &scene._catalog );
    SW_EXPECT_EQUAL( 3, loadout.getNotchCount() );

    SW_EXPECT_TRUE( loadout.equip( "strength" ) == MetroCharmResult::NotOwned );
    SW_EXPECT_TRUE( loadout.equip( "nothing" ) == MetroCharmResult::UnknownCharm );
    SW_EXPECT_TRUE( loadout.grantCharm( "strength" ) );
    SW_EXPECT_TRUE( loadout.grantCharm( "quickSlash" ) );
    SW_EXPECT_TRUE( loadout.grantCharm( "compass" ) );
    SW_EXPECT_FALSE( loadout.grantCharm( "compass" ) );

    SW_EXPECT_TRUE( loadout.equip( "strength" ) == MetroCharmResult::Equipped );
    SW_EXPECT_TRUE( loadout.equip( "strength" ) == MetroCharmResult::AlreadyEquipped );
    // 2 + 2 > 3 이지만 빈 슬롯이 하나 남아 있으니 넘겨 낀다.
    SW_EXPECT_TRUE( loadout.equip( "quickSlash" ) == MetroCharmResult::Overcharmed );
    SW_EXPECT_TRUE( loadout.isOvercharmed() );
    SW_EXPECT_NEAR_EQUAL( 2.0f, loadout.computeDamageTakenScale(), 1.0e-4f );
    // 이미 넘겼으면 더는 못 낀다.
    SW_EXPECT_TRUE( loadout.equip( "compass" ) == MetroCharmResult::NotEnoughNotches );

    StatBlock stats;
    loadout.mergeStats( stats );
    SW_EXPECT_NEAR_EQUAL( 0.5f, stats.getValue( "damage" ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, stats.getValue( "attackSpeed" ), 1.0e-4f );

    // 벗으면 넘김이 풀리고, 슬롯 조각으로 늘리면 셋 다 낀다.
    SW_EXPECT_TRUE( loadout.unequip( "quickSlash" ) == MetroCharmResult::Unequipped );
    SW_EXPECT_TRUE( loadout.unequip( "quickSlash" ) == MetroCharmResult::NotEquipped );
    SW_EXPECT_FALSE( loadout.isOvercharmed() );
    loadout.addNotches( 2 );
    SW_EXPECT_TRUE( loadout.equip( "quickSlash" ) == MetroCharmResult::Equipped );
    SW_EXPECT_TRUE( loadout.equip( "compass" ) == MetroCharmResult::Equipped );
    SW_EXPECT_EQUAL( 5, loadout.computeUsedNotches() );
    SW_EXPECT_FALSE( loadout.isOvercharmed() );

    // 규칙을 끄면 넘겨 끼기는 없다.
    scene._catalog.getRules()._bAllowOvercharm = SW_FALSE;
    MetroCharmLoadout strict;
    strict.initialize( &scene._catalog );
    SW_EXPECT_TRUE( strict.grantCharm( "strength" ) && strict.grantCharm( "quickSlash" ) );
    SW_EXPECT_TRUE( strict.equip( "strength" ) == MetroCharmResult::Equipped );
    SW_EXPECT_TRUE( strict.equip( "quickSlash" ) == MetroCharmResult::NotEnoughNotches );
    SW_EXPECT_NEAR_EQUAL( 1.0f, strict.computeDamageTakenScale(), 1.0e-4f );
}
