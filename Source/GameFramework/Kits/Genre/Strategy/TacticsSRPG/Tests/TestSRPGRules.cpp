#include "pch.h"

#include "Core/Event/ConsumerGate.h"
#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Catalog/SRPGCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/RunMap.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGBattlefield.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGCombat.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGCommanderAI.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGProgress.h"
#include "GameFramework/Runtime/Character/Navigation/GridReachability.h"

// 택틱스 SRPG 키트의 규칙: 전투 공식(명중 · 피해 · 크리티컬), 공격 순서(반격 · 지원 · 동기 · MAP), 전장(지형 · 이동 · ZOC · 차례 · 개발), AI 계획, 승패, 로그라이트 지도와 캠페인, 카탈로그 오류, 상태 바이트의 범위 검사.
// 숫자가 정확히 떨어지도록 능력치 0 인 파일럿과 명중 100 · 크리티컬 0 인 무기(sure)를 쓴다. 땅 빌리기(`bindLand`)는 기반 쪽이라 보지 않는다. 키트 GF_TacticsSRPG(nogpu).

namespace
{
    constexpr const utf8* kSRPGXML = R"(<SRPGCatalog>
  <PilotCurve base="100" exponent="0" maxLevel="30"/>
  <UnitCurve base="10" exponent="0" maxLevel="10"/>
  <Terrain id="plain" name="Plain"/>
  <Terrain id="forest" name="Forest" cost="2 1 - -" defense="20" evasion="10"/>
  <Terrain id="wall" name="Wall" cost="- - - -"/>
  <Terrain id="lake" name="Lake" domain="Water"/>
  <Terrain id="void" name="Void" domain="Space"/>
  <Weapon id="rifle" name="Rifle" minRange="1" maxRange="3" power="2000" hit="10" crit="5" en="10" ammo="2"/>
  <Weapon id="saber" name="Saber" kind="Melee" maxRange="1" power="3000"/>
  <Weapon id="cannon" name="Cannon" minRange="2" maxRange="5" power="4000" en="10" morale="120" postMove="false" counter="false"/>
  <Weapon id="beam" name="Beam" map="Self" pattern="1,0 2,0" power="2000" hit="999" crit="-999" en="20"/>
  <Weapon id="bomb" name="Bomb" map="Target" pattern="0,0 1,0" minRange="2" maxRange="4" power="2000" hit="999" crit="-999" ammo="1"/>
  <Weapon id="sure" name="Sure" maxRange="3" power="2000" hit="999" crit="-999"/>
  <Weapon id="surecrit" name="SureCrit" maxRange="3" power="2000" hit="999" crit="999"/>
  <Weapon id="long" name="Long" maxRange="6" power="2000" hit="999" crit="-999" counter="false"/>
  <Weapon id="pea" name="Pea" maxRange="3" power="100" hit="999" crit="-999"/>
  <Weapon id="fixed" name="Fixed" maxRange="2" power="2000" hit="999" crit="-999" postMove="false"/>
  <Unit id="gm" name="GM" hp="3000" en="100" move="3" armor="500" mobility="10" aptitude="A A - B" weapons="rifle,saber,cannon,beam,bomb" developsTo="gundam:2"/>
  <Unit id="gundam" name="Gundam" hp="5000" en="200" move="4" armor="800" weapons="sure"/>
  <Unit id="striker" name="Striker" hp="3000" en="50" move="3" armor="500" weapons="sure,surecrit,long"/>
  <Unit id="runner" name="Runner" hp="3000" en="50" move="3" armor="500" weapons="sure"/>
  <Unit id="weak" name="Weak" hp="3000" en="50" move="3" armor="500" weapons="pea,sure"/>
  <Unit id="fixer" name="Fixer" hp="3000" en="50" move="3" armor="500" weapons="fixed"/>
  <Unit id="dummy" name="Dummy" hp="3000" en="0" move="3" armor="500"/>
  <Unit id="big" name="Big" hp="3000" en="0" move="3" armor="500" size="LL"/>
  <Unit id="heavy" name="Heavy" hp="3000" en="0" move="3" armor="99999"/>
  <Pilot id="rookie" name="Rookie" shooting="0" melee="0" reaction="0" awaken="0" defense="0" growShooting="0" growMelee="0" growReaction="0" growAwaken="0" growDefense="0"/>
  <Pilot id="ace" name="Ace" shooting="50" melee="0" reaction="20" awaken="10" defense="0" growShooting="10" growMelee="0" growReaction="0" growAwaken="0" growDefense="0"/>
</SRPGCatalog>)";

    /** @brief 카탈로그와 전장 하나입니다. 지원은 꺼 두고 숫자를 그대로 봅니다. */
    struct SRPGRulesTest
    {
        sw::SRPGCatalog           _catalog{};
        sw::SRPGBattlefield       _field{};
        sw::SRPGSettings          _settings{};
        sw::ConsumerLease         _lease{};
        sw::vector<sw::SRPGEvent> _listEvent{};

        SRPGRulesTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kSRPGXML, "SRPGTest" ) );
            _settings._bSupportAttack  = SW_FALSE;
            _settings._bSupportDefense = SW_FALSE;
            SW_REQUIRE_TRUE( _field.claimEvents( _lease ) );
        }

        void open( int32 width = 8, int32 height = 8 ) { _field.initialize( &_catalog, width, height, "plain", _settings, 7 ); }

        int32 place( const sw::hashed_string& unitID, sw::SRPGTeam team, const sw::int2& cell ) { return _field.addUnit( unitID, "rookie", team, cell ); }

        int32 countEvents( sw::SRPGEvent::Kind kind )
        {
            _field.pollEvents( _lease, _listEvent );
            int32 count = 0;
            for ( const sw::SRPGEvent& event : _listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }

        sw::SRPGUnit& unit( int32 index ) { return *_field.findUnit( index ); }

        /** @brief 유닛 하나를 손으로 쓴 상태를 읽습니다(1x1 전장). 형식이 맞으면 결과는 값의 범위만 가른다. */
        bool readForged( int32 hp, int32 en, int32 morale, int32 dodge, int32 rosterIndex, uint8 team, const sw::int2& cell )
        {
            sw::TurnOrder  order;
            sw::GameRandom random;
            random.setSeed( 3 );
            sw::Archive written;
            written << static_cast<int32>( 1 );
            written << static_cast<int32>( 1 );
            sw::StateIO::writeName( written, "plain" );
            written << static_cast<uint32>( 1 );
            sw::StateIO::writeName( written, "dummy" );
            sw::StateIO::writeName( written, "rookie" );
            written << static_cast<uint32>( 0 );
            sw::LevelProgress{}.writeState( written );
            sw::LevelProgress{}.writeState( written );
            sw::StateIO::writeInt2( written, cell );
            written << hp;
            written << en;
            written << morale;
            written << dodge;
            written << rosterIndex;
            written << team;
            for ( int32 flag = 0; flag < 6; ++flag )
            {
                written << static_cast<uint8>( flag == 0 ? SW_TRUE : SW_FALSE ); // 살아 있음 뒤로 다섯이 꺼져 있다
            }
            order.writeState( written );
            sw::StateIO::writeRandom( written, random );
            written << static_cast<int32>( 1 );
            written << static_cast<int32>( -1 );
            written << static_cast<uint8>( 0 );
            sw::Archive reading( written.getData(), written.getSize() );
            return _field.readState( reading );
        }
    };

    /** @brief 지도 설정을 만듭니다. */
    struct SRPGTestUtil
    {
        static sw::RunMapSettings makeMapSettings( int32 floors, int32 columns, int32 paths )
        {
            sw::RunNodeRule rule;
            rule._kind = "Battle";
            sw::RunMapSettings settings;
            settings._floorCount  = floors;
            settings._columnCount = columns;
            settings._pathCount   = paths;
            settings._listRule.push_back( rule );
            return settings;
        }
    };
} // namespace

/**
 * @brief [SRPGRulesTest] 공식: 위력 × 능력치 × 적성 × 기력 - 방어(지형 포함), 명중과 크리티컬의 항목들, 최소 피해와 비율, 범위 자르기
 */
SW_TEST_CASE_F( SRPGRulesTest, StrikeFormula )
{
    open();
    SW_REQUIRE_TRUE( _field.setTerrain( sw::int2{ 3, 0 }, "forest" ) );
    SW_REQUIRE_TRUE( _field.setTerrain( sw::int2{ 0, 3 }, "lake" ) );
    const int32 rookie = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 ace    = _field.addUnit( "gm", "ace", sw::SRPGTeam::Player, sw::int2{ 1, 1 } );
    const int32 onLake = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 3 } );
    const int32 plainT = place( "gm", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 } );
    const int32 forest = place( "gm", sw::SRPGTeam::Enemy, sw::int2{ 3, 0 } );
    const int32 bigT   = place( "big", sw::SRPGTeam::Enemy, sw::int2{ 4, 0 } );
    const int32 heavyT = place( "heavy", sw::SRPGTeam::Enemy, sw::int2{ 5, 0 } );
    const int32 keen   = place( "striker", sw::SRPGTeam::Player, sw::int2{ 6, 6 } );
    const auto  strike = [this]( int32 attacker, int32 weapon, int32 defender, int32 percent = 100 )
    { return sw::SRPGCombat::computeStrike( _field, attacker, unit( attacker )._cell, weapon, defender, unit( defender )._cell, percent ); };

    const sw::SRPGStrikePreview base = strike( rookie, 0, plainT ); // 소총: 2000 - 500, 명중 70 + 10 - 운동성 10, 크리티컬 5 + 5
    SW_EXPECT_EQUAL( 1500, base._damage );
    SW_EXPECT_EQUAL( 2250, base._critDamage );
    SW_EXPECT_EQUAL( 70, base._hit );
    SW_EXPECT_EQUAL( 10, base._crit );

    const sw::SRPGStrikePreview pilot = strike( ace, 0, plainT ); // 사격 50, 반응 20, 각성 10
    SW_EXPECT_EQUAL( 2500, pilot._damage );
    SW_EXPECT_EQUAL( 90, pilot._hit );
    SW_EXPECT_EQUAL( 15, pilot._crit );
    SW_EXPECT_EQUAL( 2500, strike( ace, 1, plainT )._damage ); // 격투 무기는 격투 능력치(0)를 쓴다: 3000 - 500

    unit( ace )._pilotLevel.setLevel( _catalog.getPilotCurve(), 3 );
    SW_EXPECT_EQUAL( 2900, strike( ace, 0, plainT )._damage ); // 사격 50 + 10 × 2

    const sw::SRPGStrikePreview defended = strike( rookie, 0, forest ); // 방어 500 × 120%, 회피 10
    SW_EXPECT_EQUAL( 1400, defended._damage );
    SW_EXPECT_EQUAL( 60, defended._hit );

    const sw::SRPGStrikePreview water = strike( onLake, 0, plainT ); // 수중 적성 B(90): 공격력 1800, 명중 × 90%
    SW_EXPECT_EQUAL( 1300, water._damage );
    SW_EXPECT_EQUAL( 63, water._hit );

    SW_EXPECT_EQUAL( 90, strike( rookie, 0, bigT )._hit ); // 크기 차 2 × 5, 운동성 0

    unit( plainT )._dodge = 15;
    SW_EXPECT_EQUAL( 55, strike( rookie, 0, plainT )._hit );
    unit( plainT )._dodge = 0;

    unit( rookie )._morale = 150;
    SW_EXPECT_EQUAL( 2500, strike( rookie, 0, plainT )._damage );
    unit( rookie )._morale = 100;

    const sw::SRPGStrikePreview floorDamage = strike( rookie, 0, heavyT, 50 ); // 최소 10 × 50%
    SW_EXPECT_EQUAL( 5, floorDamage._damage );
    SW_EXPECT_EQUAL( 7, floorDamage._critDamage );
    SW_EXPECT_EQUAL( 0, strike( rookie, 0, plainT, -30 )._damage ); // 음수 비율은 0

    SW_EXPECT_EQUAL( 100, strike( keen, 0, plainT )._hit );
    SW_EXPECT_EQUAL( 0, strike( keen, 0, plainT )._crit );
    SW_EXPECT_EQUAL( 100, strike( keen, 1, plainT )._crit );

    SW_EXPECT_FALSE( sw::SRPGCombat::computeStrike( _field, -1, sw::int2{}, 0, plainT, unit( plainT )._cell ).isValid() );
    SW_EXPECT_FALSE( strike( rookie, 9, plainT ).isValid() );
}

/**
 * @brief [SRPGRulesTest] 무기를 쓸 수 있는지: EN · 탄 · 기력 · 이동 뒤 · 없는 번호, 예측의 표적 · 사거리 · MAP 병기 거절
 */
