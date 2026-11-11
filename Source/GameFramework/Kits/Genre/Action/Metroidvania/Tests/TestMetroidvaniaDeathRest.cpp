#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaDeathRestState.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaDuelist.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

#include <cstring>
#include <limits>

// MetroidvaniaDeathRestTest — 죽음과 휴식: 쉬기, 시체 떨어뜨리기 · 되찾기 · 영구 손실, 물약, 처치와 보스, 왕복. 결함: NaN 시체 위치(어디서든 되찾음) · 쉬는 곳이 아닌 되살아날 지점 · 겹친 처치 위치를 받았다.
// MetroidvaniaDuelistTest — 맞붙기: 패리 창 경계, 막기 · 가드 붕괴, 강인도, 반격 배율, 왕복. 결함: 음수 피해가 가드를 깨고 NaN 피해가 스태미나를 NaN 으로 만들었다 · NaN 표지 값의 상태를 받았다. 키트 GF_Metroidvania(nogpu).

namespace
{
    // 시간은 2 의 거듭제곱 분수라 float 오차가 없다.
    constexpr const utf8* kDuelXML = R"(<Metroidvania currency="geo">
  <Rules flaskCharges="2" flaskMaxCharges="3" flaskHeal="30" flaskHealPerUpgrade="10" riposteMultiplier="3" riposteTime="1" attackStamina="15" dodgeStamina="20" guardStaminaPerDamage="1" guardChip="0.25" corpseRadius="2"/>
  <Health max="100" poise="30" poiseBreakDuration="1"/>
  <Stamina max="100" regenRate="0" regenDelay="0"/>
  <Parry><Window grade="Perfect" early="0.25" late="0.25"/></Parry>
  <Site id="bench" area="hall" rest="true"/>
  <Site id="second" area="cave" rest="true"/>
  <Site id="sign" area="hall"/>
  <Enemy id="husk" currency="5" loot="husk"/>
  <Enemy id="poor"/>
  <Enemy id="hornet" boss="true" flag="boss.hornet" currency="300"/>
</Metroidvania>)";

    constexpr const utf8* kDuelLootXML = R"(<LootCatalog><Table id="husk"><Entry item="scrap" min="2"/></Table></LootCatalog>)";

    const sw::hashed_string kGeo{ "geo" };
    const sw::hashed_string kBench{ "bench" };
    const sw::hashed_string kHall{ "hall" };
    const sw::hashed_string kCave{ "cave" };
    const sw::hashed_string kHusk{ "husk" };
    const sw::hashed_string kHornet{ "hornet" };
    const sw::hashed_string kSpawnA{ "spawnA" };
    const sw::hashed_string kSpawnB{ "spawnB" };

    constexpr float32 kDuelTolerance = 1.0e-3f;

    /** @brief 카탈로그, 지갑, 체력, 상태를 묶은 한 판입니다. */
    struct DeathRestBench
    {
        sw::MetroidvaniaCatalog        _catalog;
        sw::Wallet                     _wallet;
        sw::HealthStateMachine         _healthMachine;
        sw::MetroidvaniaDeathRestState _state;
        sw::GameFlags                  _flags;
        sw::LootCatalog                _loot;
        sw::GameRandom                 _random{ 7u };
        sw::ItemStackList              _drops;

        [[nodiscard]] bool setUp()
        {
            if ( _catalog.loadFromXMLText( kDuelXML, "MetroidvaniaDeathRestTest" ) == false || _loot.loadFromXMLText( kDuelLootXML, "loot" ) == false )
                return false;
            sw::SessionStateRefs refs;
            refs._pWallet = &_wallet;
            _state.initialize( &_catalog, refs );
            return _healthMachine.initialize( _catalog.getRules()._health );
        }

        int32 kill( const sw::hashed_string& spawnID, const sw::hashed_string& enemyID ) { return _state.registerKill( spawnID, enemyID, _flags, &_loot, _random, _drops ); }

        int32 countEvents( sw::MetroidvaniaDeathRestEventKind type )
        {
            sw::vector<sw::MetroidvaniaDeathRestEvent> listEvent;
            _state.pollEvents( listEvent );
            int32 count = 0;
            for ( const sw::MetroidvaniaDeathRestEvent& event : listEvent )
            {
                if ( event._type == type )
                    ++count;
            }
            return count;
        }
    };

    struct MetroidvaniaDeathRestTestUtil
    {
        static void writeStateBytes( sw::Archive& outArchive, const sw::vector<sw::hashed_string>& listSpawn, float32 corpseX, const sw::hashed_string& respawnSite, int32 flaskCharges, int32 flaskMaxCharges )
        {
            outArchive << static_cast<uint32>( listSpawn.size() );
            for ( const sw::hashed_string& spawn : listSpawn )
            {
                sw::StateIO::writeName( outArchive, spawn );
                outArchive << static_cast<uint8>( SW_FALSE );
            }
            sw::StateIO::writeName( outArchive, sw::hashed_string{} );
            outArchive << sw::float2{ corpseX, 0.0f };
            outArchive << static_cast<int32>( 0 );
            outArchive << static_cast<uint8>( SW_FALSE );
            sw::StateIO::writeName( outArchive, respawnSite );
            outArchive << static_cast<int32>( 0 );
            outArchive << flaskCharges;
            outArchive << flaskMaxCharges;
            outArchive << static_cast<int32>( 0 );
        }
    };
} // namespace

