#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaAbilitySet.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaTrinketLoadout.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"

// MetroidvaniaCatalogTest — 카탈로그: 규칙을 기반 설정으로 옮기기, 패리 창 필수, 원소별 거절.
// MetroidvaniaAbilitySetTest — 능력 잠금: 잠긴 기본 설정, 값 더하기, 입력 거르기, 플래그 복원, 왕복. 결함: 음수 값을 더하는 중에 잘라 결과가 얻은 순서에 달렸고, 복원한 몸이 세이브 전과 달라졌다.
// MetroidvaniaTrinketLoadoutTest — 부적: 슬롯 비용, 넘겨 끼기 조건과 배율, 능력치 합, 왕복(결함은 없었다). 키트 GF_Metroidvania(nogpu).

namespace
{
    constexpr const utf8* kLoadoutXML = R"(<Metroidvania currency="soul">
  <Rules charmNotches="3" overcharmDamageScale="2.5"/>
  <Health max="80" poise="30"/>
  <Stamina max="60" regenRate="5" regenDelay="0.5" exhaustThreshold="10"/>
  <Parry><Window grade="Perfect" early="0.25" late="0.25"/></Parry>
  <Ability id="dash" name="Dash" flag="ability.dash"><Motor dash="1" airDashCount="1" runSpeed="2"/></Ability>
  <Ability id="wall" name="Wall" flag="ability.wall"><Motor wallJump="1"/></Ability>
  <Ability id="jump" name="Jump" flag="ability.jump"><Motor extraJumpCount="1"/></Ability>
  <Ability id="curse" name="Curse" flag="ability.curse"><Motor extraJumpCount="-1" runSpeed="-1"/></Ability>
  <Charm id="light" name="Light" cost="1"><Stats damage="0.5"/></Charm>
  <Charm id="mid" name="Mid" cost="2"><Stats damage="1" speed="2"/></Charm>
  <Charm id="heavy" name="Heavy" cost="3"/>
  <Charm id="free" name="Free" cost="0"/>
</Metroidvania>)";

    const sw::hashed_string kDash{ "dash" };
    const sw::hashed_string kWall{ "wall" };
    const sw::hashed_string kJump{ "jump" };
    const sw::hashed_string kCurse{ "curse" };
    const sw::hashed_string kLight{ "light" };
    const sw::hashed_string kMid{ "mid" };
    const sw::hashed_string kHeavy{ "heavy" };
    const sw::hashed_string kFree{ "free" };

    constexpr float32 kLoadoutTolerance = 1.0e-3f;

    struct MetroidvaniaLoadoutTestUtil
    {
        static void writeNames( sw::Archive& outArchive, const sw::vector<sw::hashed_string>& listName )
        {
            outArchive << static_cast<uint32>( listName.size() );
            for ( const sw::hashed_string& name : listName )
            {
                sw::StateIO::writeName( outArchive, name );
            }
        }

        static void writeLoadoutBytes( sw::Archive& outArchive, const sw::vector<sw::hashed_string>& listOwned, const sw::vector<sw::hashed_string>& listEquipped, int32 notchCount )
        {
            writeNames( outArchive, listOwned );
            writeNames( outArchive, listEquipped );
            outArchive << notchCount;
        }
    };
} // namespace

/**
 * @brief [MetroidvaniaCatalogTest] 규칙을 기반 체력 · 스태미나 설정으로 옮기고, 소울라이크라 기절은 끈다
 */
SW_TEST_CASE( MetroidvaniaCatalogTest, RulesMoveIntoBaseSettings )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kLoadoutXML, "MetroidvaniaCatalogTest" ) );
    const sw::MetroidvaniaRules& rules = catalog.getRules();
    SW_EXPECT_EQUAL( sw::hashed_string( "soul" ), rules._currency );
    SW_EXPECT_NEAR( 80.0f, rules._health._maxHealth, kLoadoutTolerance );
    SW_EXPECT_NEAR( 30.0f, rules._health._poiseMax, kLoadoutTolerance );
    SW_EXPECT_TRUE( rules._health._bDownedEnabled == SW_FALSE );
    SW_EXPECT_NEAR( 60.0f, rules._stamina._max, kLoadoutTolerance );
    SW_EXPECT_NEAR( 10.0f, rules._stamina._exhaustThreshold, kLoadoutTolerance );
    SW_EXPECT_EQUAL( size_t( 1 ), rules._parryJudge.getWindows().size() );
    SW_EXPECT_EQUAL( size_t( 4 ), catalog.getAbilities().size() );
}