SW_TEST_CASE_F( SRPGRulesTest, WeaponStatusAndForecastRefusals )
{
    open();
    const int32   attacker   = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32   ally       = place( "dummy", sw::SRPGTeam::Player, sw::int2{ 0, 1 } );
    const int32   closeFoe   = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 } );
    const int32   distantFoe = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 7, 7 } );
    sw::SRPGUnit& shooter    = unit( attacker );

    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::InvalidWeapon, _field.computeWeaponStatus( shooter, 9, false ) );
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::InvalidWeapon, _field.computeWeaponStatus( shooter, -1, false ) );
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::Ok, _field.computeWeaponStatus( shooter, 0, false ) );
    shooter._en = 9;
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::NotEnoughEnergy, _field.computeWeaponStatus( shooter, 0, false ) );
    shooter._en = 10; // 딱 맞으면 된다
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::Ok, _field.computeWeaponStatus( shooter, 0, false ) );
    shooter._listAmmo[0] = 0;
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::NoAmmo, _field.computeWeaponStatus( shooter, 0, false ) );
    shooter._listAmmo[0] = 2;
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::LowMorale, _field.computeWeaponStatus( shooter, 2, false ) );
    shooter._morale = 120;
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::Ok, _field.computeWeaponStatus( shooter, 2, false ) );
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::NotAfterMove, _field.computeWeaponStatus( shooter, 2, true ) );

    shooter._en = 100;
    sw::SRPGForecast forecast;
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::InvalidTarget, sw::SRPGCombat::computeForecast( _field, attacker, 0, ally, forecast ) ); // 아군
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::InvalidTarget, sw::SRPGCombat::computeForecast( _field, attacker, 0, 99, forecast ) );
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::InvalidTarget, sw::SRPGCombat::computeForecast( _field, 99, 0, closeFoe, forecast ) );
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::OutOfRange, sw::SRPGCombat::computeForecast( _field, attacker, 0, distantFoe, forecast ) );
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::OutOfRange, sw::SRPGCombat::computeForecast( _field, attacker, 2, closeFoe, forecast ) );    // 최소 사거리 2
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::InvalidWeapon, sw::SRPGCombat::computeForecast( _field, attacker, 3, closeFoe, forecast ) ); // MAP 병기
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::computeForecast( _field, attacker, 0, closeFoe, forecast ) );

    unit( closeFoe )._bAlive = SW_FALSE;
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::InvalidTarget, sw::SRPGCombat::computeForecast( _field, attacker, 0, closeFoe, forecast ) );
}

/**
 * @brief [SRPGRulesTest] 공격 한 번: 피해 · EN과 탄 · 공격 표시 · 기력 · 경험치. 다시 공격하거나 차례가 아니면 아무것도 바꾸지 않는다
 */
SW_TEST_CASE_F( SRPGRulesTest, ExecuteAttackAppliesEverything )
{
    open();
    const int32 attacker = place( "striker", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 rifleman = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 1 } );
    const int32 target   = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 } );
    _field.beginBattle();

    sw::SRPGCombatResult result;
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::CannotAct, sw::SRPGCombat::executeAttack( _field, target, 0, attacker, result ) ); // 적 차례가 아니다

    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( _field, attacker, 0, target, result ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), result._listStrike.size() ); // 표적은 무기가 없어 반격이 없다
    SW_EXPECT_EQUAL( sw::SRPGStrikeRole::Main, result._listStrike[0]._role );
    SW_EXPECT_TRUE( result._listStrike[0]._bHit == SW_TRUE );
    SW_EXPECT_EQUAL( 1500, result._listStrike[0]._damage );
    SW_EXPECT_EQUAL( 1500, unit( target )._hp );
    SW_EXPECT_TRUE( unit( attacker )._bAttacked == SW_TRUE );
    SW_EXPECT_TRUE( unit( attacker )._bMoved == SW_TRUE );
    SW_EXPECT_EQUAL( 102, unit( attacker )._morale );
    SW_EXPECT_EQUAL( 101, unit( target )._morale );
    SW_EXPECT_EQUAL( 2, unit( attacker )._unitLevel.getLevel() ); // 맞힌 경험치 10 — 기체 곡선 10
    SW_EXPECT_EQUAL( 1, countEvents( sw::SRPGEvent::Kind::UnitLevelUp ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::SRPGEvent::Kind::Hit ) );

    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::CannotAct, sw::SRPGCombat::executeAttack( _field, attacker, 0, target, result ) );
    SW_EXPECT_TRUE( result._listStrike.empty() );
    SW_EXPECT_EQUAL( 1500, unit( target )._hp );

    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( _field, rifleman, 0, target, result ) );
    SW_EXPECT_EQUAL( 90, unit( rifleman )._en );
    SW_EXPECT_EQUAL( 1, unit( rifleman )._listAmmo[0] );
}

/**
 * @brief [SRPGRulesTest] 크리티컬은 배율을 곱하고 격파는 격파 경험치 · 기력을 더한다. 격파하면 반격이 없고 슬롯은 남으며 그 셀은 빈다
 */
SW_TEST_CASE_F( SRPGRulesTest, CritAndKill )
{
    open();
    const int32 attacker = place( "striker", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 tough    = place( "weak", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 } );
    _field.beginBattle();
    unit( tough )._hp = 2000;

    sw::SRPGCombatResult result;
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( _field, attacker, 1, tough, result ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), result._listStrike.size() ); // 격파되어 반격이 없다
    SW_EXPECT_TRUE( result._listStrike[0]._bCrit == SW_TRUE );
    SW_EXPECT_EQUAL( 2250, result._listStrike[0]._damage );
    SW_EXPECT_TRUE( result._listStrike[0]._bDestroyed == SW_TRUE );
    SW_EXPECT_TRUE( unit( tough )._bAlive == SW_FALSE );
    SW_EXPECT_EQUAL( 0, unit( tough )._hp );
    SW_EXPECT_EQUAL( 107, unit( attacker )._morale ); // 2 + 5
    SW_EXPECT_EQUAL( 1, countEvents( sw::SRPGEvent::Kind::Destroyed ) );
    SW_EXPECT_EQUAL( int64( 40 ), unit( attacker )._pilotLevel.getTotalXP() ); // 10 + 30
    SW_EXPECT_FALSE( _field.findUnitAt( sw::int2{ 1, 0 } ).has_value() );
    SW_EXPECT_EQUAL( 0, _field.countAlive( sw::SRPGTeam::Enemy ) );

    SW_EXPECT_FALSE( _field.applyDamage( tough, 100, attacker ) ); // 이미 죽었다
    SW_EXPECT_EQUAL( 1, countEvents( sw::SRPGEvent::Kind::Destroyed ) );
}

/**
 * @brief [SRPGRulesTest] 경험치는 레벨 차를 반영하고 최소 1 이다. 기력은 범위를 넘지 않는다. 음수 피해는 치료가 아니다
 */
SW_TEST_CASE_F( SRPGRulesTest, XPMoraleAndDamageBounds )
{
    open();
    const int32 attacker = place( "striker", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 senior   = _field.addUnit( "dummy", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 }, 3 );
    const int32 junior   = _field.addUnit( "dummy", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 2, 0 }, 1 );
    _field.beginBattle();

    sw::SRPGCombatResult result;
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( _field, attacker, 0, senior, result ) );
    SW_EXPECT_EQUAL( int64( 14 ), unit( attacker )._pilotLevel.getTotalXP() ); // 10 + 2 × 2

    sw::SRPGSettings stingy = _settings;
    stingy._xpHit           = 1;
    stingy._xpLevelStep     = 100;
    sw::SRPGBattlefield poor;
    poor.initialize( &_catalog, 8, 8, "plain", stingy, 7 );
    const int32 elder = poor.addUnit( "striker", "rookie", sw::SRPGTeam::Player, sw::int2{ 0, 0 }, 9 );
    const int32 kid   = poor.addUnit( "dummy", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 }, 1 );
    poor.beginBattle();
    const int64 before = poor.findUnit( elder )->_pilotLevel.getTotalXP();
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( poor, elder, 0, kid, result ) );
    SW_EXPECT_EQUAL( before + 1, poor.findUnit( elder )->_pilotLevel.getTotalXP() ); // 음수가 아니라 1

    _field.addMorale( junior, 1000 );
    SW_EXPECT_EQUAL( _settings._moraleMax, unit( junior )._morale );
    _field.addMorale( junior, -1000 );
    SW_EXPECT_EQUAL( _settings._moraleMin, unit( junior )._morale );

    SW_EXPECT_FALSE( _field.applyDamage( junior, -500, -1 ) );
    SW_EXPECT_EQUAL( 3000, unit( junior )._hp );
    _field.grantXP( junior, 0 );
    _field.grantXP( junior, -10 );
    SW_EXPECT_EQUAL( int64( 0 ), unit( junior )._pilotLevel.getTotalXP() );
}

/**
 * @brief [SRPGRulesTest] 반격: 사거리 안의 반격 무기 중 기대 피해가 가장 큰 것을 쓰고 탄을 쓴다. 반격 불가 · 사거리 밖이면 없다
 */
SW_TEST_CASE_F( SRPGRulesTest, CounterAttack )
{
    open();
    const int32 attacker = place( "striker", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 fighter  = place( "weak", sw::SRPGTeam::Enemy, sw::int2{ 2, 0 } );
    const int32 distant  = place( "weak", sw::SRPGTeam::Enemy, sw::int2{ 5, 0 } );
    const int32 unarmed  = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 0, 2 } );
    _field.beginBattle();
    SW_EXPECT_TRUE( sw::SRPGCombat::findCounterWeapon( _field, fighter, attacker, unit( attacker )._cell ) == 1 ); // pea 보다 sure
    SW_EXPECT_FALSE( sw::SRPGCombat::findCounterWeapon( _field, distant, attacker, unit( attacker )._cell ).has_value() );
    SW_EXPECT_FALSE( sw::SRPGCombat::findCounterWeapon( _field, unarmed, attacker, unit( attacker )._cell ).has_value() );
    SW_EXPECT_FALSE( sw::SRPGCombat::findCounterWeapon( _field, 99, attacker, unit( attacker )._cell ).has_value() );

    sw::SRPGCombatResult result;
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( _field, attacker, 0, fighter, result ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), result._listStrike.size() );
    SW_EXPECT_EQUAL( sw::SRPGStrikeRole::Counter, result._listStrike[1]._role );
    SW_EXPECT_EQUAL( 1500, unit( attacker )._hp );

    sw::SRPGForecast forecast;
    (void)sw::SRPGCombat::computeForecast( _field, attacker, 2, distant, forecast ); // long: 사거리 안, 상대의 무기는 닿지 않는다
    SW_EXPECT_FALSE( forecast._counter.isValid() );
}

/**
 * @brief [SRPGRulesTest] 지원 방어: 이웃한 아군이 줄어든 피해로 대신 맞고 그 차례에 한 번만 나선다. 크리티컬에 쓰러질 수 있으면 나서지 않고, HP 많은 쪽이 나선다
 */
SW_TEST_CASE_F( SRPGRulesTest, SupportDefense )
{
    _settings._bSupportDefense = SW_TRUE;
    open();
    const int32 first   = place( "striker", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 second  = place( "striker", sw::SRPGTeam::Player, sw::int2{ 0, 2 } );
    const int32 target  = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 2, 1 } );
    const int32 guard   = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 3, 1 } );
    const int32 fragile = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 2, 2 } );
    _field.beginBattle();
    unit( fragile )._hp = 1125; // 크리티컬 1125 에 쓰러진다
    unit( guard )._hp   = 2500;

    sw::SRPGForecast forecast;
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::computeForecast( _field, first, 0, target, forecast ) );
    SW_EXPECT_TRUE( forecast._supportDefender == guard );
    SW_EXPECT_EQUAL( guard, forecast._attack._defender );
    SW_EXPECT_EQUAL( 750, forecast._attack._damage );

    sw::SRPGCombatResult result;
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( _field, first, 0, target, result ) );
    SW_EXPECT_EQUAL( 3000, unit( target )._hp );
    SW_EXPECT_EQUAL( 1750, unit( guard )._hp );
    SW_EXPECT_TRUE( unit( guard )._bSupportUsed == SW_TRUE );

    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( _field, second, 0, target, result ) );
    SW_EXPECT_EQUAL( 1500, unit( target )._hp ); // 지킬 이가 없다
    SW_EXPECT_EQUAL( 1125, unit( fragile )._hp );
}

/**
 * @brief [SRPGRulesTest] 지원 공격: 이웃한 아군이 함께 치고 무기를 쓴다. 주 공격이 격파하면 나서지 않고 아무것도 쓰지 않는다
 */