/**
 * @brief [MetroidvaniaDeathRestTest] 쉬는 지점에서만 쉬고, 쉬면 체력 · 물약이 차고 되살아날 곳이 바뀌며, 보통 적만 다시 나온다
 */
SW_TEST_CASE( MetroidvaniaDeathRestTest, RestingRefreshesTheWorld )
{
    DeathRestBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    SW_EXPECT_FALSE( bench._state.rest( sw::hashed_string( "sign" ), bench._healthMachine ) );
    SW_EXPECT_FALSE( bench._state.rest( sw::hashed_string( "ghost" ), bench._healthMachine ) );
    SW_EXPECT_TRUE( bench._state.getRespawnSite().empty() );

    (void)bench._healthMachine.applyDamage( 60.0f );
    SW_REQUIRE_TRUE( bench._state.drinkFlask( bench._healthMachine ) );
    SW_EXPECT_EQUAL( 5, bench.kill( kSpawnA, kHusk ) );
    SW_EXPECT_EQUAL( 300, bench.kill( kSpawnB, kHornet ) );
    SW_EXPECT_FALSE( bench._state.isSpawnAlive( kSpawnA ) );
    (void)bench.countEvents( sw::MetroidvaniaDeathRestEventKind::Rested );

    SW_EXPECT_TRUE( bench._state.rest( kBench, bench._healthMachine ) );
    SW_EXPECT_NEAR( 100.0f, bench._healthMachine.getHealth(), kDuelTolerance );
    SW_EXPECT_EQUAL( 2, bench._state.getFlaskCharges() );
    SW_EXPECT_EQUAL( kBench, bench._state.getRespawnSite() );
    SW_EXPECT_TRUE( bench._state.isSpawnAlive( kSpawnA ) );
    SW_EXPECT_FALSE( bench._state.isSpawnAlive( kSpawnB ) ); // 보스는 다시 나오지 않는다
    SW_EXPECT_EQUAL( size_t( 1 ), bench._state.getKills().size() );
}

/**
 * @brief [MetroidvaniaDeathRestTest] 죽으면 가진 통화가 그 자리의 시체가 되고 쉰 곳에서 되살아난다. 통화가 없으면 시체도 없다
 */
SW_TEST_CASE( MetroidvaniaDeathRestTest, DeathDropsACorpse )
{
    DeathRestBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    SW_REQUIRE_TRUE( bench._state.rest( kBench, bench._healthMachine ) );
    bench._healthMachine.kill( sw::nullopt );

    SW_EXPECT_EQUAL( kBench, bench._state.die( kCave, sw::float2{ 1.0f, 2.0f }, bench._healthMachine ) ); // 빈손
    SW_EXPECT_TRUE( bench._state.getCorpse()._bActive == SW_FALSE );
    SW_EXPECT_TRUE( bench._healthMachine.isAlive() );

    bench._wallet.add( kGeo, 120 );
    (void)bench.kill( kSpawnA, kHusk );
    SW_EXPECT_EQUAL( kBench, bench._state.die( kCave, sw::float2{ 3.0f, 4.0f }, bench._healthMachine ) );
    const sw::MetroidvaniaCorpse& corpse = bench._state.getCorpse();
    SW_EXPECT_TRUE( corpse._bActive == SW_TRUE );
    SW_EXPECT_EQUAL( 125, corpse._currency );
    SW_EXPECT_EQUAL( kCave, corpse._area );
    SW_EXPECT_NEAR( 3.0f, corpse._position._x, kDuelTolerance );
    SW_EXPECT_EQUAL( int64( 0 ), bench._wallet.getBalance( kGeo ) );
    SW_EXPECT_TRUE( bench._state.isSpawnAlive( kSpawnA ) ); // 죽어도 적이 다시 나온다
    SW_EXPECT_EQUAL( 1, bench.countEvents( sw::MetroidvaniaDeathRestEventKind::CorpseDropped ) );
}

/**
 * @brief [MetroidvaniaDeathRestTest] 쉰 적이 없으면 되살아날 곳이 빈 이름이다. 되찾기 전에 또 죽으면 앞 시체의 통화는 영영 사라진다(빈손으로 죽어도)
 */
SW_TEST_CASE( MetroidvaniaDeathRestTest, SecondDeathLosesTheFirstCorpse )
{
    DeathRestBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    bench._wallet.add( kGeo, 100 );
    SW_EXPECT_TRUE( bench._state.die( kHall, sw::float2{}, bench._healthMachine ).empty() );

    bench._wallet.add( kGeo, 30 );
    (void)bench._state.die( kCave, sw::float2{}, bench._healthMachine );
    SW_EXPECT_EQUAL( 100, bench._state.getLostCurrency() );
    SW_EXPECT_EQUAL( 30, bench._state.getCorpse()._currency );
    SW_EXPECT_EQUAL( kCave, bench._state.getCorpse()._area );
    SW_EXPECT_EQUAL( 1, bench.countEvents( sw::MetroidvaniaDeathRestEventKind::CurrencyLost ) );

    (void)bench._state.die( kCave, sw::float2{}, bench._healthMachine );
    SW_EXPECT_EQUAL( 130, bench._state.getLostCurrency() );
    SW_EXPECT_TRUE( bench._state.getCorpse()._bActive == SW_FALSE );
}

