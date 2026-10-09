// 메트로배니아 · 2D 소울라이크 키트 — 능력 → 몸 설정 · 길 잠금, 지도 구매 · 아이템 표시 · 빠른 이동, 시체 · 영구 손실, 휴식 · 물약 · 적 부활 · 보스, 패리 · 막기 · 강인도, 부적 슬롯.
#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Movement/PlatformerMotor2D.h"
#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Framework/GameStateRefs.h"
#include "GameFramework/Base/Foundation/Utility/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Base/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/World/World/AreaGraph.h"
#include "GameFramework/Base/World/World/GameFlags.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroAbilitySet.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroCharmLoadout.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroDuelist.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroMapState.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroSoulsState.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroidvaniaCatalog.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr float32 kMetroidvaniaStep = 1.0f / 60.0f;

    constexpr const utf8* kMetroidvaniaCatalogXml = R"(
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
    constexpr const utf8* kMetroidvaniaLevelText = R"(
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

        MetroScene() { _bLoaded = _catalog.loadFromXmlText( kMetroidvaniaCatalogXml, "metro" ) && _graph.loadFromXmlText( kAreaXml, "areas" ); }
    };

    /** @brief 영혼 상태가 빌릴 지갑 묶음입니다. */
    GameStateRefs lendMetroWallet( Wallet& wallet )
    {
        GameStateRefs refs;
        refs._pWallet = &wallet;
        return refs;
    }

    /** @brief 카탈로그 통화("geo") 잔액입니다. */
    int32 geoOf( const Wallet& wallet )
    {
        return static_cast<int32>( wallet.getBalance( "geo" ) );
    }

    uint32 runMotor( PlatformerMotor2D& motor, const PlatformTileMap& map, const MetroAbilitySet& abilities, const PlatformerInput& input, int32 frameCount )
    {
        uint32 events = 0;
        for ( int32 frame = 0; frame < frameCount; ++frame )
        {
            motor.update( map, abilities.filterInput( input ), kMetroidvaniaStep );
            events |= motor.getEvents();
        }
        return events;
    }

    /** @brief 상태 바이트를 꺼냅니다. */
    template <typename StateType>
    vector<uint8> captureMetroBytes( const StateType& state )
    {
        Archive archive;
        state.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }

    /** @brief @p bytes 를 @p outState 에 읽고 끝까지 다 읽었으면 true 입니다. */
    template <typename StateType>
    [[nodiscard]] bool restoreMetroBytes( const vector<uint8>& bytes, StateType& outState )
    {
        Archive reader( bytes.data(), bytes.size() );
        return outState.readState( reader ) && reader.getRemainingBytes() == 0;
    }

    /** @brief 마지막 한 바이트를 자른 바이트를 @p outState 에 읽습니다(거절되어야 한다). */
    template <typename StateType>
    [[nodiscard]] bool restoreTruncatedMetroBytes( const vector<uint8>& bytes, StateType& outState )
    {
        Archive reader( bytes.data(), bytes.size() - 1 );
        return outState.readState( reader );
    }
} // namespace