SW_TEST_CASE_F( SRPGRulesTest, SupportAttack )
{
    _settings._bSupportAttack = SW_TRUE;
    open();
    const int32 attacker = place( "striker", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 helper   = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 1 } );
    const int32 tall     = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 } );
    const int32 frail    = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 1, 1 } );
    _field.beginBattle();
    unit( tall )._hp  = 100000;
    unit( frail )._hp = 1;

    sw::SRPGCombatResult result;
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( _field, attacker, 0, frail, result ) );
    SW_EXPECT_EQUAL( size_t( 1 ), result._listStrike.size() );
    SW_EXPECT_TRUE( unit( helper )._bSupportUsed == SW_FALSE );
    SW_EXPECT_EQUAL( 100, unit( helper )._en );

    unit( attacker )._bAttacked = SW_FALSE; // 같은 판에서 한 번 더 친다
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( _field, attacker, 0, tall, result ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), result._listStrike.size() );
    SW_EXPECT_EQUAL( sw::SRPGStrikeRole::Support, result._listStrike[1]._role );
    SW_EXPECT_EQUAL( helper, result._listStrike[1]._preview._attacker );
    SW_EXPECT_TRUE( unit( helper )._bSupportUsed == SW_TRUE );
    SW_EXPECT_EQUAL( 90, unit( helper )._en ); // 닿는 무기는 소총뿐이고 그 EN 을 썼다
    SW_EXPECT_EQUAL( 1, unit( helper )._listAmmo[0] );
}

/**
 * @brief [SRPGRulesTest] 동기 공격: 사거리에 둔 아군이 줄어든 피해로 함께 쏘고 무기를 쓰지 않는다. 사거리 밖은 빠지고, 격파되면 남은 타격은 일어나지 않는다
 */
SW_TEST_CASE_F( SRPGRulesTest, SyncAttack )
{
    _settings._bSyncAttack = SW_TRUE;
    open();
    const int32 attacker = place( "striker", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 partner  = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 2 } );
    const int32 outside  = place( "runner", sw::SRPGTeam::Player, sw::int2{ 7, 7 } );
    const int32 target   = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 } );
    (void)outside;
    _field.beginBattle();
    unit( target )._hp           = 100000;
    unit( partner )._listAmmo[0] = 1;

    sw::SRPGForecast forecast;
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::computeForecast( _field, attacker, 0, target, forecast ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), forecast._listSync.size() );
    SW_EXPECT_EQUAL( partner, forecast._listSync[0]._attacker );

    sw::SRPGCombatResult result;
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( _field, attacker, 0, target, result ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), result._listStrike.size() );
    SW_EXPECT_EQUAL( sw::SRPGStrikeRole::Sync, result._listStrike[1]._role );
    SW_EXPECT_EQUAL( forecast._listSync[0]._damage, result._listStrike[1]._preview._damage );
    SW_EXPECT_EQUAL( 100, unit( partner )._en ); // 쓰지 않는다
    SW_EXPECT_EQUAL( 1, unit( partner )._listAmmo[0] );

    sw::SRPGBattlefield second;
    second.initialize( &_catalog, 8, 8, "plain", _settings, 7 );
    const int32 killer = second.addUnit( "striker", "rookie", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    (void)second.addUnit( "striker", "rookie", sw::SRPGTeam::Player, sw::int2{ 0, 2 } );
    const int32 victim = second.addUnit( "dummy", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 } );
    second.beginBattle();
    second.findUnit( victim )->_hp = 1;
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeAttack( second, killer, 0, victim, result ) );
    SW_EXPECT_EQUAL( size_t( 1 ), result._listStrike.size() );
}

/**
 * @brief [SRPGRulesTest] MAP 병기 범위: 쏘는 쪽 기준은 겨눈 방향으로 돌고 격자 밖은 뺀다. 겨눈 셀 기준은 사거리 안이어야 한다
 */
SW_TEST_CASE_F( SRPGRulesTest, MapWeaponCells )
{
    open();
    const int32          gm = place( "gm", sw::SRPGTeam::Player, sw::int2{ 2, 2 } );
    sw::vector<sw::int2> listCell;

    SW_REQUIRE_TRUE( _field.collectMapCells( gm, 3, sw::int2{ 7, 3 }, listCell ) ); // 동쪽이 더 멀다
    SW_REQUIRE_EQUAL( size_t( 2 ), listCell.size() );
    SW_EXPECT_TRUE( listCell[0] == ( sw::int2{ 3, 2 } ) );
    SW_EXPECT_TRUE( listCell[1] == ( sw::int2{ 4, 2 } ) );
    SW_REQUIRE_TRUE( _field.collectMapCells( gm, 3, sw::int2{ 0, 2 }, listCell ) );
    SW_EXPECT_TRUE( listCell[1] == ( sw::int2{ 0, 2 } ) );
    SW_REQUIRE_TRUE( _field.collectMapCells( gm, 3, sw::int2{ 2, 7 }, listCell ) );
    SW_EXPECT_TRUE( listCell[0] == ( sw::int2{ 2, 3 } ) );
    SW_REQUIRE_TRUE( _field.collectMapCells( gm, 3, sw::int2{ 2, 0 }, listCell ) );
    SW_EXPECT_TRUE( listCell[1] == ( sw::int2{ 2, 0 } ) );
    SW_EXPECT_FALSE( _field.collectMapCells( gm, 3, sw::int2{ 2, 2 }, listCell ) ); // 자기 자리는 방향이 없다

    SW_REQUIRE_TRUE( _field.collectMapCells( gm, 4, sw::int2{ 4, 2 }, listCell ) );
    SW_EXPECT_TRUE( listCell[0] == ( sw::int2{ 4, 2 } ) );
    SW_EXPECT_FALSE( _field.collectMapCells( gm, 4, sw::int2{ 3, 2 }, listCell ) ); // 최소 사거리 안
    SW_EXPECT_FALSE( _field.collectMapCells( gm, 4, sw::int2{ 7, 2 }, listCell ) ); // 사거리 밖
    SW_EXPECT_FALSE( _field.collectMapCells( gm, 0, sw::int2{ 3, 2 }, listCell ) ); // MAP 병기가 아니다
    SW_EXPECT_FALSE( _field.collectMapCells( 99, 3, sw::int2{ 3, 2 }, listCell ) );

    const int32 corner = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    SW_REQUIRE_TRUE( _field.collectMapCells( corner, 3, sw::int2{ -5, 0 }, listCell ) );
    SW_EXPECT_TRUE( listCell.empty() );
}

/**
 * @brief [SRPGRulesTest] MAP 공격: 범위의 적마다 치고 반격이 없다. 맞을 유닛이 없으면 쏘지 않는다. 아군은 설정이 있어야 맞고 쏜 자신은 맞지 않는다
 */
SW_TEST_CASE_F( SRPGRulesTest, MapAttack )
{
    open();
    const int32 gm       = place( "gm", sw::SRPGTeam::Player, sw::int2{ 2, 2 } );
    const int32 closeFoe = place( "weak", sw::SRPGTeam::Enemy, sw::int2{ 3, 2 } );
    const int32 next     = place( "weak", sw::SRPGTeam::Enemy, sw::int2{ 4, 2 } );
    (void)place( "dummy", sw::SRPGTeam::Player, sw::int2{ 2, 3 } );
    (void)place( "dummy", sw::SRPGTeam::Third, sw::int2{ 2, 1 } );
    _field.beginBattle();

    sw::SRPGCombatResult result;
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::CannotAct, sw::SRPGCombat::executeMapAttack( _field, closeFoe, 0, sw::int2{ 0, 0 }, result ) );
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::InvalidWeapon, sw::SRPGCombat::executeMapAttack( _field, gm, 0, sw::int2{ 3, 2 }, result ) ); // 일반 무기
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::InvalidTarget, sw::SRPGCombat::executeMapAttack( _field, gm, 3, sw::int2{ 2, 7 }, result ) ); // 아군만 있다
    SW_EXPECT_EQUAL( 100, unit( gm )._en );                                                                                              // 쏘지 않았다
    SW_EXPECT_TRUE( unit( gm )._bAttacked == SW_FALSE );

    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeMapAttack( _field, gm, 3, sw::int2{ 7, 2 }, result ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), result._listStrike.size() );
    SW_EXPECT_EQUAL( sw::SRPGStrikeRole::Map, result._listStrike[0]._role );
    SW_EXPECT_EQUAL( 1500, unit( closeFoe )._hp );
    SW_EXPECT_EQUAL( 1500, unit( next )._hp );
    SW_EXPECT_EQUAL( 80, unit( gm )._en );
    SW_EXPECT_EQUAL( 3000, unit( gm )._hp ); // 반격이 없다
    SW_EXPECT_EQUAL( sw::SRPGWeaponStatus::CannotAct, sw::SRPGCombat::executeMapAttack( _field, gm, 3, sw::int2{ 7, 2 }, result ) );

    _settings._bMapFriendlyFire = SW_TRUE;
    sw::SRPGBattlefield friendly;
    friendly.initialize( &_catalog, 8, 8, "plain", _settings, 7 );
    const int32 shooter = friendly.addUnit( "gm", "rookie", sw::SRPGTeam::Player, sw::int2{ 2, 2 } );
    const int32 buddy   = friendly.addUnit( "dummy", "rookie", sw::SRPGTeam::Player, sw::int2{ 2, 3 } );
    friendly.beginBattle();
    SW_REQUIRE_EQUAL( sw::SRPGWeaponStatus::Ok, sw::SRPGCombat::executeMapAttack( friendly, shooter, 3, sw::int2{ 2, 7 }, result ) );
    SW_EXPECT_EQUAL( 1500, friendly.findUnit( buddy )->_hp );
    SW_EXPECT_EQUAL( 3000, friendly.findUnit( shooter )->_hp );
}

/**
 * @brief [SRPGRulesTest] 시드가 같으면 같은 판정이 나온다(명중률 70 인 소총 여러 번)
 */
SW_TEST_CASE_F( SRPGRulesTest, CombatIsDeterministic )
{
    open();
    sw::SRPGBattlefield twin;
    twin.initialize( &_catalog, 8, 8, "plain", _settings, 7 );
    sw::vector<int32> listAttacker;
    for ( int32 column = 0; column < 6; ++column )
    {
        listAttacker.push_back( place( "gm", sw::SRPGTeam::Player, sw::int2{ column, 0 } ) );
        (void)twin.addUnit( "gm", "rookie", sw::SRPGTeam::Player, sw::int2{ column, 0 } );
    }
    const int32 target = place( "heavy", sw::SRPGTeam::Enemy, sw::int2{ 3, 2 } );
    (void)twin.addUnit( "heavy", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 3, 2 } );
    _field.beginBattle();
    twin.beginBattle();
    for ( const int32 attacker : listAttacker )
    {
        sw::SRPGCombatResult       left;
        sw::SRPGCombatResult       right;
        const sw::SRPGWeaponStatus status = sw::SRPGCombat::executeAttack( _field, attacker, 0, target, left );
        SW_EXPECT_EQUAL( status, sw::SRPGCombat::executeAttack( twin, attacker, 0, target, right ) );
        SW_REQUIRE_EQUAL( left._listStrike.size(), right._listStrike.size() );
        for ( size_t index = 0; index < left._listStrike.size(); ++index )
        {
            SW_EXPECT_EQUAL( left._listStrike[index]._bHit, right._listStrike[index]._bHit );
            SW_EXPECT_EQUAL( left._listStrike[index]._bCrit, right._listStrike[index]._bCrit );
        }
    }
    SW_EXPECT_EQUAL( unit( target )._hp, twin.findUnit( target )->_hp );
}

/**
 * @brief [SRPGRulesTest] 유닛 놓기: 모르는 id · 격자 밖 · 겹침 · 못 들어가는 지형을 거절한다. 지형 칠하기는 격자 안만 센다. 모르는 기본 지형은 알리고 칸은 막힌다
 */