/**
 * @brief [MetroidvaniaDeathRestTest] 시체는 같은 방이고 반경 경계(포함) 안이어야 되찾고, 되찾으면 통화가 돌아오며 한 번뿐이다
 */
SW_TEST_CASE( MetroidvaniaDeathRestTest, CorpseRecoveryNeedsSameAreaAndRadius )
{
    DeathRestBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    SW_EXPECT_FALSE( bench._state.tryRecoverCorpse( kCave, sw::float2{} ) ); // 시체 없음
    bench._wallet.add( kGeo, 50 );
    (void)bench._state.die( kCave, sw::float2{ 10.0f, 0.0f }, bench._healthMachine );

    SW_EXPECT_FALSE( bench._state.tryRecoverCorpse( kHall, sw::float2{ 10.0f, 0.0f } ) );
    SW_EXPECT_FALSE( bench._state.tryRecoverCorpse( kCave, sw::float2{ 12.5f, 0.0f } ) );
    SW_EXPECT_EQUAL( int64( 0 ), bench._wallet.getBalance( kGeo ) );
    SW_EXPECT_TRUE( bench._state.tryRecoverCorpse( kCave, sw::float2{ 12.0f, 0.0f } ) );
    SW_EXPECT_EQUAL( int64( 50 ), bench._wallet.getBalance( kGeo ) );
    SW_EXPECT_TRUE( bench._state.getCorpse()._bActive == SW_FALSE );
    SW_EXPECT_FALSE( bench._state.tryRecoverCorpse( kCave, sw::float2{ 12.0f, 0.0f } ) );
    SW_EXPECT_EQUAL( 0, bench._state.getLostCurrency() );
}

/**
 * @brief [MetroidvaniaDeathRestTest] 물약은 충전만큼, 살아 있을 때만 마시고, 충전 수는 상한까지 늘며 바로 차고, 강화하면 회복량이 는다
 */
SW_TEST_CASE( MetroidvaniaDeathRestTest, FlaskChargesAndUpgrades )
{
    DeathRestBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    (void)bench._healthMachine.applyDamage( 90.0f );
    SW_EXPECT_TRUE( bench._state.drinkFlask( bench._healthMachine ) );
    SW_EXPECT_NEAR( 40.0f, bench._healthMachine.getHealth(), kDuelTolerance );
    SW_EXPECT_TRUE( bench._state.drinkFlask( bench._healthMachine ) );
    SW_EXPECT_FALSE( bench._state.drinkFlask( bench._healthMachine ) );
    SW_EXPECT_EQUAL( 0, bench._state.getFlaskCharges() );
    SW_EXPECT_NEAR( 70.0f, bench._healthMachine.getHealth(), kDuelTolerance );

    SW_EXPECT_TRUE( bench._state.upgradeFlaskCharges() );
    SW_EXPECT_EQUAL( 1, bench._state.getFlaskCharges() );
    SW_EXPECT_EQUAL( 3, bench._state.getFlaskMaxCharges() );
    SW_EXPECT_FALSE( bench._state.upgradeFlaskCharges() );

    bench._state.upgradeFlaskPotency();
    SW_EXPECT_NEAR( 40.0f, bench._state.computeFlaskHeal(), kDuelTolerance );

    bench._healthMachine.kill( sw::nullopt );
    SW_EXPECT_FALSE( bench._state.drinkFlask( bench._healthMachine ) );
    SW_EXPECT_EQUAL( 1, bench._state.getFlaskCharges() ); // 죽어서 못 마신 충전은 그대로다
}

/**
 * @brief [MetroidvaniaDeathRestTest] 처치는 통화 · 전리품을 주고 위치마다 한 번이다. 모르는 적은 기록도 남기지 않는다. 보스는 플래그를 켠다. 통화가 0 이어도 처치이고, 전리품 카탈로그가 없으면 전리품만 없다
 */