/**
 * @brief [MetroidvaniaCatalogTest] 패리 창이 없으면 규칙도 원소도 읽지 않고 실패한다(코드가 대신 정한 창이 없다)
 */
SW_TEST_CASE( MetroidvaniaCatalogTest, ParryWindowIsRequired )
{
    SW_EXPECT_LOG( "MetroidvaniaCatalog.NeedsParryLeastOne" );
    sw::MetroidvaniaCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<Metroidvania><Enemy id="a"/></Metroidvania>)", "noParry" ) );
    SW_EXPECT_TRUE( catalog.findEnemy( sw::hashed_string( "a" ) ) == nullptr );
}

/**
 * @brief [MetroidvaniaCatalogTest] 원소 자기 칸의 오류(모르는 몸 설정 · 플래그 없는 보스)는 그 원소만 넣지 않는다
 */
SW_TEST_CASE( MetroidvaniaCatalogTest, BrokenElementsAreSkipped )
{
    SW_EXPECT_LOG( "MetroidvaniaCatalog.AbilityChangesUnknownMotor" );
    SW_EXPECT_LOG( "MetroidvaniaCatalog.BossNeedsFlagSet" );

    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<Metroidvania>
  <Parry><Window grade="Perfect"/></Parry>
  <Ability id="typo" name="t" flag="f"><Motor jumpp="1"/></Ability>
  <Ability id="fine" name="f" flag="g"><Motor runSpeed="1"/></Ability>
  <Pickup id="goodPickup" area="a" ability="fine"/>
  <Enemy id="flagless" boss="true"/>
  <Enemy id="plain"/>
</Metroidvania>)",
                                              "broken" ) );
    SW_EXPECT_TRUE( catalog.findAbility( sw::hashed_string( "typo" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findAbility( sw::hashed_string( "fine" ) ) != nullptr );
    SW_EXPECT_TRUE( catalog.findPickup( sw::hashed_string( "goodPickup" ) ) != nullptr );
    SW_EXPECT_TRUE( catalog.findEnemy( sw::hashed_string( "flagless" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findEnemy( sw::hashed_string( "plain" ) ) != nullptr );
}

/**
 * @brief [MetroidvaniaCatalogTest] 없는 능력이나 부적을 주는 줍는 것은 로드 실패다(필수 참조 — `engine/settings/datacatalogreferences.xml`). 예전에는 그 줍는 것만 빠진 채 로드가 성공했다
 */
SW_TEST_CASE( MetroidvaniaCatalogTest, PickupNamingAnUnknownGrantFailsTheLoad )
{
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    sw::MetroidvaniaCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<Metroidvania>
  <Parry><Window grade="Perfect"/></Parry>
  <Ability id="fine" name="f" flag="g"/>
  <Pickup id="badAbility" area="a" ability="ghost"/>
  <Pickup id="badCharm" area="a" charm="ghost"/>
</Metroidvania>)",
                                              "broken" ) );
    sw::MetroidvaniaCatalog clean;
    SW_EXPECT_TRUE( clean.loadFromXMLText( R"(<Metroidvania>
  <Parry><Window grade="Perfect"/></Parry>
  <Ability id="fine" name="f" flag="g"/>
  <Charm id="later" name="c"/>
  <Pickup id="goodAbility" area="a" ability="fine"/>
  <Pickup id="goodCharm" area="a" charm="later"/>
</Metroidvania>)",
                                           "clean" ) );
}

/**
 * @brief [MetroidvaniaAbilitySetTest] 몸은 벽 점프 · 공중 점프 · 공중 대시 · 대시가 잠긴 채 시작하고, 능력이 그 값을 열며 수치는 더한다
 */