SW_TEST_CASE( MetroidvaniaTest, AbilitiesRewriteMotorSettingsAndOpenAreaLocks )
{
    MetroScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );

    PlatformTileMap map;
    map.loadFromText( kMetroidvaniaLevelText, 1.0f, float2{ 0.0f, 0.0f } );
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
    Wallet wallet;
    wallet.add( "geo", 20 );
    SW_EXPECT_TRUE( mapState.buyRegionMap( "crossroads", wallet, "geo" ) == MetroMapPurchase::NotEnoughCurrency );
    SW_EXPECT_EQUAL( 20, geoOf( wallet ) );
    SW_EXPECT_TRUE( mapState.buyRegionMap( "nowhere", wallet, "geo" ) == MetroMapPurchase::UnknownRegion );
    wallet.add( "geo", 30 );
    SW_EXPECT_TRUE( mapState.buyRegionMap( "crossroads", wallet, "geo" ) == MetroMapPurchase::Bought );
    SW_EXPECT_EQUAL( 20, geoOf( wallet ) );
    SW_EXPECT_TRUE( mapState.buyRegionMap( "crossroads", wallet, "geo" ) == MetroMapPurchase::AlreadyOwned );

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
    Wallet          soulsWallet;
    MetroSoulsState souls;
    souls.initialize( &scene._catalog, lendMetroWallet( soulsWallet ) );
    MetroDuelist player;
    player.initialize( &scene._catalog );
    SW_EXPECT_TRUE( souls.rest( "bench_town", player.getVitality() ) );

    soulsWallet.add( "geo", 100 );
    const hashed_string respawn = souls.die( "cross2", float2{ 10.0f, 2.0f }, player.getVitality() );
    SW_EXPECT_TRUE( respawn == hashed_string( "bench_town" ) );
    SW_EXPECT_EQUAL( 0, geoOf( soulsWallet ) );
    SW_EXPECT_TRUE( souls.getCorpse()._bActive != SW_FALSE );
    SW_EXPECT_EQUAL( 100, souls.getCorpse()._currency );
    SW_EXPECT_TRUE( player.getVitality().isAlive() );

    // 되찾기 — 다른 방, 먼 거리는 안 되고 반경 안이면 된다.
    SW_EXPECT_FALSE( souls.tryRecoverCorpse( "cross1", float2{ 10.0f, 2.0f } ) );
    SW_EXPECT_FALSE( souls.tryRecoverCorpse( "cross2", float2{ 12.0f, 2.0f } ) );
    SW_EXPECT_TRUE( souls.tryRecoverCorpse( "cross2", float2{ 11.0f, 2.5f } ) );
    SW_EXPECT_EQUAL( 100, geoOf( soulsWallet ) );
    SW_EXPECT_FALSE( souls.getCorpse()._bActive != SW_FALSE );

    // 되찾기 전에 또 죽으면 — 첫 시체의 통화는 영영 사라지고 지금 통화가 새 시체가 된다.
    (void)souls.die( "cross3", float2{ 3.0f, 1.0f }, player.getVitality() );
    soulsWallet.add( "geo", 30 );
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
    SW_EXPECT_TRUE( soulsWallet.trySpend( "geo", 30 ) );
    SW_EXPECT_FALSE( soulsWallet.trySpend( "geo", 1 ) );
    (void)souls.die( "cross1", float2{ 5.0f, 1.0f }, player.getVitality() );
    SW_EXPECT_FALSE( souls.getCorpse()._bActive != SW_FALSE );
}

/**
 * @brief [MetroidvaniaTest] 죽으면 빌린 지갑의 카탈로그 통화만 시체로 옮기고, 되찾으면 지갑으로 돌아온다 — 같은 지갑의 다른 통화(다른 키트의 돈)는 그대로다
 */
SW_TEST_CASE( MetroidvaniaTest, DeathDropsTheWalletCurrencyAndRecoveryReturnsIt )
{
    MetroScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    Wallet shared;
    shared.add( "geo", 70 );
    shared.add( "Gold", 15 ); // 다른 키트가 같은 지갑에 둔 돈
    MetroSoulsState souls;
    souls.initialize( &scene._catalog, lendMetroWallet( shared ) );
    MetroDuelist player;
    player.initialize( &scene._catalog );
    SW_EXPECT_TRUE( souls.rest( "bench_town", player.getVitality() ) );

    (void)souls.die( "cross2", float2{ 10.0f, 2.0f }, player.getVitality() );
    SW_EXPECT_EQUAL( 0, geoOf( shared ) );
    SW_EXPECT_EQUAL( 70, souls.getCorpse()._currency );
    SW_EXPECT_EQUAL( int64{ 15 }, shared.getBalance( "Gold" ) );
    SW_ASSERT_TRUE( souls.tryRecoverCorpse( "cross2", float2{ 10.5f, 2.0f } ) );
    SW_EXPECT_EQUAL( 70, geoOf( shared ) );
    SW_EXPECT_EQUAL( int64{ 15 }, shared.getBalance( "Gold" ) );
}