SW_TEST_CASE( MetroidvaniaDeathRestTest, KillsRewardOncePerSpawn )
{
    DeathRestBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    SW_EXPECT_EQUAL( -1, bench.kill( kSpawnA, sw::hashed_string( "ghost" ) ) );
    SW_EXPECT_TRUE( bench._state.isSpawnAlive( kSpawnA ) );

    SW_EXPECT_EQUAL( 5, bench.kill( kSpawnA, kHusk ) );
    SW_EXPECT_EQUAL( int64( 5 ), bench._wallet.getBalance( kGeo ) );
    SW_EXPECT_EQUAL( 2, bench._drops.getItemCount( sw::hashed_string( "scrap" ) ) );
    SW_EXPECT_EQUAL( -1, bench.kill( kSpawnA, kHusk ) );
    SW_EXPECT_EQUAL( int64( 5 ), bench._wallet.getBalance( kGeo ) );

    SW_EXPECT_EQUAL( 0, bench.kill( sw::hashed_string( "spawnC" ), sw::hashed_string( "poor" ) ) );
    SW_EXPECT_FALSE( bench._state.isSpawnAlive( sw::hashed_string( "spawnC" ) ) );

    SW_EXPECT_FALSE( bench._flags.hasFlag( sw::hashed_string( "boss.hornet" ) ) );
    SW_EXPECT_EQUAL( 300, bench.kill( kSpawnB, kHornet ) );
    SW_EXPECT_TRUE( bench._flags.hasFlag( sw::hashed_string( "boss.hornet" ) ) );
    SW_EXPECT_EQUAL( 1, bench.countEvents( sw::MetroidvaniaDeathRestEventKind::BossDefeated ) );

    sw::ItemStackList drops;
    SW_EXPECT_EQUAL( 5, bench._state.registerKill( sw::hashed_string( "spawnD" ), kHusk, bench._flags, nullptr, bench._random, drops ) );
    SW_EXPECT_TRUE( drops.isEmpty() );
}

/**
 * @brief [MetroidvaniaDeathRestTest] 지갑이 없으면 보상도 시체도 없고, 카탈로그가 없으면 쉬기 · 처치 · 되찾기가 모두 거절이다
 */
SW_TEST_CASE( MetroidvaniaDeathRestTest, MissingWalletOrCatalogIsSafe )
{
    DeathRestBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::MetroidvaniaDeathRestState noWallet;
    noWallet.initialize( &bench._catalog, sw::SessionStateRefs{} );
    SW_EXPECT_EQUAL( 5, noWallet.registerKill( kSpawnA, kHusk, bench._flags, nullptr, bench._random, bench._drops ) );
    (void)noWallet.die( kHall, sw::float2{}, bench._healthMachine );
    SW_EXPECT_TRUE( noWallet.getCorpse()._bActive == SW_FALSE );

    sw::MetroidvaniaDeathRestState noCatalog;
    noCatalog.initialize( nullptr, sw::SessionStateRefs{} );
    SW_EXPECT_FALSE( noCatalog.rest( kBench, bench._healthMachine ) );
    SW_EXPECT_EQUAL( -1, noCatalog.registerKill( kSpawnA, kHusk, bench._flags, nullptr, bench._random, bench._drops ) );
    SW_EXPECT_FALSE( noCatalog.drinkFlask( bench._healthMachine ) );
    SW_EXPECT_FALSE( noCatalog.upgradeFlaskCharges() );
    SW_EXPECT_NEAR( 0.0f, noCatalog.computeFlaskHeal(), kDuelTolerance );
}

/**
 * @brief [MetroidvaniaDeathRestTest] 쓰고 읽으면 처치 · 시체 · 되살아날 곳 · 물약이 이어지고, 알림은 비운다
 */
SW_TEST_CASE( MetroidvaniaDeathRestTest, StateRoundTripResumes )
{
    DeathRestBench original;
    SW_REQUIRE_TRUE( original.setUp() );
    original._wallet.add( kGeo, 40 );
    SW_REQUIRE_TRUE( original._state.rest( sw::hashed_string( "second" ), original._healthMachine ) );
    (void)original._state.die( kCave, sw::float2{ 5.0f, 6.0f }, original._healthMachine );
    (void)original.kill( kSpawnB, kHornet );
    (void)original.kill( kSpawnA, kHusk );
    original._state.upgradeFlaskPotency();
    SW_REQUIRE_TRUE( original._state.upgradeFlaskCharges() );
    sw::Archive written;
    original._state.writeState( written );

    DeathRestBench restored;
    SW_REQUIRE_TRUE( restored.setUp() );
    (void)restored.kill( sw::hashed_string( "other" ), kHusk );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored._state.readState( reading ) );
    SW_EXPECT_EQUAL( 0, restored.countEvents( sw::MetroidvaniaDeathRestEventKind::EnemyKilled ) );
    SW_EXPECT_TRUE( restored._state.isSpawnAlive( sw::hashed_string( "other" ) ) );
    SW_EXPECT_FALSE( restored._state.isSpawnAlive( kSpawnA ) );
    SW_EXPECT_EQUAL( 40, restored._state.getCorpse()._currency );
    SW_EXPECT_EQUAL( sw::hashed_string( "second" ), restored._state.getRespawnSite() );
    SW_EXPECT_EQUAL( 3, restored._state.getFlaskMaxCharges() );
    SW_EXPECT_NEAR( 40.0f, restored._state.computeFlaskHeal(), kDuelTolerance );

    SW_REQUIRE_TRUE( restored._state.rest( kBench, restored._healthMachine ) ); // 보스 표시도 이어졌다
    SW_EXPECT_TRUE( restored._state.isSpawnAlive( kSpawnA ) );
    SW_EXPECT_FALSE( restored._state.isSpawnAlive( kSpawnB ) );
}