SW_TEST_CASE_F( SRPGRulesTest, PlacingAndTerrain )
{
    SW_EXPECT_LOG( "SRPGBattlefield.CannotPlaceUnitPilot" );
    SW_EXPECT_LOG( "SRPGBattlefield.UnknownDefaultTerrainCells" );
    open( 4, 4 );
    SW_EXPECT_EQUAL( -1, _field.addUnit( "nothing", "rookie", sw::SRPGTeam::Player, sw::int2{ 0, 0 } ) );
    SW_EXPECT_EQUAL( -1, _field.addUnit( "gm", "nobody", sw::SRPGTeam::Player, sw::int2{ 0, 0 } ) );
    SW_EXPECT_EQUAL( -1, place( "gm", sw::SRPGTeam::Player, sw::int2{ 4, 0 } ) );
    SW_EXPECT_EQUAL( -1, place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, -1 } ) );

    SW_EXPECT_EQUAL( 4, _field.fillTerrain( sw::int2{ 3, 1 }, sw::int2{ 2, 0 }, "wall" ) ); // 뒤집어 적어도 된다
    SW_EXPECT_EQUAL( 1, _field.fillTerrain( sw::int2{ 3, 2 }, sw::int2{ 4, 2 }, "wall" ) ); // 격자 안만 센다
    SW_EXPECT_EQUAL( 0, _field.fillTerrain( sw::int2{ 0, 0 }, sw::int2{ 1, 1 }, "nothing" ) );
    SW_EXPECT_FALSE( _field.setTerrain( sw::int2{ 9, 9 }, "wall" ) );
    SW_EXPECT_EQUAL( -1, place( "gm", sw::SRPGTeam::Player, sw::int2{ 2, 0 } ) ); // 벽

    SW_REQUIRE_TRUE( _field.setTerrain( sw::int2{ 1, 3 }, "void" ) );
    SW_EXPECT_EQUAL( -1, place( "gm", sw::SRPGTeam::Player, sw::int2{ 1, 3 } ) ); // 우주 적성 없음
    SW_EXPECT_TRUE( place( "dummy", sw::SRPGTeam::Player, sw::int2{ 1, 3 } ) >= 0 );

    const int32 first = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    SW_REQUIRE_TRUE( first >= 0 );
    SW_EXPECT_EQUAL( -1, place( "gm", sw::SRPGTeam::Enemy, sw::int2{ 0, 0 } ) ); // 겹침
    unit( first )._bAlive = SW_FALSE;
    SW_EXPECT_TRUE( place( "gm", sw::SRPGTeam::Enemy, sw::int2{ 0, 0 } ) >= 0 ); // 격파된 유닛의 셀은 비었다

    sw::SRPGBattlefield noGround;
    noGround.initialize( &_catalog, 2, 2, "nothing", _settings, 7 );
    SW_EXPECT_EQUAL( -1, noGround.addUnit( "gm", "rookie", sw::SRPGTeam::Player, sw::int2{ 0, 0 } ) );
    SW_EXPECT_TRUE( noGround.findTerrainAt( sw::int2{ 0, 0 } ) == nullptr );
    SW_EXPECT_EQUAL( 0, noGround.computeAptitude( *_field.findUnit( first ), sw::int2{ 0, 0 } ) );
}

/**
 * @brief [SRPGRulesTest] 이동: 지형 비용, 벽, 적은 막고 아군은 지나가되 멈추지 못한다. 범위 밖 · 차례 아님 · 두 번째 이동을 거절한다. 제자리는 움직인 것이 아니다
 */
SW_TEST_CASE_F( SRPGRulesTest, MovementRange )
{
    open();
    SW_REQUIRE_TRUE( _field.setTerrain( sw::int2{ 1, 0 }, "forest" ) );
    SW_REQUIRE_TRUE( _field.setTerrain( sw::int2{ 0, 1 }, "wall" ) );
    const int32 mover = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 foe   = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 7, 7 } );

    SW_EXPECT_FALSE( _field.moveUnit( mover, sw::int2{ 1, 0 } ) ); // 전투 전
    _field.beginBattle();

    sw::GridReachability reach;
    _field.computeMoveRange( mover, reach );
    SW_EXPECT_EQUAL( 2, reach.getCost( sw::int2{ 1, 0 } ) ); // 숲 2
    SW_EXPECT_EQUAL( 3, reach.getCost( sw::int2{ 2, 0 } ) );
    SW_EXPECT_FALSE( reach.isReachable( sw::int2{ 0, 1 } ) ); // 벽
    SW_EXPECT_FALSE( reach.isReachable( sw::int2{ 3, 0 } ) ); // 4 가 든다

    SW_EXPECT_FALSE( _field.moveUnit( foe, sw::int2{ 7, 6 } ) ); // 적 차례가 아니다
    SW_EXPECT_FALSE( _field.moveUnit( mover, sw::int2{ 3, 0 } ) );
    SW_EXPECT_TRUE( _field.moveUnit( mover, sw::int2{ 0, 0 } ) );
    SW_EXPECT_TRUE( unit( mover )._bMoved == SW_FALSE );
    SW_EXPECT_TRUE( _field.moveUnit( mover, sw::int2{ 2, 0 } ) );
    SW_EXPECT_TRUE( unit( mover )._cell == ( sw::int2{ 2, 0 } ) );
    SW_EXPECT_FALSE( _field.moveUnit( mover, sw::int2{ 3, 0 } ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::SRPGEvent::Kind::Moved ) );

    sw::SRPGBattlefield blocked;
    blocked.initialize( &_catalog, 8, 8, "plain", _settings, 7 );
    const int32 walker = blocked.addUnit( "gm", "rookie", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    (void)blocked.addUnit( "dummy", "rookie", sw::SRPGTeam::Player, sw::int2{ 1, 0 } );
    (void)blocked.addUnit( "dummy", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 0, 1 } );
    blocked.beginBattle();
    sw::GridReachability blockedReach;
    blocked.computeMoveRange( walker, blockedReach );
    SW_EXPECT_TRUE( blockedReach.isReachable( sw::int2{ 2, 0 } ) );  // 아군을 지나간다
    SW_EXPECT_FALSE( blockedReach.isReachable( sw::int2{ 0, 2 } ) ); // 적은 지나가지 못한다(돌아가면 4 가 든다)
    SW_EXPECT_FALSE( blocked.moveUnit( walker, sw::int2{ 1, 0 } ) ); // 아군 위에 멈추지 못한다
    SW_EXPECT_TRUE( blocked.moveUnit( walker, sw::int2{ 2, 0 } ) );

    sw::GridReachability deadReach;
    blocked.findUnit( walker )->_bAlive = SW_FALSE;
    blocked.computeMoveRange( walker, deadReach );
    SW_EXPECT_FALSE( deadReach.isReachable( sw::int2{ 2, 0 } ) );
}

/**
 * @brief [SRPGRulesTest] ZOC: 적과 이웃한 셀에 들어가면 거기서 멈추고, 처음부터 이웃해 있었으면 떠날 수 있다
 */
SW_TEST_CASE_F( SRPGRulesTest, ZoneOfControl )
{
    _settings._bZoneOfControl = SW_TRUE;
    open();
    const int32 mover = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 foe   = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 2, 1 } );
    _field.beginBattle();
    SW_EXPECT_TRUE( _field.isInEnemyZone( unit( mover ), sw::int2{ 2, 0 } ) );
    SW_EXPECT_FALSE( _field.isInEnemyZone( unit( mover ), sw::int2{ 3, 0 } ) );

    sw::GridReachability reach;
    _field.computeMoveRange( mover, reach );
    SW_EXPECT_TRUE( reach.isReachable( sw::int2{ 2, 0 } ) );
    SW_EXPECT_FALSE( reach.isReachable( sw::int2{ 3, 0 } ) );

    unit( mover )._cell = sw::int2{ 1, 1 }; // 이미 이웃해 있다
    sw::GridReachability fromZone;
    _field.computeMoveRange( mover, fromZone );
    SW_EXPECT_TRUE( fromZone.isReachable( sw::int2{ 0, 1 } ) );
    SW_EXPECT_TRUE( fromZone.isReachable( sw::int2{ 0, 2 } ) );

    unit( foe )._bAlive = SW_FALSE; // 격파된 적은 막지 않는다
    SW_EXPECT_FALSE( _field.isInEnemyZone( unit( mover ), sw::int2{ 2, 0 } ) );
}

/**
 * @brief [SRPGRulesTest] 이동 회피: 걸은 셀만큼 상한까지 쌓이고 자기 차례가 오면 지워진다
 */
SW_TEST_CASE_F( SRPGRulesTest, MoveDodge )
{
    _settings._bMoveDodge = SW_TRUE;
    _settings._dodgeMax   = 12;
    open();
    const int32 first  = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 second = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 7 } );
    (void)place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 7, 7 } );
    _field.beginBattle();
    SW_REQUIRE_TRUE( _field.moveUnit( first, sw::int2{ 2, 0 } ) );
    SW_EXPECT_EQUAL( 10, unit( first )._dodge );
    SW_REQUIRE_TRUE( _field.moveUnit( second, sw::int2{ 3, 7 } ) );
    SW_EXPECT_EQUAL( 12, unit( second )._dodge ); // 15 를 상한으로

    _field.endPhase();
    SW_EXPECT_EQUAL( 10, unit( first )._dodge ); // 적 차례에는 남는다
    _field.endPhase();
    SW_EXPECT_EQUAL( 0, unit( first )._dodge );
    SW_EXPECT_TRUE( unit( first )._bMoved == SW_FALSE );
}

/**
 * @brief [SRPGRulesTest] 위협 범위: 이동 뒤 쓸 수 있는 무기는 갈 수 있는 곳에서, 아니면 지금 자리에서 닿는 곳이다. 이미 움직였으면 지금 자리뿐이다
 */
SW_TEST_CASE_F( SRPGRulesTest, ThreatCells )
{
    open( 12, 1 );
    const int32 runner = place( "runner", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const auto  covers = [this]( int32 index, int32 column )
    {
        sw::vector<sw::int2> listCell;
        _field.collectThreatCells( index, listCell );
        int32 count = 0;
        for ( const sw::int2& cell : listCell )
        {
            count += cell._x == column ? 1 : 0;
        }
        return count;
    };
    SW_EXPECT_EQUAL( 1, covers( runner, 6 ) ); // 이동 3 + 사거리 3, 셀마다 한 번
    SW_EXPECT_EQUAL( 0, covers( runner, 7 ) );
    unit( runner )._bMoved = SW_TRUE;
    SW_EXPECT_EQUAL( 0, covers( runner, 4 ) );
    SW_EXPECT_EQUAL( 1, covers( runner, 3 ) );

    const int32 fixer = place( "fixer", sw::SRPGTeam::Player, sw::int2{ 11, 0 } );
    SW_EXPECT_EQUAL( 1, covers( fixer, 9 ) ); // 이동 뒤에 못 쏘는 무기
    SW_EXPECT_EQUAL( 0, covers( fixer, 8 ) );
    unit( fixer )._bAlive = SW_FALSE;
    SW_EXPECT_EQUAL( 0, covers( fixer, 9 ) );
    SW_EXPECT_EQUAL( 0, covers( 99, 9 ) );
}

/**
 * @brief [SRPGRulesTest] 페이즈: 팀의 모두가 끝나면 다음 팀으로, 한 바퀴 돌면 다음 턴으로 간다. 살아 있는 유닛이 없는 팀은 건너뛰고, 승부가 나면(한 팀만 남음) 넘기지 않는다
 */
SW_TEST_CASE_F( SRPGRulesTest, PhaseFlow )
{
    open();
    const int32 first  = place( "dummy", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 second = place( "dummy", sw::SRPGTeam::Player, sw::int2{ 1, 0 } );
    const int32 enemy  = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 2, 0 } );
    const int32 third  = place( "dummy", sw::SRPGTeam::Third, sw::int2{ 3, 0 } );

    SW_EXPECT_FALSE( _field.canAct( first ) ); // 시작 전
    _field.beginBattle();
    SW_EXPECT_EQUAL( 1, _field.getTurn() );
    SW_EXPECT_EQUAL( sw::SRPGTeam::Player, _field.getPhaseTeam() );
    SW_EXPECT_TRUE( _field.canAct( first ) );
    SW_EXPECT_FALSE( _field.canAct( enemy ) );
    SW_EXPECT_FALSE( _field.canAct( 99 ) );

    _field.endUnitAction( enemy ); // 차례가 아닌 유닛은 아무 일도 없다
    SW_EXPECT_EQUAL( sw::SRPGTeam::Player, _field.getPhaseTeam() );
    _field.endUnitAction( first );
    SW_EXPECT_FALSE( _field.canAct( first ) );
    SW_EXPECT_EQUAL( sw::SRPGTeam::Player, _field.getPhaseTeam() );
    _field.endUnitAction( second );
    SW_EXPECT_EQUAL( sw::SRPGTeam::Enemy, _field.getPhaseTeam() );
    SW_EXPECT_EQUAL( 1, _field.getTurn() );

    _field.endUnitAction( enemy );
    SW_EXPECT_EQUAL( sw::SRPGTeam::Third, _field.getPhaseTeam() );
    _field.endPhase();
    SW_EXPECT_EQUAL( sw::SRPGTeam::Player, _field.getPhaseTeam() );
    SW_EXPECT_EQUAL( 2, _field.getTurn() );
    SW_EXPECT_TRUE( _field.canAct( first ) );

    SW_EXPECT_TRUE( _field.applyDamage( third, 99999, first ) );
    SW_EXPECT_TRUE( _field.applyDamage( enemy, 99999, first ) );
    SW_EXPECT_TRUE( _field.isBattleDecided() );
    _field.endPhase(); // 적과 제3세력이 없다 — 승부가 났으니 페이즈 · 턴이 넘어가지 않는다(전에는 한 바퀴 돌아 턴 3 이 된 뒤에 승리를 판정했다)
    SW_EXPECT_EQUAL( sw::SRPGTeam::Player, _field.getPhaseTeam() );
    SW_EXPECT_EQUAL( 2, _field.getTurn() );

    sw::SRPGBattlefield onlyEnemy;
    onlyEnemy.initialize( &_catalog, 8, 8, "plain", _settings, 7 );
    (void)onlyEnemy.addUnit( "dummy", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 0, 0 } );
    onlyEnemy.beginBattle();
    SW_EXPECT_EQUAL( sw::SRPGTeam::Enemy, onlyEnemy.getPhaseTeam() ); // 아군이 없으면 적부터
}