SW_TEST_CASE( MetroidvaniaAbilitySetTest, AbilitiesUnlockTheLockedBase )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kLoadoutXML, "MetroidvaniaAbilitySetTest" ) );
    sw::MetroidvaniaAbilitySet abilities;
    sw::PlatformerSettings     base;
    base._extraJumpCount = 3;
    base._airDashCount   = 3;
    base._bWallJump      = SW_TRUE;
    abilities.initialize( &catalog, base );

    sw::PlatformerSettings settings;
    abilities.applyToSettings( settings );
    SW_EXPECT_EQUAL( 0, settings._extraJumpCount );
    SW_EXPECT_EQUAL( 0, settings._airDashCount );
    SW_EXPECT_TRUE( settings._bWallJump == SW_FALSE );
    SW_EXPECT_NEAR( base._runSpeed, settings._runSpeed, kLoadoutTolerance );
    SW_EXPECT_FALSE( abilities.canDash() );

    sw::GameFlags flags;
    SW_EXPECT_TRUE( abilities.grantAbility( kDash, flags ) );
    SW_EXPECT_TRUE( flags.hasFlag( sw::hashed_string( "ability.dash" ) ) );
    SW_EXPECT_FALSE( abilities.grantAbility( kDash, flags ) );
    SW_EXPECT_FALSE( abilities.grantAbility( sw::hashed_string( "ghost" ), flags ) );
    SW_EXPECT_TRUE( abilities.grantAbility( kWall, flags ) );
    SW_EXPECT_TRUE( abilities.grantAbility( kJump, flags ) );
    SW_EXPECT_EQUAL( size_t( 3 ), abilities.getAbilities().size() );
    SW_EXPECT_TRUE( abilities.hasAbility( kWall ) );
    SW_EXPECT_FALSE( abilities.hasAbility( kCurse ) );
    SW_EXPECT_TRUE( abilities.canDash() );

    abilities.applyToSettings( settings );
    SW_EXPECT_EQUAL( 1, settings._extraJumpCount );
    SW_EXPECT_EQUAL( 1, settings._airDashCount );
    SW_EXPECT_TRUE( settings._bWallJump == SW_TRUE );
    SW_EXPECT_NEAR( base._runSpeed + 2.0f, settings._runSpeed, kLoadoutTolerance );

    sw::PlatformerMotor2D motor;
    abilities.applyToMotor( motor );
    SW_EXPECT_EQUAL( 1, motor.getSettings()._extraJumpCount );
}

/**
 * @brief [MetroidvaniaAbilitySetTest] 대시를 얻기 전에는 대시 입력을 지우고, 다른 입력은 그대로 둔다
 */
SW_TEST_CASE( MetroidvaniaAbilitySetTest, InputIsFilteredUntilDashIsOwned )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kLoadoutXML, "MetroidvaniaAbilitySetTest" ) );
    sw::MetroidvaniaAbilitySet abilities;
    abilities.initialize( &catalog, sw::PlatformerSettings{} );

    sw::PlatformerInput input;
    input._move                  = sw::float2{ 0.5f, 0.0f };
    input._bDashPressed          = SW_TRUE;
    input._bJumpPressed          = SW_TRUE;
    sw::PlatformerInput filtered = abilities.filterInput( input );
    SW_EXPECT_TRUE( filtered._bDashPressed == SW_FALSE );
    SW_EXPECT_TRUE( filtered._bJumpPressed == SW_TRUE );
    SW_EXPECT_NEAR( 0.5f, filtered._move._x, kLoadoutTolerance );

    sw::GameFlags flags;
    SW_REQUIRE_TRUE( abilities.grantAbility( kDash, flags ) );
    filtered = abilities.filterInput( input );
    SW_EXPECT_TRUE( filtered._bDashPressed == SW_TRUE );
}

/**
 * @brief [MetroidvaniaAbilitySetTest] 결함: 음수 값을 더하는 중에 0 으로 잘라 결과가 얻은 순서에 달렸다(공중 점프 없음 + 하나 잃기 + 하나 얻기). 같은 능력이면 어떤 순서로 얻어도, 플래그로 복원해도 같은 몸이다
 */