/**
 * @brief [MetroidvaniaDeathRestTest] 결함: NaN 시체 위치(거리 비교가 거짓이라 어디서든 되찾는다) · 쉬는 곳이 아닌 되살아날 지점 · 겹친 처치 위치를 받았다. 모르는 지점 · 물약 범위 · 잘린 바이트까지 거절하고 그대로 둔다
 */
SW_TEST_CASE( MetroidvaniaDeathRestTest, ReadRejectsBrokenBytes )
{
    DeathRestBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    SW_REQUIRE_TRUE( bench._state.rest( kBench, bench._healthMachine ) );

    const auto expectRejected = [&bench]( const sw::Archive& bytes )
    {
        sw::Archive reading( bytes.getData(), bytes.getSize() );
        SW_EXPECT_FALSE( bench._state.readState( reading ) );
        SW_EXPECT_EQUAL( kBench, bench._state.getRespawnSite() );
        SW_EXPECT_EQUAL( size_t( 0 ), bench._state.getKills().size() );
    };
    const sw::hashed_string none{};
    sw::Archive             nanCorpse;
    MetroidvaniaDeathRestTestUtil::writeStateBytes( nanCorpse, {}, std::numeric_limits<float32>::quiet_NaN(), none, 1, 2 );
    expectRejected( nanCorpse );
    sw::Archive notRestSite;
    MetroidvaniaDeathRestTestUtil::writeStateBytes( notRestSite, {}, 0.0f, sw::hashed_string( "sign" ), 1, 2 );
    expectRejected( notRestSite );
    sw::Archive unknownSite;
    MetroidvaniaDeathRestTestUtil::writeStateBytes( unknownSite, {}, 0.0f, sw::hashed_string( "ghost" ), 1, 2 );
    expectRejected( unknownSite );
    sw::Archive twiceKilled;
    MetroidvaniaDeathRestTestUtil::writeStateBytes( twiceKilled, { kSpawnA, kSpawnA }, 0.0f, none, 1, 2 );
    expectRejected( twiceKilled );
    sw::Archive overFull;
    MetroidvaniaDeathRestTestUtil::writeStateBytes( overFull, {}, 0.0f, none, 3, 2 );
    expectRejected( overFull );
    sw::Archive negative;
    MetroidvaniaDeathRestTestUtil::writeStateBytes( negative, {}, 0.0f, none, -1, 2 );
    expectRejected( negative );

    sw::Archive valid;
    MetroidvaniaDeathRestTestUtil::writeStateBytes( valid, { kSpawnA }, 0.0f, sw::hashed_string( "second" ), 1, 2 );
    sw::Archive truncated( valid.getData(), valid.getSize() - 1 );
    SW_EXPECT_FALSE( bench._state.readState( truncated ) );
    sw::Archive whole( valid.getData(), valid.getSize() );
    SW_EXPECT_TRUE( bench._state.readState( whole ) );
    SW_EXPECT_EQUAL( 1, bench._state.getFlaskCharges() );
}

/**
 * @brief [MetroidvaniaDuelistTest] 카탈로그가 없으면 초기화를 거절하고 공격 · 받기 모두 없는 일이다
 */
SW_TEST_CASE( MetroidvaniaDuelistTest, RequiresACatalog )
{
    SW_EXPECT_LOG( "MetroidvaniaDuelist.MetroidvaniaDuelistRefusedNoCatalog" );
    sw::MetroidvaniaDuelist duelist;
    SW_EXPECT_FALSE( duelist.initialize( nullptr ) );
    SW_EXPECT_FALSE( duelist.tryAttack() );
    SW_EXPECT_FALSE( duelist.tryDodge() );
    SW_EXPECT_TRUE( duelist.receiveAttack( 10.0f, 0.0f )._result == sw::MetroidvaniaDefenseResult::Ignored );
    const sw::MetroidvaniaDuelist other;
    SW_EXPECT_NEAR( 7.0f, duelist.computeAttackDamage( 7.0f, other ), kDuelTolerance );
}

/**
 * @brief [MetroidvaniaDuelistTest] 공격 · 구르기는 스태미나를 쓰고 모자라면 못 한다. 죽으면 못 한다
 */
SW_TEST_CASE( MetroidvaniaDuelistTest, ActionsSpendStamina )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDuelXML, "MetroidvaniaDuelistTest" ) );
    sw::MetroidvaniaDuelist duelist;
    SW_REQUIRE_TRUE( duelist.initialize( &catalog ) );
    SW_EXPECT_TRUE( duelist.tryAttack() );
    SW_EXPECT_NEAR( 85.0f, duelist.getStamina().getValue(), kDuelTolerance );
    SW_EXPECT_TRUE( duelist.tryDodge() );
    SW_EXPECT_NEAR( 65.0f, duelist.getStamina().getValue(), kDuelTolerance );
    SW_REQUIRE_TRUE( duelist.getStamina().trySpend( 55.0f ) ); // 10 남음
    SW_EXPECT_FALSE( duelist.tryAttack() );
    SW_EXPECT_FALSE( duelist.tryDodge() );
    SW_EXPECT_NEAR( 10.0f, duelist.getStamina().getValue(), kDuelTolerance );

    sw::MetroidvaniaDuelist dead;
    SW_REQUIRE_TRUE( dead.initialize( &catalog ) );
    dead.getHealthMachine().kill( sw::nullopt );
    SW_EXPECT_FALSE( dead.tryAttack() );
    SW_EXPECT_FALSE( dead.tryDodge() );
}