/**
 * @brief [SRPGRulesTest] 개별 행동 순: 지금 유닛만 행동하고 끝나면 다음 유닛으로, 모두 돌면 다음 턴이다. 격파된 유닛의 차례는 건너뛴다
 */
SW_TEST_CASE_F( SRPGRulesTest, IndividualTurns )
{
    _settings._turnMode = sw::SRPGTurnMode::Individual;
    open();
    const int32 first  = place( "dummy", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 second = _field.addUnit( "dummy", "ace", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 } );
    _field.beginBattle();
    const int32 active = _field.getActiveUnit();
    SW_REQUIRE_TRUE( active == first || active == second );
    const int32 other = active == first ? second : first;
    SW_EXPECT_EQUAL( 1, _field.getTurn() );
    SW_EXPECT_TRUE( _field.canAct( active ) );
    SW_EXPECT_FALSE( _field.canAct( other ) );
    SW_EXPECT_EQUAL( unit( active )._team, _field.getPhaseTeam() );

    _field.endUnitAction( other ); // 차례가 아니다
    SW_EXPECT_EQUAL( active, _field.getActiveUnit() );
    _field.endUnitAction( active );
    SW_EXPECT_EQUAL( other, _field.getActiveUnit() );
    SW_EXPECT_EQUAL( 1, _field.getTurn() );
    _field.endPhase();
    SW_EXPECT_EQUAL( 2, _field.getTurn() );

    const int32 survivor = _field.getActiveUnit();
    const int32 victim   = survivor == first ? second : first;
    SW_EXPECT_TRUE( _field.applyDamage( victim, 99999, survivor ) );
    _field.endUnitAction( survivor );
    SW_EXPECT_TRUE( _field.isBattleDecided() );
    SW_EXPECT_EQUAL( survivor, _field.getActiveUnit() ); // 혼자 남았다 — 승부가 나 차례 · 턴이 넘어가지 않는다
    SW_EXPECT_EQUAL( 2, _field.getTurn() );
}

/**
 * @brief [SRPGRulesTest] 개발: 기체 레벨이 차야 고르고, 새 기체는 가득 차고 기체 레벨 1 이다. 결함: 격파된 유닛도 개발되어 HP 가 가득 찬 채 죽어 있었다
 */
SW_TEST_CASE_F( SRPGRulesTest, Development )
{
    open();
    const int32                   gm = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    sw::vector<sw::hashed_string> listOption;
    _field.collectDevelopOptions( gm, listOption );
    SW_EXPECT_TRUE( listOption.empty() ); // 레벨 1
    SW_EXPECT_FALSE( _field.developUnit( gm, "gundam" ) );

    _field.grantXP( gm, 10 );
    SW_EXPECT_EQUAL( 2, unit( gm )._unitLevel.getLevel() );
    _field.collectDevelopOptions( gm, listOption );
    SW_REQUIRE_EQUAL( size_t( 1 ), listOption.size() );
    SW_EXPECT_FALSE( _field.developUnit( gm, "striker" ) ); // 분기가 아니다

    unit( gm )._bAlive = SW_FALSE;
    _field.collectDevelopOptions( gm, listOption );
    SW_EXPECT_TRUE( listOption.empty() );
    SW_EXPECT_FALSE( _field.developUnit( gm, "gundam" ) );
    unit( gm )._bAlive = SW_TRUE;

    unit( gm )._hp = 1;
    SW_REQUIRE_TRUE( _field.developUnit( gm, "gundam" ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "gundam" ), unit( gm )._pDef->_id );
    SW_EXPECT_EQUAL( 5000, unit( gm )._hp );
    SW_EXPECT_EQUAL( 200, unit( gm )._en );
    SW_EXPECT_EQUAL( 1, unit( gm )._unitLevel.getLevel() );
    SW_EXPECT_EQUAL( size_t( 1 ), unit( gm )._listWeapon.size() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::SRPGEvent::Kind::Developed ) );
}

/**
 * @brief [SRPGRulesTest] 상태 왕복: 유닛 · 지형 · 차례 · 난수가 이어져 같은 판정이 나온다. 크기가 다르면 거절한다
 */
SW_TEST_CASE_F( SRPGRulesTest, StateRoundTrip )
{
    open();
    SW_REQUIRE_TRUE( _field.setTerrain( sw::int2{ 5, 5 }, "forest" ) );
    const int32 attacker = place( "gm", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 target   = place( "heavy", sw::SRPGTeam::Enemy, sw::int2{ 1, 0 } );
    _field.beginBattle();
    sw::SRPGCombatResult result;
    (void)sw::SRPGCombat::executeAttack( _field, attacker, 0, target, result );

    sw::Archive written;
    _field.writeState( written );
    sw::SRPGBattlefield restored;
    restored.initialize( &_catalog, 8, 8, "plain", _settings, 99 );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );

    SW_EXPECT_EQUAL( 1, restored.getTurn() );
    SW_EXPECT_EQUAL( sw::hashed_string( "forest" ), restored.findTerrainAt( sw::int2{ 5, 5 } )->_id );
    SW_REQUIRE_EQUAL( size_t( 2 ), restored.getUnits().size() );
    SW_EXPECT_EQUAL( unit( attacker )._en, restored.findUnit( attacker )->_en );
    SW_EXPECT_EQUAL( unit( attacker )._listAmmo[0], restored.findUnit( attacker )->_listAmmo[0] );
    SW_EXPECT_TRUE( restored.findUnit( attacker )->_bAttacked == SW_TRUE );
    for ( int32 roll = 0; roll < 8; ++roll )
    {
        SW_EXPECT_EQUAL( _field.getRandom().nextInt( 0, 99 ), restored.getRandom().nextInt( 0, 99 ) );
    }

    sw::SRPGBattlefield smaller;
    smaller.initialize( &_catalog, 4, 4, "plain", _settings, 1 );
    sw::Archive readingAgain( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( smaller.readState( readingAgain ) );
}

/**
 * @brief [SRPGRulesTest] 결함: 음수 HP · EN · 기력 · 회피와 범위 밖 명단 슬롯을 받았다 — 거절하고 상태를 둔다. 격자 밖 · 깨진 팀도 거절한다
 */
SW_TEST_CASE_F( SRPGRulesTest, ReadStateRefusesBrokenUnit )
{
    open( 1, 1 );
    const sw::int2 cell{ 0, 0 };
    SW_REQUIRE_TRUE( readForged( 500, 10, 100, 0, -1, 0, cell ) ); // 형식은 맞다
    SW_EXPECT_EQUAL( 500, unit( 0 )._hp );

    SW_EXPECT_FALSE( readForged( -1, 10, 100, 0, -1, 0, cell ) );
    SW_EXPECT_FALSE( readForged( 500, -1, 100, 0, -1, 0, cell ) );
    SW_EXPECT_FALSE( readForged( 500, 10, -1, 0, -1, 0, cell ) );
    SW_EXPECT_FALSE( readForged( 500, 10, 100, -1, -1, 0, cell ) );
    SW_EXPECT_FALSE( readForged( 500, 10, 100, 0, -2, 0, cell ) );
    SW_EXPECT_FALSE( readForged( 500, 10, 100, 0, -1, 3, cell ) );
    SW_EXPECT_FALSE( readForged( 500, 10, 100, 0, -1, 0, sw::int2{ 1, 0 } ) );
    SW_EXPECT_EQUAL( 500, unit( 0 )._hp );
}

/**
 * @brief [SRPGMissionTest] 승패: 패배가 승리보다 앞서고, 조건마다의 승리와 턴 제한의 경계(같은 턴은 아직 넘지 않았다)
 */
SW_TEST_CASE_F( SRPGRulesTest, MissionOutcomes )
{
    open();
    const int32 commander = place( "dummy", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 ally      = place( "dummy", sw::SRPGTeam::Player, sw::int2{ 1, 0 } );
    const int32 boss      = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 7, 7 } );
    const int32 grunt     = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 6, 7 } );
    (void)place( "dummy", sw::SRPGTeam::Third, sw::int2{ 4, 4 } ); // 전멸에 세지 않는다
    _field.setCommander( commander, true );
    _field.setCommander( boss, true );

    sw::SRPGMissionRule rule;
    SW_EXPECT_EQUAL( sw::SRPGResult::Ongoing, sw::SRPGMissionRule::evaluate( _field, rule ) ); // 시작 전
    _field.beginBattle();
    SW_EXPECT_EQUAL( sw::SRPGResult::Ongoing, sw::SRPGMissionRule::evaluate( _field, rule ) );

    rule._objective = sw::SRPGObjective::ReachCell;
    SW_EXPECT_EQUAL( sw::SRPGResult::Ongoing, sw::SRPGMissionRule::evaluate( _field, rule ) ); // 목표 없음
    rule._listGoalCell.push_back( sw::int2{ 1, 0 } );
    SW_EXPECT_EQUAL( sw::SRPGResult::Victory, sw::SRPGMissionRule::evaluate( _field, rule ) );
    unit( ally )._bAlive = SW_FALSE; // 죽은 유닛은 서 있지 않다
    SW_EXPECT_EQUAL( sw::SRPGResult::Ongoing, sw::SRPGMissionRule::evaluate( _field, rule ) );
    unit( ally )._bAlive = SW_TRUE;

    rule._objective = sw::SRPGObjective::DefeatCommander;
    SW_EXPECT_EQUAL( sw::SRPGResult::Ongoing, sw::SRPGMissionRule::evaluate( _field, rule ) );
    unit( boss )._bAlive = SW_FALSE;
    SW_EXPECT_EQUAL( sw::SRPGResult::Victory, sw::SRPGMissionRule::evaluate( _field, rule ) );

    rule._objective = sw::SRPGObjective::DefeatAll;
    SW_EXPECT_EQUAL( sw::SRPGResult::Ongoing, sw::SRPGMissionRule::evaluate( _field, rule ) ); // 졸개가 남았다
    unit( grunt )._bAlive = SW_FALSE;
    SW_EXPECT_EQUAL( sw::SRPGResult::Victory, sw::SRPGMissionRule::evaluate( _field, rule ) );

    unit( commander )._bAlive = SW_FALSE; // 같은 순간의 승리보다 앞선다
    SW_EXPECT_EQUAL( sw::SRPGResult::Defeat, sw::SRPGMissionRule::evaluate( _field, rule ) );
    rule._bLoseOnCommander = SW_FALSE;
    SW_EXPECT_EQUAL( sw::SRPGResult::Victory, sw::SRPGMissionRule::evaluate( _field, rule ) );
    unit( ally )._bAlive = SW_FALSE; // 아군 전멸
    SW_EXPECT_EQUAL( sw::SRPGResult::Defeat, sw::SRPGMissionRule::evaluate( _field, rule ) );
}

/**
 * @brief [SRPGRulesTest] 턴 제한: 버티기는 넘기면 승리, 그 밖에는 넘기면 패배이다. 제한 턴 자체는 아직이고 0 은 제한이 없다. 지휘관이 없는 지휘관 격파는 전멸로 본다
 */