SW_TEST_CASE( MetroidvaniaAbilitySetTest, SettingsDoNotDependOnGrantOrder )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kLoadoutXML, "MetroidvaniaAbilitySetTest" ) );
    const sw::PlatformerSettings base{};

    sw::GameFlags              flagsA;
    sw::MetroidvaniaAbilitySet curseFirst;
    curseFirst.initialize( &catalog, base );
    SW_REQUIRE_TRUE( curseFirst.grantAbility( kCurse, flagsA ) );
    SW_REQUIRE_TRUE( curseFirst.grantAbility( kJump, flagsA ) );

    sw::GameFlags              flagsB;
    sw::MetroidvaniaAbilitySet jumpFirst;
    jumpFirst.initialize( &catalog, base );
    SW_REQUIRE_TRUE( jumpFirst.grantAbility( kJump, flagsB ) );
    SW_REQUIRE_TRUE( jumpFirst.grantAbility( kCurse, flagsB ) );

    sw::MetroidvaniaAbilitySet restored;
    restored.initialize( &catalog, base );
    SW_REQUIRE_TRUE( restored.grantAbility( kDash, flagsB ) ); // 복원은 지금 능력을 비운다
    sw::GameFlags flagsC = flagsA;
    SW_EXPECT_EQUAL( 2, restored.restoreFromFlags( flagsC ) );
    SW_EXPECT_FALSE( restored.hasAbility( kDash ) );
    SW_EXPECT_EQUAL( kJump, restored.getAbilities()[0] ); // 카탈로그 순서

    sw::PlatformerSettings settingsA;
    sw::PlatformerSettings settingsB;
    sw::PlatformerSettings settingsC;
    curseFirst.applyToSettings( settingsA );
    jumpFirst.applyToSettings( settingsB );
    restored.applyToSettings( settingsC );
    SW_EXPECT_EQUAL( 0, settingsA._extraJumpCount );
    SW_EXPECT_EQUAL( settingsA._extraJumpCount, settingsB._extraJumpCount );
    SW_EXPECT_EQUAL( settingsA._extraJumpCount, settingsC._extraJumpCount );
    SW_EXPECT_NEAR( settingsA._runSpeed, settingsC._runSpeed, kLoadoutTolerance );

    sw::GameFlags              flagsD;
    sw::MetroidvaniaAbilitySet onlyCurse;
    onlyCurse.initialize( &catalog, base );
    SW_REQUIRE_TRUE( onlyCurse.grantAbility( kCurse, flagsD ) );
    onlyCurse.applyToSettings( settingsA );
    SW_EXPECT_EQUAL( 0, settingsA._extraJumpCount ); // 음수로 내려가지 않는다
}

/**
 * @brief [MetroidvaniaAbilitySetTest] 카탈로그 없이는 능력을 얻지 못하고 기본 설정 그대로다. 몸 설정 이름 표는 대시 · 벽 점프까지 안다
 */
SW_TEST_CASE( MetroidvaniaAbilitySetTest, WithoutCatalogNothingChanges )
{
    sw::MetroidvaniaAbilitySet abilities;
    sw::PlatformerSettings     base;
    base._runSpeed = 12.0f;
    abilities.initialize( nullptr, base );
    sw::GameFlags flags;
    SW_EXPECT_FALSE( abilities.grantAbility( kDash, flags ) );
    SW_EXPECT_EQUAL( 0, abilities.restoreFromFlags( flags ) );
    SW_EXPECT_FALSE( abilities.canDash() );
    sw::PlatformerSettings settings;
    abilities.applyToSettings( settings );
    SW_EXPECT_NEAR( 12.0f, settings._runSpeed, kLoadoutTolerance );

    SW_EXPECT_TRUE( sw::MetroidvaniaAbilitySet::isMotorSettingName( sw::hashed_string( "dash" ) ) );
    SW_EXPECT_TRUE( sw::MetroidvaniaAbilitySet::isMotorSettingName( sw::hashed_string( "wallJump" ) ) );
    SW_EXPECT_TRUE( sw::MetroidvaniaAbilitySet::isMotorSettingName( sw::hashed_string( "runSpeed" ) ) );
    SW_EXPECT_TRUE( sw::MetroidvaniaAbilitySet::isMotorSettingName( sw::hashed_string( "airDashCount" ) ) );
    SW_EXPECT_FALSE( sw::MetroidvaniaAbilitySet::isMotorSettingName( sw::hashed_string( "fly" ) ) );
}