/**
 * @brief [MetroidvaniaDuelistTest] 패리는 공격 전 창 경계(포함) 안에 누르면 피해 없이 반격 창을 열고, 누름은 한 번만 쓴다. 너무 이르면 그냥 맞고, 누른 적이 없어도 그냥 맞는다
 */
SW_TEST_CASE( MetroidvaniaDuelistTest, ParryWindowBoundaries )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDuelXML, "MetroidvaniaDuelistTest" ) );
    sw::MetroidvaniaDuelist duelist;
    SW_REQUIRE_TRUE( duelist.initialize( &catalog ) );

    SW_EXPECT_TRUE( duelist.receiveAttack( 10.0f, 0.0f )._result == sw::MetroidvaniaDefenseResult::Hit );
    duelist.pressParry();
    duelist.update( 0.25f );
    sw::MetroidvaniaDefenseOutcome result = duelist.receiveAttack( 10.0f, 5.0f );
    SW_EXPECT_TRUE( result._result == sw::MetroidvaniaDefenseResult::Parried );
    SW_EXPECT_EQUAL( sw::hashed_string( "Perfect" ), result._parryGrade );
    SW_EXPECT_NEAR( 0.0f, result._healthDamage, kDuelTolerance );
    SW_EXPECT_NEAR( 90.0f, duelist.getHealthMachine().getHealth(), kDuelTolerance );
    SW_EXPECT_NEAR( 30.0f, duelist.getHealthMachine().getPoise(), kDuelTolerance ); // 패리는 강인도 피해도 없다
    SW_EXPECT_TRUE( duelist.isRiposteReady() );
    SW_EXPECT_TRUE( duelist.receiveAttack( 10.0f, 0.0f )._result == sw::MetroidvaniaDefenseResult::Hit ); // 한 번만 쓴다

    duelist.pressParry();
    duelist.update( 0.5f ); // 창 0.25 초를 넘겼다
    SW_EXPECT_TRUE( duelist.receiveAttack( 10.0f, 0.0f )._result == sw::MetroidvaniaDefenseResult::Hit );

    duelist.update( 0.25f );
    duelist.pressParry();
    duelist.setGuarding( true );
    SW_EXPECT_TRUE( duelist.receiveAttack( 10.0f, 0.0f )._result == sw::MetroidvaniaDefenseResult::Parried ); // 패리가 막기보다 먼저다
    SW_EXPECT_NEAR( 100.0f, duelist.getStamina().getValue(), kDuelTolerance );
}

/**
 * @brief [MetroidvaniaDuelistTest] 맞으면 받는 피해 배율이 들어가고, 막으면 스태미나를 쓰고 일부 체력 · 절반 강인도만 깎인다. 스태미나가 모자라면 가드가 깨져 다 잃고 그대로 맞는다
 */
SW_TEST_CASE( MetroidvaniaDuelistTest, GuardSpendsStaminaAndCanBreak )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDuelXML, "MetroidvaniaDuelistTest" ) );
    sw::MetroidvaniaDuelist duelist;
    SW_REQUIRE_TRUE( duelist.initialize( &catalog ) );

    duelist.setDamageTakenScale( 2.0f );
    sw::MetroidvaniaDefenseOutcome result = duelist.receiveAttack( 5.0f, 4.0f );
    SW_EXPECT_TRUE( result._result == sw::MetroidvaniaDefenseResult::Hit );
    SW_EXPECT_NEAR( 10.0f, result._healthDamage, kDuelTolerance );
    SW_EXPECT_NEAR( 26.0f, duelist.getHealthMachine().getPoise(), kDuelTolerance );
    duelist.setDamageTakenScale( 0.0f ); // 0 이하는 배율 없음
    duelist.setDamageTakenScale( -3.0f );
    SW_EXPECT_NEAR( 5.0f, duelist.receiveAttack( 5.0f, 0.0f )._healthDamage, kDuelTolerance );

    duelist.setGuarding( true );
    SW_EXPECT_TRUE( duelist.isGuarding() );
    const float32 healthBefore = duelist.getHealthMachine().getHealth();
    result                     = duelist.receiveAttack( 40.0f, 8.0f );
    SW_EXPECT_TRUE( result._result == sw::MetroidvaniaDefenseResult::Blocked );
    SW_EXPECT_NEAR( 10.0f, result._healthDamage, kDuelTolerance ); // 40 × 0.25
    SW_EXPECT_NEAR( healthBefore - 10.0f, duelist.getHealthMachine().getHealth(), kDuelTolerance );
    SW_EXPECT_NEAR( 60.0f, duelist.getStamina().getValue(), kDuelTolerance );
    SW_EXPECT_NEAR( 22.0f, duelist.getHealthMachine().getPoise(), kDuelTolerance ); // 8 의 절반

    result = duelist.receiveAttack( 60.0f, 0.0f ); // 스태미나 정확히 60 — 막는다
    SW_EXPECT_TRUE( result._result == sw::MetroidvaniaDefenseResult::Blocked );
    SW_EXPECT_NEAR( 0.0f, duelist.getStamina().getValue(), kDuelTolerance );

    SW_REQUIRE_TRUE( duelist.getStamina().trySpend( 0.0f ) );
    duelist.getStamina().restore( 10.0f );
    result = duelist.receiveAttack( 20.0f, 2.0f ); // 스태미나 10 < 20
    SW_EXPECT_TRUE( result._result == sw::MetroidvaniaDefenseResult::GuardBroken );
    SW_EXPECT_NEAR( 20.0f, result._healthDamage, kDuelTolerance ); // 다 맞는다(health 는 남아 있다)
    SW_EXPECT_NEAR( 0.0f, duelist.getStamina().getValue(), kDuelTolerance );
    SW_EXPECT_NEAR( 20.0f, duelist.getHealthMachine().getPoise(), kDuelTolerance ); // 강인도도 그대로 깎인다
}