SW_TEST_CASE_F( SRPGRulesTest, MissionTurnLimit )
{
    open();
    (void)place( "dummy", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 foe = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 7, 7 } );
    _field.beginBattle();

    sw::SRPGMissionRule survive;
    survive._objective = sw::SRPGObjective::SurviveTurns;
    survive._turnLimit = 2;
    sw::SRPGMissionRule timed;
    timed._turnLimit = 2;
    sw::SRPGMissionRule endless;
    endless._objective = sw::SRPGObjective::SurviveTurns;

    const auto nextTurn = [this]()
    {
        _field.endPhase();
        _field.endPhase();
    };
    nextTurn();
    SW_REQUIRE_EQUAL( 2, _field.getTurn() );
    SW_EXPECT_EQUAL( sw::SRPGResult::Ongoing, sw::SRPGMissionRule::evaluate( _field, survive ) );
    SW_EXPECT_EQUAL( sw::SRPGResult::Ongoing, sw::SRPGMissionRule::evaluate( _field, timed ) );
    nextTurn();
    SW_EXPECT_EQUAL( sw::SRPGResult::Victory, sw::SRPGMissionRule::evaluate( _field, survive ) );
    SW_EXPECT_EQUAL( sw::SRPGResult::Defeat, sw::SRPGMissionRule::evaluate( _field, timed ) );
    SW_EXPECT_EQUAL( sw::SRPGResult::Ongoing, sw::SRPGMissionRule::evaluate( _field, endless ) );

    sw::SRPGMissionRule leaderless;
    leaderless._objective = sw::SRPGObjective::DefeatCommander;
    SW_EXPECT_EQUAL( sw::SRPGResult::Ongoing, sw::SRPGMissionRule::evaluate( _field, leaderless ) );
    unit( foe )._bAlive = SW_FALSE;
    SW_EXPECT_EQUAL( sw::SRPGResult::Victory, sw::SRPGMissionRule::evaluate( _field, leaderless ) );
}

/**
 * @brief [SRPGRunMapTest] 지도의 모양: 시드 스무 개에서 길이 엇갈리지 않고, 옆 열까지만 이어지며, 어느 길로 가도 보스에 닿는다. 시드가 같으면 같다
 */
SW_TEST_CASE( SRPGRunMapTest, ShapeHoldsForManySeeds )
{
    for ( uint32 seed = 1; seed <= 20; ++seed )
    {
        sw::RunMap map;
        SW_REQUIRE_TRUE( map.generate( SRPGTestUtil::makeMapSettings( 8, 5, 4 ), seed ) );
        const sw::vector<sw::RunNode>& listNode = map.getNodes();
        int32                          boss     = 0;
        for ( const sw::RunNode& node : listNode )
        {
            boss += node._floor == 7 ? 1 : 0;
            SW_EXPECT_TRUE( node._floor == 7 || node._listNext.empty() == false ); // 막힌 길이 없다
            SW_EXPECT_FALSE( node._kind.empty() );
            for ( const int32 next : node._listNext )
            {
                const sw::RunNode& target = listNode[static_cast<size_t>( next )];
                SW_EXPECT_EQUAL( node._floor + 1, target._floor );
                SW_EXPECT_TRUE( node._floor == 6 || sw::MathUtil::abs( target._column - node._column ) <= 1 );
            }
        }
        SW_EXPECT_EQUAL( 1, boss );

        // 아래에서 왼쪽에 있는 쪽의 위가 오른쪽을 넘지 않아야 엇갈리지 않는다
        for ( const sw::RunNode& lhs : listNode )
        {
            for ( const sw::RunNode& rhs : listNode )
            {
                if ( lhs._floor != rhs._floor || lhs._column >= rhs._column || lhs._floor >= 6 )
                    continue;
                for ( const int32 lhsNext : lhs._listNext )
                {
                    for ( const int32 rhsNext : rhs._listNext )
                    {
                        SW_EXPECT_TRUE( listNode[static_cast<size_t>( lhsNext )]._column <= listNode[static_cast<size_t>( rhsNext )]._column );
                    }
                }
            }
        }

        sw::vector<int32> listChoice;
        map.collectChoices( listChoice );
        SW_EXPECT_TRUE( listChoice.size() >= 2 ); // 첫 두 길은 다른 열이다
        int32 steps = 0;
        while ( map.isFinished() == false && listChoice.empty() == false && steps < 16 )
        {
            SW_REQUIRE_TRUE( map.moveTo( listChoice.back() ) );
            map.collectChoices( listChoice );
            ++steps;
        }
        SW_EXPECT_TRUE( map.isFinished() );
        SW_EXPECT_EQUAL( 8, steps );

        sw::RunMap twin;
        SW_REQUIRE_TRUE( twin.generate( SRPGTestUtil::makeMapSettings( 8, 5, 4 ), seed ) );
        SW_REQUIRE_EQUAL( listNode.size(), twin.getNodes().size() );
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            SW_EXPECT_EQUAL( listNode[index]._column, twin.getNodes()[index]._column );
            SW_EXPECT_EQUAL( listNode[index]._listNext.size(), twin.getNodes()[index]._listNext.size() );
        }
    }
}

/**
 * @brief [SRPGRunMapTest] 끝값: 2 층 · 1 열 · 1 길도 지도가 되고, 없는 노드로는 가지 못하며 시작 전에는 끝나지 않았다
 */
SW_TEST_CASE( SRPGRunMapTest, SmallestMaps )
{
    sw::RunMap        empty;
    sw::vector<int32> listChoice;
    empty.collectChoices( listChoice );
    SW_EXPECT_TRUE( listChoice.empty() );
    SW_EXPECT_FALSE( empty.isFinished() );
    SW_EXPECT_FALSE( empty.moveTo( 0 ) );
    SW_EXPECT_TRUE( empty.findNode( 0 ) == nullptr );

    sw::RunMap map;
    SW_REQUIRE_TRUE( map.generate( SRPGTestUtil::makeMapSettings( 2, 1, 1 ), 5 ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), map.getNodes().size() );
    map.collectChoices( listChoice );
    SW_REQUIRE_EQUAL( size_t( 1 ), listChoice.size() );
    SW_EXPECT_FALSE( map.isFinished() );
    SW_EXPECT_FALSE( map.moveTo( 99 ) );
    SW_EXPECT_FALSE( map.moveTo( -1 ) );
    SW_EXPECT_EQUAL( sw::RunMap::kStart, map.getCurrent() );
    SW_REQUIRE_TRUE( map.moveTo( listChoice[0] ) );
    map.collectChoices( listChoice );
    SW_REQUIRE_EQUAL( size_t( 1 ), listChoice.size() );
    SW_REQUIRE_TRUE( map.moveTo( listChoice[0] ) );
    SW_EXPECT_TRUE( map.isFinished() );
    map.collectChoices( listChoice );
    SW_EXPECT_TRUE( listChoice.empty() );
}

/**
 * @brief [SRPGRunMapTest] 종류: 고정 층, 층 범위, 연속 금지를 지킨다(길 하나로 앞뒤가 분명한 지도)
 */
SW_TEST_CASE( SRPGRunMapTest, NodeKinds )
{
    sw::RunNodeRule battle;
    battle._kind = "Battle";
    sw::RunNodeRule rest;
    rest._kind                  = "Rest";
    rest._weight                = 100.0f;
    rest._bNoRepeat             = true;
    rest._minFloor              = 2;
    sw::RunMapSettings settings = SRPGTestUtil::makeMapSettings( 12, 1, 1 );
    settings._listRule          = { battle, rest };
    settings._listForcedFloor.assign( 12, sw::hashed_string{} );
    settings._listForcedFloor[11] = "Boss";

    sw::RunMap map;
    SW_REQUIRE_TRUE( map.generate( settings, 9 ) );
    SW_REQUIRE_EQUAL( size_t( 12 ), map.getNodes().size() );
    sw::vector<sw::hashed_string> listKind( 12 );
    for ( const sw::RunNode& node : map.getNodes() )
    {
        listKind[static_cast<size_t>( node._floor )] = node._kind;
    }
    SW_EXPECT_EQUAL( sw::hashed_string( "Boss" ), listKind[11] );
    SW_EXPECT_EQUAL( sw::hashed_string( "Battle" ), listKind[0] ); // 최소 층 전
    SW_EXPECT_EQUAL( sw::hashed_string( "Battle" ), listKind[1] );
    int32 restCount = 0;
    for ( size_t floor = 0; floor + 1 < listKind.size(); ++floor )
    {
        restCount += listKind[floor] == sw::hashed_string( "Rest" ) ? 1 : 0;
        SW_EXPECT_FALSE( listKind[floor] == sw::hashed_string( "Rest" ) && listKind[floor + 1] == sw::hashed_string( "Rest" ) );
    }
    SW_EXPECT_TRUE( restCount >= 2 ); // 무게가 커서 많이 나온 판이다
}

/**
 * @brief [SRPGRunMapTest] 결함: 층 수가 음수인 설정을 읽으면 표를 터무니없는 크기로 잡아 멈췄다. 깨진 원소가 있으면 출력을 두고 실패한다. 음수 층 번호는 끝에서 센다
 */
SW_TEST_CASE( SRPGRunMapTest, LoadSettingsRefusesBrokenData )
{
    constexpr const utf8* kGoodXML = R"(<RunMap floors="4" columns="3" paths="2"><Node kind="Battle" weight="5"/><Floor index="-1" kind="Boss"/></RunMap>)";
    sw::RunMapSettings    settings;
    SW_REQUIRE_TRUE( sw::RunMap::loadSettings( kGoodXML, "SRPGTest", settings ) );
    SW_EXPECT_EQUAL( 4, settings._floorCount );
    SW_REQUIRE_EQUAL( size_t( 4 ), settings._listForcedFloor.size() );
    SW_EXPECT_EQUAL( sw::hashed_string( "Boss" ), settings._listForcedFloor[3] );

    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    constexpr const utf8* kNegativeXML = R"(<RunMap floors="-5"><Node kind="Battle"/></RunMap>)";
    SW_EXPECT_FALSE( sw::RunMap::loadSettings( kNegativeXML, "SRPGTest", settings ) );
    SW_EXPECT_EQUAL( 4, settings._floorCount );

    SW_EXPECT_LOG( "RunMap.FloorIndexOutsideFloors" );
    constexpr const utf8* kOutsideXML = R"(<RunMap floors="4"><Node kind="Battle"/><Floor index="9" kind="Boss"/></RunMap>)";
    SW_EXPECT_FALSE( sw::RunMap::loadSettings( kOutsideXML, "SRPGTest", settings ) );

    SW_EXPECT_LOG( "RunMap.RunMapNeedsLeastOne" );
    constexpr const utf8* kNoRuleXML = R"(<RunMap floors="4"/>)";
    SW_EXPECT_FALSE( sw::RunMap::loadSettings( kNoRuleXML, "SRPGTest", settings ) );

    SW_EXPECT_LOG( "Xml.UnknownElement" );
    constexpr const utf8* kUnknownXML = R"(<RunMap floors="4"><Node kind="Battle"/><Mystery/></RunMap>)";
    SW_EXPECT_FALSE( sw::RunMap::loadSettings( kUnknownXML, "SRPGTest", settings ) );
}

/**
 * @brief [SRPGRunMapTest] 결함: 시드 0 은 난수의 고정점이라 단언에 걸렸다 — 오류로 거절하고 지도를 둔다. 범위 밖 설정도 같다
 */
SW_TEST_CASE( SRPGRunMapTest, GenerateRefusesBadInput )
{
    SW_EXPECT_LOG( "RunMap.RunMapRefusedSeedZero" );
    SW_EXPECT_LOG( "RunMap.RunMapRefusedSettings" );
    sw::RunMap map;
    SW_REQUIRE_TRUE( map.generate( SRPGTestUtil::makeMapSettings( 3, 3, 2 ), 5 ) );
    const size_t nodeCount = map.getNodes().size();
    SW_EXPECT_FALSE( map.generate( SRPGTestUtil::makeMapSettings( 8, 3, 2 ), 0 ) );
    SW_EXPECT_FALSE( map.generate( SRPGTestUtil::makeMapSettings( 1, 3, 2 ), 5 ) );
    SW_EXPECT_FALSE( map.generate( SRPGTestUtil::makeMapSettings( 3, 0, 2 ), 5 ) );
    SW_EXPECT_EQUAL( nodeCount, map.getNodes().size() );
}

/**
 * @brief [SRPGRunMapTest] 왕복은 위치와 길을 잇는다. 결함: 칸 표의 노드 번호를 보지 않아, 깨진 칸을 고르면 현재 위치가 노드 밖이 되어 범위 밖을 읽었다. 노드의 층 · 열도 본다
 */