/**
 * @brief [MetroidvaniaAbilitySetTest] 쓰고 읽으면 얻은 순서까지 이어지고, 모르는 능력 · 겹친 능력 · 잘린 바이트 · 카탈로그 없음은 거절해 그대로 둔다
 */
SW_TEST_CASE( MetroidvaniaAbilitySetTest, StateRoundTripAndRejection )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kLoadoutXML, "MetroidvaniaAbilitySetTest" ) );
    sw::MetroidvaniaAbilitySet original;
    original.initialize( &catalog, sw::PlatformerSettings{} );
    sw::GameFlags flags;
    SW_REQUIRE_TRUE( original.grantAbility( kJump, flags ) );
    SW_REQUIRE_TRUE( original.grantAbility( kDash, flags ) );
    sw::Archive written;
    original.writeState( written );

    sw::MetroidvaniaAbilitySet noCatalog;
    sw::Archive                forNoCatalog( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( noCatalog.readState( forNoCatalog ) );

    sw::MetroidvaniaAbilitySet restored;
    restored.initialize( &catalog, sw::PlatformerSettings{} );
    SW_REQUIRE_TRUE( restored.grantAbility( kWall, flags ) );

    const auto expectRejected = [&restored]( const sw::vector<sw::hashed_string>& listName )
    {
        sw::Archive bytes;
        MetroidvaniaLoadoutTestUtil::writeNames( bytes, listName );
        sw::Archive reading( bytes.getData(), bytes.getSize() );
        SW_EXPECT_FALSE( restored.readState( reading ) );
        SW_EXPECT_TRUE( restored.hasAbility( kWall ) );
    };
    expectRejected( { sw::hashed_string( "ghost" ) } );
    expectRejected( { kDash, kDash } );
    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( restored.readState( truncated ) );

    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), restored.getAbilities().size() );
    SW_EXPECT_EQUAL( kJump, restored.getAbilities()[0] );
    SW_EXPECT_EQUAL( kDash, restored.getAbilities()[1] );
}

/**
 * @brief [MetroidvaniaTrinketLoadoutTest] 모르는 부적 · 안 가진 부적 · 이미 낀 부적은 이유를 돌려주고, 슬롯 안이면 끼며, 벗으면 비용이 돌아온다
 */
SW_TEST_CASE( MetroidvaniaTrinketLoadoutTest, EquipReportsWhyItFails )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kLoadoutXML, "MetroidvaniaTrinketLoadoutTest" ) );
    sw::MetroidvaniaTrinketLoadout loadout;
    SW_EXPECT_TRUE( loadout.equip( kLight ) == sw::MetroidvaniaTrinketResult::UnknownCharm ); // 카탈로그 없음
    SW_EXPECT_FALSE( loadout.grantCharm( kLight ) );
    SW_EXPECT_NEAR( 1.0f, loadout.computeDamageTakenScale(), kLoadoutTolerance );
    loadout.initialize( &catalog );
    SW_EXPECT_EQUAL( 3, loadout.getNotchCount() );

    SW_EXPECT_TRUE( loadout.equip( sw::hashed_string( "ghost" ) ) == sw::MetroidvaniaTrinketResult::UnknownCharm );
    SW_EXPECT_TRUE( loadout.equip( kLight ) == sw::MetroidvaniaTrinketResult::NotOwned );
    SW_EXPECT_FALSE( loadout.grantCharm( sw::hashed_string( "ghost" ) ) );
    SW_EXPECT_TRUE( loadout.grantCharm( kLight ) );
    SW_EXPECT_FALSE( loadout.grantCharm( kLight ) );
    SW_EXPECT_TRUE( loadout.equip( kLight ) == sw::MetroidvaniaTrinketResult::Equipped );
    SW_EXPECT_TRUE( loadout.equip( kLight ) == sw::MetroidvaniaTrinketResult::AlreadyEquipped );
    SW_EXPECT_EQUAL( 1, loadout.computeUsedNotches() );
    SW_EXPECT_TRUE( loadout.isEquipped( kLight ) );

    SW_EXPECT_TRUE( loadout.unequip( kMid ) == sw::MetroidvaniaTrinketResult::NotEquipped );
    SW_EXPECT_TRUE( loadout.unequip( kLight ) == sw::MetroidvaniaTrinketResult::Unequipped );
    SW_EXPECT_EQUAL( 0, loadout.computeUsedNotches() );
    SW_EXPECT_TRUE( loadout.unequip( kLight ) == sw::MetroidvaniaTrinketResult::NotEquipped );

    loadout.addNotches( 2 );
    loadout.addNotches( -5 );
    loadout.addNotches( 0 );
    SW_EXPECT_EQUAL( 5, loadout.getNotchCount() );
}