/**
 * @brief [MetroidvaniaDuelistTest] 결함: 음수 피해는 막기 비용이 음수라 가드가 깨졌고, NaN 피해는 막기 비용으로 스태미나를 NaN 으로 만들었다. 없는 피해로 친다
 */
SW_TEST_CASE( MetroidvaniaDuelistTest, InvalidDamageDoesNotBreakTheGuard )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDuelXML, "MetroidvaniaDuelistTest" ) );
    sw::MetroidvaniaDuelist duelist;
    SW_REQUIRE_TRUE( duelist.initialize( &catalog ) );
    duelist.setGuarding( true );

    const float32 arrDamage[] = { -50.0f, std::numeric_limits<float32>::quiet_NaN() };
    for ( const float32 damage : arrDamage )
    {
        const sw::MetroidvaniaDefenseOutcome result = duelist.receiveAttack( damage, damage );
        SW_EXPECT_TRUE( result._result == sw::MetroidvaniaDefenseResult::Blocked );
        SW_EXPECT_NEAR( 0.0f, result._healthDamage, kDuelTolerance );
        SW_EXPECT_NEAR( 100.0f, duelist.getStamina().getValue(), kDuelTolerance );
        SW_EXPECT_NEAR( 100.0f, duelist.getHealthMachine().getHealth(), kDuelTolerance );
        SW_EXPECT_NEAR( 30.0f, duelist.getHealthMachine().getPoise(), kDuelTolerance );
    }
}

/**
 * @brief [MetroidvaniaDuelistTest] 죽었거나 무적이면 받은 일이 없다(스태미나 · 패리 누름을 쓰지 않는다)
 */
SW_TEST_CASE( MetroidvaniaDuelistTest, IgnoredAttacksCostNothing )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDuelXML, "MetroidvaniaDuelistTest" ) );
    sw::MetroidvaniaDuelist duelist;
    SW_REQUIRE_TRUE( duelist.initialize( &catalog ) );
    duelist.setGuarding( true );
    duelist.pressParry();
    duelist.getHealthMachine().setInvulnerable( 1.0f );
    SW_EXPECT_TRUE( duelist.receiveAttack( 10.0f, 5.0f )._result == sw::MetroidvaniaDefenseResult::Ignored );
    SW_EXPECT_NEAR( 100.0f, duelist.getStamina().getValue(), kDuelTolerance );
    SW_EXPECT_FALSE( duelist.isRiposteReady() );
    SW_EXPECT_TRUE( duelist.receiveAttack( 10.0f, 5.0f )._result == sw::MetroidvaniaDefenseResult::Ignored ); // 패리 누름도 남아 있다
    duelist.getHealthMachine().setInvulnerable( 0.0f );

    sw::MetroidvaniaDuelist dead;
    SW_REQUIRE_TRUE( dead.initialize( &catalog ) );
    dead.getHealthMachine().kill( sw::nullopt );
    SW_EXPECT_TRUE( dead.receiveAttack( 10.0f, 5.0f )._result == sw::MetroidvaniaDefenseResult::Ignored );
}

/**
 * @brief [MetroidvaniaDuelistTest] 반격 창은 시간이 지나거나 한 번 쓰면 닫힌다. 강인도가 무너진 상대에게는 창 없이도 배율이 붙고, 풀리면 없어진다. 배율은 겹치지 않는다
 */