SW_TEST_CASE( SRPGRunMapTest, ReadStateChecksCells )
{
    sw::RunMap map;
    SW_REQUIRE_TRUE( map.generate( SRPGTestUtil::makeMapSettings( 5, 3, 2 ), 5 ) );
    sw::vector<int32> listChoice;
    map.collectChoices( listChoice );
    SW_REQUIRE_TRUE( map.moveTo( listChoice[0] ) );

    sw::Archive written;
    map.writeState( written );
    sw::RunMap  restored;
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( map.getCurrent(), restored.getCurrent() );
    SW_EXPECT_EQUAL( map.getFloorCount(), restored.getFloorCount() );
    sw::vector<int32> listRestored;
    restored.collectChoices( listRestored );
    map.collectChoices( listChoice );
    SW_EXPECT_EQUAL( listChoice.size(), listRestored.size() );

    const auto readForged = [&restored]( int32 floor, int32 column, int32 cell, int32 current )
    {
        sw::Archive forged;
        forged << static_cast<uint32>( 1 );
        sw::StateIO::writeName( forged, "Battle" );
        forged << floor;
        forged << column;
        forged << static_cast<uint32>( 0 );
        forged << static_cast<uint32>( 1 ); // 칸 하나
        forged << cell;
        forged << static_cast<int32>( 1 );
        forged << static_cast<int32>( 1 );
        forged << current;
        sw::Archive forgedReading( forged.getData(), forged.getSize() );
        return restored.readState( forgedReading );
    };
    SW_EXPECT_FALSE( readForged( 0, 0, 5, sw::RunMap::kStart ) );
    SW_EXPECT_FALSE( readForged( 0, 0, -2, sw::RunMap::kStart ) );
    SW_EXPECT_FALSE( readForged( 9, 0, 0, sw::RunMap::kStart ) );
    SW_EXPECT_FALSE( readForged( 0, -1, 0, sw::RunMap::kStart ) );
    SW_EXPECT_FALSE( readForged( 0, 0, 0, 7 ) );
    SW_EXPECT_EQUAL( map.getFloorCount(), restored.getFloorCount() ); // 거절은 상태를 두었다

    SW_EXPECT_TRUE( readForged( 0, 0, 0, sw::RunMap::kStart ) ); // 형식은 맞다
    restored.collectChoices( listRestored );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRestored.size() );
    SW_EXPECT_TRUE( restored.moveTo( 0 ) );
}

/**
 * @brief [SRPGCampaignTest] 한 판: 명단을 내보내고 레벨을 되돌리며 격파된 유닛을 잃는다. 작전 중에는 다른 작전을 열지 못하고, 보스를 이기면 끝난다
 */
SW_TEST_CASE_F( SRPGRulesTest, CampaignRun )
{
    sw::RunMapSettings settings = SRPGTestUtil::makeMapSettings( 2, 1, 1 );
    settings._listForcedFloor.assign( 2, sw::hashed_string{} );
    settings._listForcedFloor[1] = "Boss";
    sw::SRPGCampaign campaign;
    SW_REQUIRE_TRUE( campaign.initialize( settings, 5 ) );
    SW_EXPECT_EQUAL( -1, campaign.addRosterEntry( _catalog, "nothing", "rookie" ) );
    SW_EXPECT_EQUAL( -1, campaign.addRosterEntry( _catalog, "gm", "nobody" ) );
    SW_EXPECT_EQUAL( 0, campaign.addRosterEntry( _catalog, "gm", "rookie", 3 ) );
    SW_EXPECT_EQUAL( 1, campaign.addRosterEntry( _catalog, "dummy", "rookie" ) );
    SW_EXPECT_TRUE( campaign.getMissionKind().empty() ); // 아직 어디에도 없다

    sw::vector<int32> listChoice;
    campaign.collectChoices( listChoice );
    SW_REQUIRE_EQUAL( size_t( 1 ), listChoice.size() );
    SW_EXPECT_FALSE( campaign.beginMission( 99 ) );
    SW_EXPECT_FALSE( campaign.isInMission() );
    SW_REQUIRE_TRUE( campaign.beginMission( listChoice[0] ) );
    SW_EXPECT_TRUE( campaign.isInMission() );
    SW_EXPECT_EQUAL( sw::hashed_string( "Battle" ), campaign.getMissionKind() );
    campaign.collectChoices( listChoice );
    SW_EXPECT_FALSE( campaign.beginMission( listChoice[0] ) ); // 작전 중

    open();
    SW_EXPECT_EQUAL( 2, campaign.deployRoster( _field, {
                                                           sw::int2{0, 0},
                                                           sw::int2{1, 0}
    } ) );
    SW_EXPECT_EQUAL( 3, unit( 0 )._pilotLevel.getLevel() );
    SW_EXPECT_EQUAL( 1, unit( 1 )._rosterIndex );
    _field.grantXP( 0, 10 ); // 기체 레벨 2
    SW_REQUIRE_TRUE( _field.developUnit( 0, "gundam" ) );
    _field.grantXP( 0, 10 );
    unit( 1 )._bAlive = SW_FALSE;

    campaign.completeMission( _field, sw::SRPGResult::Ongoing ); // 끝나지 않았다
    SW_EXPECT_TRUE( campaign.isInMission() );
    campaign.completeMission( _field, sw::SRPGResult::Victory );
    SW_EXPECT_FALSE( campaign.isInMission() );
    SW_EXPECT_FALSE( campaign.isFailed() );
    SW_EXPECT_FALSE( campaign.isCleared() );                                           // 보스 전이다
    SW_EXPECT_EQUAL( sw::hashed_string( "gundam" ), campaign.getRoster()[0]._unitID ); // 개발이 이어진다
    SW_EXPECT_EQUAL( 2, campaign.getRoster()[0]._unitLevel.getLevel() );
    SW_EXPECT_TRUE( campaign.getRoster()[1]._bLost == SW_TRUE );
    campaign.completeMission( _field, sw::SRPGResult::Defeat ); // 작전 밖에서는 아무 일도 없다
    SW_EXPECT_FALSE( campaign.isFailed() );

    campaign.collectChoices( listChoice );
    SW_REQUIRE_TRUE( campaign.beginMission( listChoice[0] ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "Boss" ), campaign.getMissionKind() );
    open();
    SW_EXPECT_EQUAL( 1, campaign.deployRoster( _field, {
                                                           sw::int2{0, 0},
                                                           sw::int2{1, 0}
    } ) ); // 잃은 유닛은 나가지 않는다
    SW_EXPECT_EQUAL( 1, static_cast<int32>( _field.getUnits().size() ) );
    campaign.completeMission( _field, sw::SRPGResult::Victory );
    SW_EXPECT_TRUE( campaign.isCleared() );
}

/**
 * @brief [SRPGCampaignTest] 패배하거나 명단을 모두 잃으면 판이 끝나고 다음 작전을 열지 못한다. 칸이 모자라면 앞의 명단만 나간다. 범위 밖 지도는 거절한다
 */
SW_TEST_CASE_F( SRPGRulesTest, CampaignFailure )
{
    sw::SRPGCampaign defeated;
    SW_REQUIRE_TRUE( defeated.initialize( SRPGTestUtil::makeMapSettings( 3, 1, 1 ), 5 ) );
    (void)defeated.addRosterEntry( _catalog, "gm", "rookie" );
    (void)defeated.addRosterEntry( _catalog, "gm", "rookie" );
    sw::vector<int32> listChoice;
    defeated.collectChoices( listChoice );
    SW_REQUIRE_TRUE( defeated.beginMission( listChoice[0] ) );
    open();
    SW_EXPECT_EQUAL( 1, defeated.deployRoster( _field, {
                                                           sw::int2{ 0, 0 }
    } ) ); // 칸이 하나다
    SW_EXPECT_EQUAL( 0, defeated.deployRoster( _field, {} ) );
    defeated.completeMission( _field, sw::SRPGResult::Defeat );
    SW_EXPECT_TRUE( defeated.isFailed() );
    SW_EXPECT_FALSE( defeated.isCleared() );
    defeated.collectChoices( listChoice );
    SW_EXPECT_FALSE( defeated.beginMission( listChoice[0] ) );

    sw::SRPGCampaign wiped;
    SW_REQUIRE_TRUE( wiped.initialize( SRPGTestUtil::makeMapSettings( 3, 1, 1 ), 5 ) );
    (void)wiped.addRosterEntry( _catalog, "gm", "rookie" );
    wiped.collectChoices( listChoice );
    SW_REQUIRE_TRUE( wiped.beginMission( listChoice[0] ) );
    open();
    SW_EXPECT_EQUAL( 1, wiped.deployRoster( _field, {
                                                        sw::int2{ 0, 0 }
    } ) );
    unit( 0 )._bAlive = SW_FALSE;
    wiped.completeMission( _field, sw::SRPGResult::Victory ); // 이겼지만 남은 이가 없다
    SW_EXPECT_TRUE( wiped.isFailed() );

    SW_EXPECT_LOG( "RunMap.RunMapRefusedSettings" );
    sw::SRPGCampaign refused;
    SW_EXPECT_FALSE( refused.initialize( SRPGTestUtil::makeMapSettings( 1, 1, 1 ), 5 ) );
}

/**
 * @brief [SRPGCampaignTest] 작전 시드는 판 시드와 노드로 정해진다. 왕복은 명단과 깃발을 지키고 깨진 깃발은 거절한다
 */
SW_TEST_CASE_F( SRPGRulesTest, CampaignState )
{
    sw::SRPGCampaign campaign;
    SW_REQUIRE_TRUE( campaign.initialize( SRPGTestUtil::makeMapSettings( 4, 3, 2 ), 5 ) );
    (void)campaign.addRosterEntry( _catalog, "gm", "rookie", 2 );
    sw::SRPGCampaign twin;
    SW_REQUIRE_TRUE( twin.initialize( SRPGTestUtil::makeMapSettings( 4, 3, 2 ), 5 ) );

    sw::vector<int32> listChoice;
    campaign.collectChoices( listChoice );
    SW_REQUIRE_TRUE( campaign.beginMission( listChoice[0] ) );
    SW_REQUIRE_TRUE( twin.beginMission( listChoice[0] ) );
    SW_EXPECT_EQUAL( campaign.getMissionSeed(), twin.getMissionSeed() );

    sw::Archive written;
    campaign.writeState( written );
    sw::SRPGCampaign restored;
    sw::Archive      reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( restored.isInMission() );
    SW_EXPECT_EQUAL( campaign.getMissionSeed(), restored.getMissionSeed() );
    SW_REQUIRE_EQUAL( size_t( 1 ), restored.getRoster().size() );
    SW_EXPECT_EQUAL( 2, restored.getRoster()[0]._pilotLevel.getLevel() );

    const auto readForged = [&restored, &campaign]( uint8 bInMission, uint8 bFailed )
    {
        sw::Archive forged;
        campaign.getRunMap().writeState( forged );
        forged << static_cast<uint32>( 0 );
        forged << static_cast<uint32>( 5 );
        forged << bInMission;
        forged << bFailed;
        sw::Archive forgedReading( forged.getData(), forged.getSize() );
        return restored.readState( forgedReading );
    };
    SW_EXPECT_FALSE( readForged( 2, 0 ) );
    SW_EXPECT_FALSE( readForged( 0, 2 ) );
    SW_EXPECT_EQUAL( size_t( 1 ), restored.getRoster().size() );
    SW_EXPECT_TRUE( readForged( 0, 1 ) ); // 형식은 맞다
    SW_EXPECT_TRUE( restored.isFailed() );
}

/**
 * @brief [SRPGCommanderAITest] 계획: 차례가 아니면 없고, 닿으면 제자리에서 치며(덜 걷는 셀), 격파 · 지휘관 · 반격 위험이 표적을 가른다
 */