/**
 * @brief [MetroidvaniaTrinketLoadoutTest] 슬롯을 다 채웠으면 넘겨 끼지 못하고, 빈 슬롯이 하나라도 있으면 한 번 넘겨 끼어 배율이 붙으며, 벗으면 풀린다. 규칙이 끄면 넘겨 끼지 못한다
 */
SW_TEST_CASE( MetroidvaniaTrinketLoadoutTest, OvercharmNeedsAFreeNotch )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kLoadoutXML, "MetroidvaniaTrinketLoadoutTest" ) );
    sw::MetroidvaniaTrinketLoadout loadout;
    loadout.initialize( &catalog );
    for ( const sw::hashed_string& charm : { kLight, kMid, kHeavy, kFree } )
    {
        SW_REQUIRE_TRUE( loadout.grantCharm( charm ) );
    }

    SW_EXPECT_TRUE( loadout.equip( kLight ) == sw::MetroidvaniaTrinketResult::Equipped );
    SW_EXPECT_TRUE( loadout.equip( kMid ) == sw::MetroidvaniaTrinketResult::Equipped ); // 정확히 3
    SW_EXPECT_FALSE( loadout.isOvercharmed() );
    SW_EXPECT_TRUE( loadout.equip( kHeavy ) == sw::MetroidvaniaTrinketResult::NotEnoughNotches ); // 빈 슬롯이 없다
    SW_EXPECT_TRUE( loadout.equip( kFree ) == sw::MetroidvaniaTrinketResult::Equipped );          // 비용 0 은 슬롯이 꽉 차도 든다

    SW_EXPECT_TRUE( loadout.unequip( kLight ) == sw::MetroidvaniaTrinketResult::Unequipped );
    SW_EXPECT_TRUE( loadout.equip( kHeavy ) == sw::MetroidvaniaTrinketResult::Overcharmed ); // 2 + 3 > 3
    SW_EXPECT_TRUE( loadout.isOvercharmed() );
    SW_EXPECT_NEAR( 2.5f, loadout.computeDamageTakenScale(), kLoadoutTolerance );
    SW_EXPECT_TRUE( loadout.equip( kLight ) == sw::MetroidvaniaTrinketResult::NotEnoughNotches ); // 이미 넘겼다

    SW_EXPECT_TRUE( loadout.unequip( kHeavy ) == sw::MetroidvaniaTrinketResult::Unequipped );
    SW_EXPECT_FALSE( loadout.isOvercharmed() );
    SW_EXPECT_NEAR( 1.0f, loadout.computeDamageTakenScale(), kLoadoutTolerance );

    catalog.getRules()._bAllowOvercharm = false;
    SW_EXPECT_TRUE( loadout.equip( kHeavy ) == sw::MetroidvaniaTrinketResult::NotEnoughNotches );
}

/**
 * @brief [MetroidvaniaTrinketLoadoutTest] 낀 부적의 능력치를 더하고(먼저 비우지 않는다), 벗은 것은 빠진다
 */