SW_TEST_CASE( MetroidvaniaTest, RestRefillsFlasksAndRespawnsEnemiesButNotBosses )
{
    MetroScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    LootCatalog loot;
    SW_ASSERT_TRUE( loot.loadFromXmlText( kLootXml, "loot" ) );
    Wallet          soulsWallet;
    MetroSoulsState souls;
    souls.initialize( &scene._catalog, lendMetroWallet( soulsWallet ) );
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
    GameRandom    random( 7u );
    ItemStackList drops;
    SW_EXPECT_EQUAL( 5, souls.registerKill( "cross1.husk_a", "husk", scene._flags, &loot, random, drops ) );
    SW_EXPECT_EQUAL( -1, souls.registerKill( "cross1.husk_a", "husk", scene._flags, &loot, random, drops ) );
    SW_EXPECT_EQUAL( 200, souls.registerKill( "cross3.boss", "falseKnight", scene._flags, &loot, random, drops ) );
    SW_EXPECT_TRUE( scene._flags.hasFlag( "boss.falseKnight" ) );
    SW_EXPECT_FALSE( souls.isSpawnAlive( "cross1.husk_a" ) );
    SW_EXPECT_EQUAL( 205, geoOf( soulsWallet ) );

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
        Wallet          runWallet;
        MetroSoulsState run;
        run.initialize( &scene._catalog, lendMetroWallet( runWallet ) );
        GameFlags     flags;
        GameRandom    runRandom( 1234u );
        ItemStackList runDrops;
        for ( int32 spawnIndex = 0; spawnIndex < 20; ++spawnIndex )
        {
            (void)run.registerKill( hashed_string( string( "husk_" ) + static_cast<utf8>( 'a' + spawnIndex ) ), "husk", flags, &loot, runRandom, runDrops );
        }
        arrShard[runIndex] = runDrops.getItemCount( "shard" );
        SW_EXPECT_EQUAL( 100, geoOf( runWallet ) );
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
    {
        player.update( kMetroidvaniaStep );
    }
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
    {
        player.update( kMetroidvaniaStep );
    }
    outcome = player.receiveAttack( 25.0f, 10.0f );
    SW_EXPECT_TRUE( outcome._result == MetroDefenseResult::Hit );
    SW_EXPECT_NEAR_EQUAL( 25.0f, outcome._healthDamage, 1.0e-3f );

    // 반격 창은 시간이 지나면 닫힌다.
    player.pressParry();
    player.update( kMetroidvaniaStep );
    SW_EXPECT_TRUE( player.receiveAttack( 5.0f, 0.0f )._result == MetroDefenseResult::Parried );
    for ( int32 frame = 0; frame < 70; ++frame )
    {
        player.update( kMetroidvaniaStep );
    }
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
    {
        SW_EXPECT_TRUE( attacker.tryAttack() );
    }
    SW_EXPECT_FALSE( attacker.tryAttack() );
    for ( int32 frame = 0; frame < 60; ++frame )
    {
        attacker.update( kMetroidvaniaStep );
    }
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

/**
 * @brief [MetroidvaniaTest] 상태 바이트 — 능력 · 부적 · 결투(체력 · 지구력 · 패리 · 막기) · 지도(산 지도 · 지점 · 주운 것) · 영혼(처치 · 시체 · 물약)이
 *        그대로 오고, 같은 걸음을 더 돌려도 바이트가 같다. 빌린 그래프 · 지갑 · 플래그는 싣지 않고, 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( MetroidvaniaTest, StateRoundTripContinuesTheSameJourney )
{
    MetroScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );

    // 능력 — 대시 · 벽 점프를 얻었다.
    MetroAbilitySet abilities;
    abilities.initialize( &scene._catalog, PlatformerSettings{} );
    SW_EXPECT_TRUE( abilities.grantAbility( "dash", scene._flags ) );
    SW_EXPECT_TRUE( abilities.grantAbility( "wallJump", scene._flags ) );
    const vector<uint8> abilityBytes = captureMetroBytes( abilities );
    MetroAbilitySet     restoredAbilities;
    restoredAbilities.initialize( &scene._catalog, PlatformerSettings{} );
    SW_ASSERT_TRUE( restoreMetroBytes( abilityBytes, restoredAbilities ) );
    SW_EXPECT_TRUE( restoredAbilities.canDash() );
    SW_EXPECT_TRUE( restoredAbilities.hasAbility( "wallJump" ) );
    SW_EXPECT_TRUE( abilityBytes == captureMetroBytes( restoredAbilities ) );
    GameFlags restoredFlags;
    SW_EXPECT_TRUE( abilities.grantAbility( "doubleJump", scene._flags ) );
    SW_EXPECT_TRUE( restoredAbilities.grantAbility( "doubleJump", restoredFlags ) );
    SW_EXPECT_TRUE( captureMetroBytes( abilities ) == captureMetroBytes( restoredAbilities ) );
    MetroAbilitySet truncatedAbilities;
    truncatedAbilities.initialize( &scene._catalog, PlatformerSettings{} );
    SW_EXPECT_FALSE( restoreTruncatedMetroBytes( abilityBytes, truncatedAbilities ) );
    SW_EXPECT_TRUE( truncatedAbilities.getAbilities().empty() );

    // 부적 — 셋 중 둘을 꼈고 슬롯 조각 하나.
    MetroCharmLoadout charms;
    charms.initialize( &scene._catalog );
    SW_EXPECT_TRUE( charms.grantCharm( "strength" ) && charms.grantCharm( "compass" ) && charms.grantCharm( "quickSlash" ) );
    SW_EXPECT_TRUE( charms.equip( "strength" ) == MetroCharmResult::Equipped );
    SW_EXPECT_TRUE( charms.equip( "compass" ) == MetroCharmResult::Equipped );
    charms.addNotches( 1 );
    const vector<uint8> charmBytes = captureMetroBytes( charms );
    MetroCharmLoadout   restoredCharms;
    restoredCharms.initialize( &scene._catalog );
    SW_ASSERT_TRUE( restoreMetroBytes( charmBytes, restoredCharms ) );
    SW_EXPECT_EQUAL( 4, restoredCharms.getNotchCount() );
    SW_EXPECT_EQUAL( 3, restoredCharms.computeUsedNotches() );
    SW_EXPECT_TRUE( restoredCharms.isOwned( "quickSlash" ) );
    SW_EXPECT_TRUE( charmBytes == captureMetroBytes( restoredCharms ) );
    SW_EXPECT_TRUE( charms.equip( "quickSlash" ) == MetroCharmResult::Overcharmed );
    SW_EXPECT_TRUE( restoredCharms.equip( "quickSlash" ) == MetroCharmResult::Overcharmed ); // 슬롯 수가 이어져 같은 판정
    SW_EXPECT_TRUE( captureMetroBytes( charms ) == captureMetroBytes( restoredCharms ) );
    MetroCharmLoadout truncatedCharms;
    truncatedCharms.initialize( &scene._catalog );
    SW_EXPECT_FALSE( restoreTruncatedMetroBytes( charmBytes, truncatedCharms ) );
    SW_EXPECT_EQUAL( 3, truncatedCharms.getNotchCount() );

    // 결투 — 한 대 맞고, 패리를 누르고, 막는 중.
    MetroDuelist duelist;
    duelist.initialize( &scene._catalog );
    SW_EXPECT_TRUE( duelist.tryAttack() );
    (void)duelist.receiveAttack( 20.0f, 10.0f );
    duelist.pressParry();
    for ( int32 frame = 0; frame < 5; ++frame )
    {
        duelist.update( kMetroidvaniaStep );
    }
    duelist.setGuarding( true );
    const vector<uint8> duelistBytes = captureMetroBytes( duelist );
    MetroDuelist        restoredDuelist;
    restoredDuelist.initialize( &scene._catalog );
    SW_ASSERT_TRUE( restoreMetroBytes( duelistBytes, restoredDuelist ) );
    SW_EXPECT_NEAR_EQUAL( 80.0f, restoredDuelist.getVitality().getHealth(), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( duelist.getStamina().getValue(), restoredDuelist.getStamina().getValue(), 1.0e-5f );
    SW_EXPECT_TRUE( restoredDuelist.isGuarding() );
    SW_EXPECT_TRUE( duelistBytes == captureMetroBytes( restoredDuelist ) );
    const MetroDefenseOutcome outcome         = duelist.receiveAttack( 25.0f, 10.0f );
    const MetroDefenseOutcome restoredOutcome = restoredDuelist.receiveAttack( 25.0f, 10.0f ); // 패리 누른 시각이 이어져 같은 판정
    SW_EXPECT_TRUE( outcome._result == restoredOutcome._result );
    for ( int32 frame = 0; frame < 30; ++frame )
    {
        duelist.update( kMetroidvaniaStep );
        restoredDuelist.update( kMetroidvaniaStep );
    }
    SW_EXPECT_TRUE( captureMetroBytes( duelist ) == captureMetroBytes( restoredDuelist ) );
    MetroDuelist truncatedDuelist;
    truncatedDuelist.initialize( &scene._catalog );
    SW_EXPECT_FALSE( restoreTruncatedMetroBytes( duelistBytes, truncatedDuelist ) );
    SW_EXPECT_FALSE( truncatedDuelist.isGuarding() );

    // 지도 — 교차로 지도를 사고, 가면 하나를 줍고, 정거장을 열었다. 되살린 쪽의 그래프에도 산 지역이 드러난다.
    MetroMapState mapState;
    mapState.initialize( &scene._catalog, &scene._graph );
    SW_EXPECT_TRUE( mapState.enterArea( "town" ) );
    SW_EXPECT_TRUE( mapState.enterArea( "cross1" ) );
    Wallet wallet;
    wallet.add( "geo", 50 );
    SW_EXPECT_TRUE( mapState.buyRegionMap( "crossroads", wallet, "geo" ) == MetroMapPurchase::Bought );
    SW_EXPECT_NOT_NULL( mapState.collectPickup( "mask_cross3" ) );
    SW_EXPECT_TRUE( mapState.activateSite( "stag_town" ) );
    const vector<uint8> mapBytes = captureMetroBytes( mapState );
    AreaGraph           restoredGraph;
    SW_ASSERT_TRUE( restoredGraph.loadFromXmlText( kAreaXml, "areas" ) );
    MetroMapState restoredMap;
    restoredMap.initialize( &scene._catalog, &restoredGraph );
    SW_ASSERT_TRUE( restoreMetroBytes( mapBytes, restoredMap ) );
    SW_EXPECT_TRUE( restoredMap.hasRegionMap( "crossroads" ) );
    SW_EXPECT_TRUE( restoredMap.isCollected( "mask_cross3" ) );
    SW_EXPECT_TRUE( restoredMap.isSiteActive( "stag_town" ) );
    SW_EXPECT_TRUE( restoredGraph.isDiscovered( "cross3" ) );
    SW_EXPECT_TRUE( mapBytes == captureMetroBytes( restoredMap ) );
    SW_EXPECT_NOT_NULL( mapState.collectPickup( "wings" ) );
    SW_EXPECT_NOT_NULL( restoredMap.collectPickup( "wings" ) );
    SW_EXPECT_NULL( restoredMap.collectPickup( "mask_cross3" ) ); // 주운 것은 다시 줍지 못한다
    SW_EXPECT_TRUE( captureMetroBytes( mapState ) == captureMetroBytes( restoredMap ) );
    AreaGraph truncatedGraph;
    SW_ASSERT_TRUE( truncatedGraph.loadFromXmlText( kAreaXml, "areas" ) );
    MetroMapState truncatedMap;
    truncatedMap.initialize( &scene._catalog, &truncatedGraph );
    SW_EXPECT_FALSE( restoreTruncatedMetroBytes( mapBytes, truncatedMap ) );
    SW_EXPECT_FALSE( truncatedMap.hasRegionMap( "crossroads" ) );

    // 영혼 — 쉬고, 100 을 들고 죽어 시체가 남았고, 졸개 하나를 쓰러뜨렸고, 물약 하나를 마셨다.
    Wallet          soulsWallet;
    MetroSoulsState souls;
    souls.initialize( &scene._catalog, lendMetroWallet( soulsWallet ) );
    MetroDuelist player;
    player.initialize( &scene._catalog );
    SW_EXPECT_TRUE( souls.rest( "bench_town", player.getVitality() ) );
    soulsWallet.add( "geo", 100 );
    (void)souls.die( "cross2", float2{ 10.0f, 2.0f }, player.getVitality() );
    GameRandom    random( 7u );
    ItemStackList drops;
    SW_EXPECT_EQUAL( 5, souls.registerKill( "cross1.husk_a", "husk", scene._flags, nullptr, random, drops ) );
    (void)player.getVitality().applyDamage( 50.0f );
    SW_EXPECT_TRUE( souls.drinkFlask( player.getVitality() ) );
    const vector<uint8> soulsBytes = captureMetroBytes( souls );
    Wallet              restoredWallet;
    MetroSoulsState     restoredSouls;
    restoredSouls.initialize( &scene._catalog, lendMetroWallet( restoredWallet ) );
    SW_ASSERT_TRUE( restoreMetroBytes( soulsBytes, restoredSouls ) );
    SW_EXPECT_EQUAL( 100, restoredSouls.getCorpse()._currency );
    SW_EXPECT_TRUE( restoredSouls.getRespawnSite() == hashed_string( "bench_town" ) );
    SW_EXPECT_EQUAL( souls.getFlaskCharges(), restoredSouls.getFlaskCharges() );
    SW_EXPECT_FALSE( restoredSouls.isSpawnAlive( "cross1.husk_a" ) );
    SW_EXPECT_TRUE( soulsBytes == captureMetroBytes( restoredSouls ) );
    MetroDuelist restoredPlayer;
    restoredPlayer.initialize( &scene._catalog );
    SW_EXPECT_TRUE( souls.rest( "bench_town", player.getVitality() ) );
    SW_EXPECT_TRUE( restoredSouls.rest( "bench_town", restoredPlayer.getVitality() ) ); // 졸개가 되살아나고 물약이 찬다
    SW_EXPECT_TRUE( restoredSouls.isSpawnAlive( "cross1.husk_a" ) );
    SW_EXPECT_TRUE( captureMetroBytes( souls ) == captureMetroBytes( restoredSouls ) );
    MetroSoulsState truncatedSouls;
    truncatedSouls.initialize( &scene._catalog, lendMetroWallet( restoredWallet ) );
    SW_EXPECT_FALSE( restoreTruncatedMetroBytes( soulsBytes, truncatedSouls ) );
    SW_EXPECT_TRUE( truncatedSouls.getKills().empty() );
}