SW_TEST_CASE_F( SRPGRulesTest, CommanderPlansTargets )
{
    open();
    const int32 unitIndex = place( "striker", sw::SRPGTeam::Player, sw::int2{ 3, 3 } );
    const int32 plainFoe  = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 4, 3 } );
    const int32 killable  = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 2, 3 } );
    _field.beginBattle();
    sw::SRPGCommanderAI     ai;
    sw::SRPGCommanderAIPlan plan;
    SW_EXPECT_FALSE( ai.makePlan( _field, plainFoe, plan ) ); // 적 차례가 아니다
    SW_EXPECT_FALSE( ai.makePlan( _field, 99, plan ) );

    SW_REQUIRE_TRUE( ai.makePlan( _field, unitIndex, plan ) );
    SW_REQUIRE_TRUE( plan._attack.has_value() );
    SW_EXPECT_TRUE( plan._moveCell == ( sw::int2{ 3, 3 } ) );
    SW_EXPECT_EQUAL( plainFoe, plan._attack.value()._target ); // 점수가 같으면 앞 번호
    SW_EXPECT_EQUAL( 1500, plan._score );

    unit( killable )._hp = 1400; // 1400 + 격파 1000 > 1500
    SW_REQUIRE_TRUE( ai.makePlan( _field, unitIndex, plan ) );
    SW_EXPECT_EQUAL( killable, plan._attack.value()._target );
    SW_EXPECT_EQUAL( 2400, plan._score );

    unit( killable )._hp = 3000;
    _field.setCommander( killable, true ); // 1500 + 500
    SW_REQUIRE_TRUE( ai.makePlan( _field, unitIndex, plan ) );
    SW_EXPECT_EQUAL( killable, plan._attack.value()._target );
    SW_EXPECT_EQUAL( 2000, plan._score );
    _field.setCommander( killable, false );

    sw::SRPGBattlefield risky;
    risky.initialize( &_catalog, 8, 8, "plain", _settings, 7 );
    const int32 hunter = risky.addUnit( "striker", "rookie", sw::SRPGTeam::Player, sw::int2{ 3, 3 } );
    (void)risky.addUnit( "weak", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 4, 3 } ); // 반격한다
    const int32 harmless = risky.addUnit( "dummy", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 2, 3 } );
    risky.beginBattle();
    SW_REQUIRE_TRUE( ai.makePlan( risky, hunter, plan ) );
    SW_EXPECT_EQUAL( harmless, plan._attack.value()._target );
}

/**
 * @brief [SRPGCommanderAITest] 멀면 닿는 만큼만 걸어 공격하고, 닿지 못하면 가장 가까운 적 쪽으로 간다. 적이 없으면 제자리다. 이미 공격했으면 끝낸다
 */
SW_TEST_CASE_F( SRPGRulesTest, CommanderMovesTowardEnemy )
{
    open();
    const int32 unitIndex = place( "runner", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 foe       = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 5, 0 } );
    _field.beginBattle();
    sw::SRPGCommanderAI     ai;
    sw::SRPGCommanderAIPlan plan;
    SW_REQUIRE_TRUE( ai.makePlan( _field, unitIndex, plan ) );
    SW_REQUIRE_TRUE( plan._attack.has_value() );
    SW_EXPECT_TRUE( plan._moveCell == ( sw::int2{ 2, 0 } ) ); // 사거리 3 까지만 걷는다

    sw::SRPGCombatResult result;
    SW_REQUIRE_TRUE( ai.runUnit( _field, unitIndex, &result ) );
    SW_EXPECT_EQUAL( size_t( 1 ), result._listStrike.size() );
    SW_EXPECT_TRUE( unit( unitIndex )._cell == ( sw::int2{ 2, 0 } ) );
    SW_EXPECT_EQUAL( 1500, unit( foe )._hp );
    SW_EXPECT_FALSE( _field.canAct( unitIndex ) ); // 행동이 끝났다

    sw::SRPGBattlefield distant;
    distant.initialize( &_catalog, 8, 8, "plain", _settings, 7 );
    const int32 walker = distant.addUnit( "runner", "rookie", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    (void)distant.addUnit( "dummy", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 7, 0 } );
    distant.beginBattle();
    SW_REQUIRE_TRUE( ai.makePlan( distant, walker, plan ) );
    SW_EXPECT_FALSE( plan._attack.has_value() );
    SW_EXPECT_TRUE( plan._moveCell == ( sw::int2{ 3, 0 } ) );

    distant.findUnit( walker )->_bAttacked = SW_TRUE;
    SW_REQUIRE_TRUE( ai.makePlan( distant, walker, plan ) );
    SW_EXPECT_FALSE( plan._attack.has_value() );
    SW_EXPECT_TRUE( plan._moveCell == ( sw::int2{ 0, 0 } ) );

    sw::SRPGBattlefield alone;
    alone.initialize( &_catalog, 8, 8, "plain", _settings, 7 );
    const int32 lonely = alone.addUnit( "runner", "rookie", sw::SRPGTeam::Player, sw::int2{ 4, 4 } );
    alone.beginBattle();
    SW_REQUIRE_TRUE( ai.makePlan( alone, lonely, plan ) );
    SW_EXPECT_TRUE( plan._moveCell == ( sw::int2{ 4, 4 } ) );
    SW_EXPECT_FALSE( plan._attack.has_value() );
}

/**
 * @brief [SRPGCommanderAITest] 결함: 이미 움직인 유닛은 계획이 제자리인데 `moveUnit` 의 거절을 공격 포기로 읽어, 닿는 적을 두고 그냥 끝냈다
 */
SW_TEST_CASE_F( SRPGRulesTest, CommanderAttacksAfterAlreadyMoving )
{
    open();
    const int32 unitIndex = place( "runner", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 foe       = place( "dummy", sw::SRPGTeam::Enemy, sw::int2{ 4, 0 } );
    _field.beginBattle();
    SW_REQUIRE_TRUE( _field.moveUnit( unitIndex, sw::int2{ 2, 0 } ) ); // 사람이 먼저 옮겼다

    sw::SRPGCommanderAI  ai;
    sw::SRPGCombatResult result;
    SW_REQUIRE_TRUE( ai.runUnit( _field, unitIndex, &result ) );
    SW_EXPECT_EQUAL( size_t( 1 ), result._listStrike.size() );
    SW_EXPECT_EQUAL( 1500, unit( foe )._hp );
    SW_EXPECT_TRUE( unit( unitIndex )._cell == ( sw::int2{ 2, 0 } ) );
}

/**
 * @brief [SRPGCommanderAITest] 페이즈 전체: 그 팀의 차례일 때만 움직이고 모두 움직이면 다음 페이즈로 넘어간다. 개별 행동 순에서는 지금 유닛부터 움직인다
 */
SW_TEST_CASE_F( SRPGRulesTest, CommanderRunsPhase )
{
    open();
    (void)place( "dummy", sw::SRPGTeam::Player, sw::int2{ 0, 0 } );
    const int32 first  = place( "runner", sw::SRPGTeam::Enemy, sw::int2{ 7, 7 } );
    const int32 second = place( "runner", sw::SRPGTeam::Enemy, sw::int2{ 6, 7 } );
    _field.beginBattle();
    sw::SRPGCommanderAI ai;
    SW_EXPECT_EQUAL( 0, ai.runPhase( _field, sw::SRPGTeam::Enemy ) ); // 아군 차례다
    SW_EXPECT_EQUAL( sw::SRPGTeam::Player, _field.getPhaseTeam() );

    _field.endPhase();
    SW_REQUIRE_EQUAL( sw::SRPGTeam::Enemy, _field.getPhaseTeam() );
    SW_EXPECT_EQUAL( 2, ai.runPhase( _field, sw::SRPGTeam::Enemy ) );
    SW_EXPECT_EQUAL( sw::SRPGTeam::Player, _field.getPhaseTeam() );
    SW_EXPECT_EQUAL( 2, _field.getTurn() );
    SW_EXPECT_TRUE( unit( first )._cell != ( sw::int2{ 7, 7 } ) );
    SW_EXPECT_TRUE( unit( second )._cell != ( sw::int2{ 6, 7 } ) );

    _settings._turnMode = sw::SRPGTurnMode::Individual;
    sw::SRPGBattlefield individual;
    individual.initialize( &_catalog, 8, 8, "plain", _settings, 7 );
    const int32 solo = individual.addUnit( "runner", "rookie", sw::SRPGTeam::Enemy, sw::int2{ 0, 0 } );
    (void)individual.addUnit( "dummy", "rookie", sw::SRPGTeam::Player, sw::int2{ 7, 7 } );
    individual.beginBattle();
    while ( individual.getActiveUnit() != solo )
    {
        individual.endUnitAction( individual.getActiveUnit() );
    }
    SW_EXPECT_EQUAL( 1, ai.runPhase( individual, sw::SRPGTeam::Enemy ) );
    SW_EXPECT_TRUE( individual.findUnit( solo )->_cell != ( sw::int2{ 0, 0 } ) );
}

/**
 * @brief [SRPGCatalogTest] 적성과 파일럿 능력치: 글자 · 숫자 · 깨진 값, 레벨 성장, 범위 밖 능력치
 */
SW_TEST_CASE( SRPGCatalogTest, AptitudeAndStats )
{
    int32 aptitude = -7;
    SW_REQUIRE_TRUE( sw::SRPGCatalog::parseAptitude( "S", aptitude ) );
    SW_EXPECT_EQUAL( 120, aptitude );
    SW_REQUIRE_TRUE( sw::SRPGCatalog::parseAptitude( "-", aptitude ) );
    SW_EXPECT_EQUAL( 0, aptitude );
    SW_REQUIRE_TRUE( sw::SRPGCatalog::parseAptitude( "75", aptitude ) );
    SW_EXPECT_EQUAL( 75, aptitude );
    SW_EXPECT_FALSE( sw::SRPGCatalog::parseAptitude( "Z", aptitude ) );
    SW_EXPECT_FALSE( sw::SRPGCatalog::parseAptitude( "-5", aptitude ) );
    SW_EXPECT_EQUAL( 75, aptitude ); // 실패는 바꾸지 않는다

    sw::SRPGPilotDef pilot;
    pilot._arrStat[0]   = 20;
    pilot._arrGrowth[0] = 3;
    SW_EXPECT_EQUAL( 20, pilot.computeStat( sw::SRPGPilotStat::Shooting, 1 ) );
    SW_EXPECT_EQUAL( 29, pilot.computeStat( sw::SRPGPilotStat::Shooting, 4 ) );
    SW_EXPECT_EQUAL( 20, pilot.computeStat( sw::SRPGPilotStat::Shooting, -5 ) ); // 레벨이 낮아도 깎이지 않는다
    SW_EXPECT_EQUAL( 0, pilot.computeStat( sw::SRPGPilotStat::Count, 5 ) );
}

/**
 * @brief [SRPGCatalogTest] 깨진 원소는 넣지 않고 알린다: 값이 모자란 비용 · 적성, MAP 패턴 없음 · 깨짐, MAP 아닌 무기의 패턴, 깨진 개발 레벨, 모르는 무기 · 개발 대상 · 원소. MAP 병기는 반격하지 않는다
 */
SW_TEST_CASE( SRPGCatalogTest, ReportsBrokenElements )
{
    SW_EXPECT_LOG( "SRPGCatalog.NeedsFourValuesGround" );
    SW_EXPECT_LOG( "SRPGCatalog.MAPWeaponNeedsPattern" );
    SW_EXPECT_LOG( "SRPGCatalog.MAPWeaponBadPattern" );
    SW_EXPECT_LOG( "SRPGCatalog.WeaponPatternButNo" );
    SW_EXPECT_LOG( "SRPGCatalog.UnitBadDevelopLevel" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    constexpr const utf8* kBrokenXML = R"(<SRPGCatalog>
  <Terrain id="short" name="Short" cost="1 1 1"/>
  <Terrain id="long" name="Long" cost="1 1 1 1 1"/>
  <Terrain id="fine" name="Fine" cost="1 - 2 0"/>
  <Weapon id="nopattern" name="A" map="Self"/>
  <Weapon id="badpattern" name="B" map="Target" pattern="x"/>
  <Weapon id="stray" name="C" pattern="1,0"/>
  <Weapon id="good" name="D" map="Self" pattern="1,0" counter="true"/>
  <Unit id="badaptitude" name="E" aptitude="A B"/>
  <Unit id="baddevelop" name="F" developsTo="good:0"/>
  <Unit id="dangling" name="G" weapons="ghost" developsTo="phantom:2"/>
  <Mystery/>
</SRPGCatalog>)";
    sw::SRPGCatalog       catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( kBrokenXML, "SRPGTest" ) ); // 깨진 참조(ghost · phantom)로 로드는 실패다 — 읽은 항목의 형식 판정은 그대로 본다
    SW_EXPECT_TRUE( catalog.findTerrain( "short" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findTerrain( "long" ) == nullptr );
    SW_REQUIRE_TRUE( catalog.findTerrain( "fine" ) != nullptr );
    SW_EXPECT_EQUAL( -1, catalog.findTerrain( "fine" )->_arrMoveCost[1] );
    SW_EXPECT_EQUAL( 0, catalog.findTerrain( "fine" )->_arrMoveCost[3] );
    SW_EXPECT_TRUE( catalog.findWeapon( "nopattern" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findWeapon( "badpattern" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findWeapon( "stray" ) == nullptr );
    SW_REQUIRE_TRUE( catalog.findWeapon( "good" ) != nullptr );
    SW_EXPECT_FALSE( catalog.findWeapon( "good" )->_bCounter ); // 적어도 MAP 병기는 반격하지 않는다
    SW_EXPECT_TRUE( catalog.findUnit( "badaptitude" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findUnit( "baddevelop" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findUnit( "dangling" ) != nullptr );
}