SW_TEST_CASE( MetroidvaniaTrinketLoadoutTest, StatsMergeFromEquippedCharms )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kLoadoutXML, "MetroidvaniaTrinketLoadoutTest" ) );
    sw::MetroidvaniaTrinketLoadout loadout;
    loadout.initialize( &catalog );
    SW_REQUIRE_TRUE( loadout.grantCharm( kLight ) );
    SW_REQUIRE_TRUE( loadout.grantCharm( kMid ) );
    SW_REQUIRE_TRUE( loadout.equip( kLight ) == sw::MetroidvaniaTrinketResult::Equipped );
    SW_REQUIRE_TRUE( loadout.equip( kMid ) == sw::MetroidvaniaTrinketResult::Equipped );

    const sw::hashed_string damage{ "damage" };
    sw::StatBlock           stats;
    stats.merge( catalog.findCharm( kLight )->_stats ); // 앞서 있던 값 0.5
    loadout.mergeStats( stats );
    SW_EXPECT_NEAR( 2.0f, stats.getValue( damage ), kLoadoutTolerance );
    SW_EXPECT_NEAR( 2.0f, stats.getValue( sw::hashed_string( "speed" ) ), kLoadoutTolerance );

    SW_REQUIRE_TRUE( loadout.unequip( kMid ) == sw::MetroidvaniaTrinketResult::Unequipped );
    sw::StatBlock fresh;
    loadout.mergeStats( fresh );
    SW_EXPECT_NEAR( 0.5f, fresh.getValue( damage ), kLoadoutTolerance );
    SW_EXPECT_NEAR( 0.0f, fresh.getValue( sw::hashed_string( "speed" ) ), kLoadoutTolerance );
}

/**
 * @brief [MetroidvaniaTrinketLoadoutTest] 쓰고 읽으면 낀 순서 · 슬롯 수까지 이어지고, 모르는 부적 · 겹침 · 안 가진 부적을 낌 · 음수 슬롯 · 잘린 바이트 · 카탈로그 없음은 거절해 그대로 둔다
 */
SW_TEST_CASE( MetroidvaniaTrinketLoadoutTest, StateRoundTripAndRejection )
{
    sw::MetroidvaniaCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kLoadoutXML, "MetroidvaniaTrinketLoadoutTest" ) );
    sw::MetroidvaniaTrinketLoadout original;
    original.initialize( &catalog );
    SW_REQUIRE_TRUE( original.grantCharm( kLight ) );
    SW_REQUIRE_TRUE( original.grantCharm( kMid ) );
    SW_REQUIRE_TRUE( original.equip( kMid ) == sw::MetroidvaniaTrinketResult::Equipped );
    SW_REQUIRE_TRUE( original.equip( kLight ) == sw::MetroidvaniaTrinketResult::Equipped );
    original.addNotches( 4 );
    sw::Archive written;
    original.writeState( written );

    sw::MetroidvaniaTrinketLoadout noCatalog;
    sw::Archive                    forNoCatalog( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( noCatalog.readState( forNoCatalog ) );

    sw::MetroidvaniaTrinketLoadout restored;
    restored.initialize( &catalog );
    SW_REQUIRE_TRUE( restored.grantCharm( kHeavy ) );
    const auto expectRejected = [&restored]( const sw::vector<sw::hashed_string>& listOwned, const sw::vector<sw::hashed_string>& listEquipped, int32 notchCount )
    {
        sw::Archive bytes;
        MetroidvaniaLoadoutTestUtil::writeLoadoutBytes( bytes, listOwned, listEquipped, notchCount );
        sw::Archive reading( bytes.getData(), bytes.getSize() );
        SW_EXPECT_FALSE( restored.readState( reading ) );
        SW_EXPECT_TRUE( restored.isOwned( kHeavy ) );
        SW_EXPECT_EQUAL( 3, restored.getNotchCount() );
    };
    expectRejected( { sw::hashed_string( "ghost" ) }, {}, 3 );
    expectRejected( { kLight, kLight }, {}, 3 );
    expectRejected( { kLight }, { kMid }, 3 );
    expectRejected( { kLight }, { kLight, kLight }, 3 );
    expectRejected( { kLight }, {}, -1 );
    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( restored.readState( truncated ) );

    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 7, restored.getNotchCount() );
    SW_EXPECT_FALSE( restored.isOwned( kHeavy ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), restored.getEquipped().size() );
    SW_EXPECT_EQUAL( kMid, restored.getEquipped()[0] );
    SW_EXPECT_EQUAL( 3, restored.computeUsedNotches() );
}