SW_TEST_CASE( MetroidvaniaDuelistTest, RiposteAndStaggerMultiplyOnce )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDuelXML, "MetroidvaniaDuelistTest" ) );
    sw::MetroidvaniaDuelist attacker;
    SW_REQUIRE_TRUE( attacker.initialize( &catalog ) );
    sw::MetroidvaniaDuelist target;
    SW_REQUIRE_TRUE( target.initialize( &catalog ) );
    SW_EXPECT_NEAR( 10.0f, attacker.computeAttackDamage( 10.0f, target ), kDuelTolerance );

    attacker.pressParry();
    SW_REQUIRE_TRUE( attacker.receiveAttack( 1.0f, 0.0f )._result == sw::MetroidvaniaDefenseResult::Parried );
    SW_EXPECT_NEAR( 30.0f, attacker.computeAttackDamage( 10.0f, target ), kDuelTolerance );
    SW_EXPECT_FALSE( attacker.isRiposteReady() );
    SW_EXPECT_NEAR( 10.0f, attacker.computeAttackDamage( 10.0f, target ), kDuelTolerance );

    attacker.pressParry();
    SW_REQUIRE_TRUE( attacker.receiveAttack( 1.0f, 0.0f )._result == sw::MetroidvaniaDefenseResult::Parried );
    attacker.update( 1.0f );
    SW_EXPECT_FALSE( attacker.isRiposteReady() );
    SW_EXPECT_NEAR( 10.0f, attacker.computeAttackDamage( 10.0f, target ), kDuelTolerance );

    const sw::MetroidvaniaDefenseOutcome staggered = target.receiveAttack( 1.0f, 30.0f );
    SW_EXPECT_TRUE( staggered._bStaggered == SW_TRUE );
    SW_EXPECT_NEAR( 30.0f, attacker.computeAttackDamage( 10.0f, target ), kDuelTolerance );
    SW_EXPECT_NEAR( 30.0f, attacker.computeAttackDamage( 10.0f, target ), kDuelTolerance ); // 무너진 동안은 계속이다

    attacker.pressParry();
    SW_REQUIRE_TRUE( attacker.receiveAttack( 1.0f, 0.0f )._result == sw::MetroidvaniaDefenseResult::Parried );
    SW_EXPECT_NEAR( 30.0f, attacker.computeAttackDamage( 10.0f, target ), kDuelTolerance ); // 둘 다여도 3 배 한 번

    target.update( 1.0f );
    SW_EXPECT_FALSE( target.getHealthMachine().isPoiseBroken() );
    SW_EXPECT_NEAR( 10.0f, attacker.computeAttackDamage( 10.0f, target ), kDuelTolerance );
}

/**
 * @brief [MetroidvaniaDuelistTest] 쓰고 읽으면 체력 · 스태미나 · 패리 누름 · 반격 창이 이어진다. 결함: NaN 패리 시각 · 무한 배율을 받았다. 범위 밖 시간 · 표시 · 잘린 바이트까지 거절하고 그대로 둔다
 */
SW_TEST_CASE( MetroidvaniaDuelistTest, StateRoundTripAndRejection )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDuelXML, "MetroidvaniaDuelistTest" ) );
    sw::MetroidvaniaDuelist original;
    SW_REQUIRE_TRUE( original.initialize( &catalog ) );
    original.update( 1.0f );
    SW_REQUIRE_TRUE( original.tryAttack() );
    (void)original.receiveAttack( 10.0f, 5.0f );
    original.setGuarding( true );
    original.setDamageTakenScale( 2.0f );
    original.pressParry();

    sw::Archive written;
    original.writeState( written );

    // 뒤에서: 시각(4) 패리 시각(4) 반격 창(4) 배율(4) 막기(1).
    const size_t            size = static_cast<size_t>( written.getSize() );
    sw::MetroidvaniaDuelist target;
    SW_REQUIRE_TRUE( target.initialize( &catalog ) );
    const auto expectRejected = [&]( size_t offsetFromEnd, auto value )
    {
        sw::vector<uint8> listByte( written.getData(), written.getData() + size );
        std::memcpy( listByte.data() + size - offsetFromEnd, &value, sizeof( value ) );
        sw::Archive reading( listByte.data(), listByte.size() );
        SW_EXPECT_FALSE( target.readState( reading ) );
        SW_EXPECT_NEAR( 100.0f, target.getStamina().getValue(), kDuelTolerance );
    };
    const float32 nan      = std::numeric_limits<float32>::quiet_NaN();
    const float32 infinity = std::numeric_limits<float32>::infinity();
    expectRejected( 17, -1.0f );
    expectRejected( 17, nan );
    expectRejected( 13, nan );
    expectRejected( 9, nan );
    expectRejected( 5, 0.0f );
    expectRejected( 5, -1.0f );
    expectRejected( 5, infinity );
    expectRejected( 1, static_cast<uint8>( 2 ) );

    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( target.readState( truncated ) );

    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( target.readState( reading ) );
    SW_EXPECT_NEAR( 1.0f, target.getTime(), kDuelTolerance );
    SW_EXPECT_NEAR( 90.0f, target.getHealthMachine().getHealth(), kDuelTolerance );
    SW_EXPECT_NEAR( 85.0f, target.getStamina().getValue(), kDuelTolerance );
    SW_EXPECT_TRUE( target.isGuarding() );
    SW_EXPECT_TRUE( target.receiveAttack( 10.0f, 0.0f )._result == sw::MetroidvaniaDefenseResult::Parried ); // 눌러 둔 패리가 이어졌다
}
