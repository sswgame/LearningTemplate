#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Navigation/GridReachability.h"
#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgAiController.h"
#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgBattlefield.h"
#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgCatalog.h"
#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgCombat.h"
#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgProgress.h"

#include "TestFramework/TestFramework.h"

// 택틱스 SRPG 키트 — 이동 범위(지형 · 이동 타입 · 아군 통과 · ZOC), 위협 칸 · MAP 병기 범위, 예측 공식과 결정적 전투 · 이동 회피,
// 반격 · 지원 공격 · 지원 방어 · 동기 공격, EN · 탄 · 기력 조건, 페이즈 · 개별 행동 순과 승패, 점수 AI, 레벨업 · 개발 · 로그라이트 작전 지도.

using namespace sw;

namespace
{
    constexpr const utf8* kSrpgTestXml = R"(
<SrpgCatalog>
  <PilotCurve base="100" exponent="1" linear="0" maxLevel="20"/>
  <UnitCurve base="50" exponent="1" linear="0" maxLevel="10"/>
  <Terrain id="plain" cost="1 1 - 2" domain="Ground"/>
  <Terrain id="forest" cost="2 1 - 3" defense="20" evasion="15" domain="Ground"/>
  <Terrain id="mountain" cost="- 1 - -" defense="30" evasion="20" domain="Ground"/>
  <Terrain id="sea" cost="- 1 - 1" domain="Water"/>
  <Weapon id="vulcan" kind="Shooting" minRange="1" maxRange="2" power="600" hit="10" ammo="2"/>
  <Weapon id="rifle" kind="Shooting" minRange="2" maxRange="4" power="1500" en="10"/>
  <Weapon id="saber" kind="Melee" minRange="1" maxRange="1" power="1800" hit="5" crit="10" en="5"/>
  <Weapon id="cannon" minRange="3" maxRange="6" power="2200" en="20" postMove="false" counter="false"/>
  <Weapon id="funnel" kind="Awaken" minRange="2" maxRange="5" power="1600" en="15" morale="120"/>
  <Weapon id="megabeam" map="Self" pattern="1,0 2,0 3,0 4,0" power="2500" en="50" maxRange="4"/>
  <Weapon id="bomb" map="Target" pattern="0,0 1,0 -1,0 0,1 0,-1" minRange="2" maxRange="5" power="1200" ammo="1"/>
  <Unit id="gm" hp="3000" en="100" move="4" moveType="Ground" size="M" armor="600" mobility="5" aptitude="A B - C" weapons="vulcan,rifle,saber" developsTo="gmcustom:3 gundam:5"/>
  <Unit id="gmcustom" hp="3600" en="120" move="5" armor="700" mobility="10" weapons="vulcan,rifle,saber"/>
  <Unit id="gundam" hp="4000" en="150" move="5" armor="800" mobility="15" weapons="rifle,saber,funnel"/>
  <Unit id="core" hp="1500" en="80" move="6" moveType="Air" size="S" armor="300" mobility="20" aptitude="B A - -" weapons="vulcan"/>
  <Unit id="zaku" hp="2800" en="80" move="4" size="M" armor="500" mobility="5" aptitude="A - - -" weapons="vulcan,rifle"/>
  <Unit id="tank" hp="2000" en="60" move="3" size="L" armor="400" mobility="0" weapons="cannon"/>
  <Unit id="whitebase" hp="9000" en="200" move="3" moveType="Air" size="LL" armor="900" weapons="megabeam,bomb,vulcan"/>
  <Unit id="gogg" hp="2500" en="60" move="4" moveType="Water" aptitude="B - - A" weapons="vulcan"/>
  <Pilot id="ace" shooting="20" melee="18" reaction="20" awaken="10" defense="10" growShooting="2" growMelee="2" growReaction="1" growAwaken="1" growDefense="1"/>
  <Pilot id="grunt" shooting="10" melee="10" reaction="10" awaken="0" defense="5"/>
</SrpgCatalog>
)";

    struct SrpgTestScene
    {
        SrpgCatalog       _catalog;
        SrpgBattlefield   _field;
        vector<SrpgEvent> _listEvent;

        bool initialize( int32 width, int32 height, const SrpgSettings& settings = SrpgSettings{}, uint32 seed = 7 )
        {
            if ( _catalog.loadFromXmlText( kSrpgTestXml, "TacticsSrpgTest" ) == false )
                return false;
            _field.initialize( &_catalog, width, height, hashed_string( "plain" ), settings, seed );
            return true;
        }

        int32 add( const utf8* pUnit, const utf8* pPilot, SrpgTeam team, int32 cellX, int32 cellY )
        {
            return _field.addUnit( hashed_string( pUnit ), hashed_string( pPilot ), team, int2{ cellX, cellY } );
        }

        int32 countEvents( SrpgEvent::Kind kind )
        {
            _field.drainEvents( _listEvent );
            int32 count = 0;
            for ( const SrpgEvent& event : _listEvent )
                count += event._kind == kind ? 1 : 0;
            return count;
        }

        static bool containsCell( const vector<int2>& listCell, const int2& cell )
        {
            for ( const int2& entry : listCell )
            {
                if ( entry == cell )
                    return true;
            }
            return false;
        }
    };

    SrpgSettings makeNoSupportSettings()
    {
        SrpgSettings settings;
        settings._bSupportAttack  = SW_FALSE;
        settings._bSupportDefense = SW_FALSE;
        return settings;
    }
} // namespace

SW_TEST_CASE( TacticsSrpgTest, MoveRangeFollowsTerrainMoveTypeAlliesAndZoneOfControl )
{
    SrpgTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 8, 3 ) );
    SW_EXPECT_EQUAL( 3, scene._field.fillTerrain( int2{ 2, 0 }, int2{ 2, 2 }, hashed_string( "forest" ) ) );
    const int32 gm   = scene.add( "gm", "grunt", SrpgTeam::Player, 0, 1 );
    const int32 core = scene.add( "core", "grunt", SrpgTeam::Player, 1, 1 );
    SW_ASSERT_TRUE( gm >= 0 && core >= 0 );
    SW_EXPECT_EQUAL( -1, scene.add( "zaku", "grunt", SrpgTeam::Enemy, 1, 1 ) ); // 이미 선 칸

    GridReachability reach;
    scene._field.computeMoveRange( gm, reach );
    SW_EXPECT_EQUAL( 1, reach.getCost( int2{ 1, 1 } ) );
    SW_EXPECT_FALSE( reach.isReachable( int2{ 1, 1 } ) ); // 아군 칸은 지나가기만
    SW_EXPECT_EQUAL( 3, reach.getCost( int2{ 2, 1 } ) );  // 숲은 지상 2
    SW_EXPECT_TRUE( reach.isReachable( int2{ 2, 1 } ) );
    SW_EXPECT_EQUAL( 4, reach.getCost( int2{ 3, 1 } ) );
    SW_EXPECT_FALSE( reach.isReachable( int2{ 4, 1 } ) ); // 평지면 4 칸이지만 숲이 하나 더 먹었다

    scene._field.computeMoveRange( core, reach ); // 공중은 숲도 1
    SW_EXPECT_EQUAL( 5, reach.getCost( int2{ 6, 1 } ) );
    SW_EXPECT_TRUE( reach.isReachable( int2{ 7, 1 } ) );

    // 이동 타입 — 지상은 바다에 못 서고, 수중은 바다가 1 · 땅이 2
    SrpgTestScene water;
    SW_ASSERT_TRUE( water.initialize( 6, 1 ) );
    SW_EXPECT_EQUAL( 2, water._field.fillTerrain( int2{ 0, 0 }, int2{ 1, 0 }, hashed_string( "sea" ) ) );
    SW_EXPECT_EQUAL( -1, water.add( "gm", "grunt", SrpgTeam::Player, 0, 0 ) );
    const int32 gogg = water.add( "gogg", "grunt", SrpgTeam::Enemy, 0, 0 );
    SW_ASSERT_TRUE( gogg >= 0 );
    water._field.computeMoveRange( gogg, reach );
    SW_EXPECT_EQUAL( 1, reach.getCost( int2{ 1, 0 } ) );
    SW_EXPECT_EQUAL( 3, reach.getCost( int2{ 2, 0 } ) );
    SW_EXPECT_FALSE( reach.isReachable( int2{ 3, 0 } ) );

    // 적은 막고, ZOC 를 켜면 적과 이웃한 칸에서 멈춘다
    SrpgSettings zocOff;
    SrpgSettings zocOn;
    zocOn._bZoneOfControl = SW_TRUE;
    for ( int32 pass = 0; pass < 2; ++pass )
    {
        SrpgTestScene zone;
        SW_ASSERT_TRUE( zone.initialize( 7, 3, pass == 0 ? zocOff : zocOn ) );
        const int32 mover = zone.add( "gm", "grunt", SrpgTeam::Player, 0, 1 );
        SW_ASSERT_TRUE( zone.add( "zaku", "grunt", SrpgTeam::Enemy, 2, 1 ) >= 0 );
        zone._field.computeMoveRange( mover, reach );
        SW_EXPECT_FALSE( reach.isReachable( int2{ 2, 1 } ) );                 // 적 칸
        SW_EXPECT_TRUE( reach.isReachable( int2{ 2, 0 } ) );                  // 적 옆까지는 들어간다
        SW_EXPECT_TRUE( reach.isReachable( int2{ 3, 0 } ) == ( pass == 0 ) ); // ZOC 면 적 옆을 지나 더 가지 못한다
    }
}

SW_TEST_CASE( TacticsSrpgTest, ThreatCellsAndMapWeaponPatterns )
{
    SrpgTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 13, 13 ) );
    const int32  gm = scene.add( "gm", "grunt", SrpgTeam::Player, 6, 6 );
    vector<int2> listCell;
    scene._field.collectThreatCells( gm, listCell );
    SW_EXPECT_TRUE( SrpgTestScene::containsCell( listCell, int2{ 2, 2 } ) );  // 이동 4 + 라이플 4
    SW_EXPECT_FALSE( SrpgTestScene::containsCell( listCell, int2{ 1, 2 } ) ); // 9 칸은 닿지 않는다

    SrpgTestScene artillery;
    SW_ASSERT_TRUE( artillery.initialize( 13, 13 ) );
    const int32 tank = artillery.add( "tank", "grunt", SrpgTeam::Player, 6, 6 );
    artillery._field.beginBattle();
    artillery._field.collectThreatCells( tank, listCell );
    SW_EXPECT_TRUE( SrpgTestScene::containsCell( listCell, int2{ 6, 0 } ) );
    SW_EXPECT_FALSE( SrpgTestScene::containsCell( listCell, int2{ 0, 5 } ) ); // 이동 후 못 쓰는 포는 지금 칸에서만
    SW_EXPECT_FALSE( SrpgTestScene::containsCell( listCell, int2{ 6, 7 } ) ); // 최소 사거리 3
    SW_ASSERT_TRUE( artillery._field.moveUnit( tank, int2{ 6, 8 } ) );
    const SrpgUnit& tankUnit = *artillery._field.findUnit( tank );
    SW_EXPECT_TRUE( artillery._field.computeWeaponStatus( tankUnit, 0, true ) == SrpgWeaponStatus::NotAfterMove );
    SW_EXPECT_TRUE( artillery._field.computeWeaponStatus( tankUnit, 0, false ) == SrpgWeaponStatus::Ok );

    // MAP 병기 — 자기 기준은 겨눈 쪽으로 돌고, 표적 기준은 사거리 안의 칸 둘레
    SrpgTestScene map;
    SW_ASSERT_TRUE( map.initialize( 11, 11 ) );
    const int32 ship   = map.add( "whitebase", "ace", SrpgTeam::Player, 5, 5 );
    const int32 ally   = map.add( "gm", "grunt", SrpgTeam::Player, 5, 4 );
    const int32 enemyA = map.add( "zaku", "grunt", SrpgTeam::Enemy, 5, 3 );
    const int32 enemyB = map.add( "zaku", "grunt", SrpgTeam::Enemy, 5, 2 );
    SW_ASSERT_TRUE( ship >= 0 && ally >= 0 && enemyA >= 0 && enemyB >= 0 );
    SW_ASSERT_TRUE( map._field.collectMapCells( ship, 0, int2{ 5, 1 }, listCell ) );
    SW_ASSERT_TRUE( listCell.size() == 4 );
    SW_EXPECT_TRUE( listCell[0] == ( int2{ 5, 4 } ) && listCell[3] == ( int2{ 5, 1 } ) );
    SW_ASSERT_TRUE( map._field.collectMapCells( ship, 0, int2{ 9, 6 }, listCell ) ); // 주로 동쪽
    SW_EXPECT_TRUE( listCell[0] == ( int2{ 6, 5 } ) && listCell[3] == ( int2{ 9, 5 } ) );
    SW_EXPECT_FALSE( map._field.collectMapCells( ship, 0, int2{ 5, 5 }, listCell ) );
    SW_ASSERT_TRUE( map._field.collectMapCells( ship, 1, int2{ 5, 8 }, listCell ) );
    SW_EXPECT_EQUAL( 5, static_cast<int32>( listCell.size() ) );
    SW_EXPECT_TRUE( SrpgTestScene::containsCell( listCell, int2{ 5, 9 } ) && SrpgTestScene::containsCell( listCell, int2{ 4, 8 } ) );
    SW_EXPECT_FALSE( map._field.collectMapCells( ship, 1, int2{ 5, 6 }, listCell ) ); // 최소 사거리 2

    map._field.beginBattle();
    SrpgCombatResult result;
    SW_EXPECT_TRUE( SrpgCombat::executeMapAttack( map._field, ship, 0, int2{ 5, 9 }, result ) == SrpgWeaponStatus::InvalidTarget );
    SW_EXPECT_EQUAL( 200, map._field.findUnit( ship )->_en ); // 맞을 것이 없으면 쏘지 않는다
    SW_EXPECT_TRUE( SrpgCombat::executeMapAttack( map._field, ship, 0, int2{ 5, 1 }, result ) == SrpgWeaponStatus::Ok );
    SW_ASSERT_TRUE( result._listStrike.size() == 2 ); // 아군은 빼고 적 둘
    SW_EXPECT_EQUAL( enemyA, result._listStrike[0]._preview._defender );
    SW_EXPECT_EQUAL( enemyB, result._listStrike[1]._preview._defender );
    SW_EXPECT_EQUAL( 3000, map._field.findUnit( ally )->_hp );
    SW_EXPECT_EQUAL( 150, map._field.findUnit( ship )->_en );
    SW_EXPECT_TRUE( SrpgCombat::executeMapAttack( map._field, ship, 0, int2{ 5, 1 }, result ) == SrpgWeaponStatus::CannotAct );
}

SW_TEST_CASE( TacticsSrpgTest, ForecastMatchesFormulaAndBattleIsDeterministic )
{
    SrpgTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 10, 5, makeNoSupportSettings() ) );
    SW_ASSERT_TRUE( scene._field.setTerrain( int2{ 4, 2 }, hashed_string( "forest" ) ) );
    const int32 gm   = scene.add( "gm", "ace", SrpgTeam::Player, 2, 2 );
    const int32 zaku = scene.add( "zaku", "grunt", SrpgTeam::Enemy, 4, 2 );
    const int32 tank = scene.add( "tank", "grunt", SrpgTeam::Enemy, 2, 4 );
    SW_ASSERT_TRUE( gm >= 0 && zaku >= 0 && tank >= 0 );

    SrpgForecast forecast;
    SW_ASSERT_TRUE( SrpgCombat::computeForecast( scene._field, gm, 1, zaku, forecast ) == SrpgWeaponStatus::Ok );
    // 공격력 1500 × 120 / 100 = 1800, 방어 ( 500 + 5 ) × 120 / 100 = 606 → 1194
    SW_EXPECT_EQUAL( 1194, forecast._attack._damage );
    SW_EXPECT_EQUAL( 1791, forecast._attack._critDamage );
    // 70 + ( 20 − 10 ) − 숲 15 − 운동성 5 = 60, 크리티컬 5 + ( 10 − 0 ) / 2 = 10
    SW_EXPECT_EQUAL( 60, forecast._attack._hit );
    SW_EXPECT_EQUAL( 10, forecast._attack._crit );
    // 반격 — 자쿠는 기대 피해가 큰 라이플로: 1650 − 610 = 1040, 70 + ( 10 − 20 ) − 5 = 55
    SW_ASSERT_TRUE( forecast._counter.isValid() );
    SW_EXPECT_EQUAL( 1, forecast._counter._weapon );
    SW_EXPECT_EQUAL( 1040, forecast._counter._damage );
    SW_EXPECT_EQUAL( 55, forecast._counter._hit );

    SW_ASSERT_TRUE( SrpgCombat::computeForecast( scene._field, gm, 1, tank, forecast ) == SrpgWeaponStatus::Ok );
    SW_EXPECT_EQUAL( 85, forecast._attack._hit );   // 큰 표적 + 5
    SW_EXPECT_FALSE( forecast._counter.isValid() ); // 포는 반격하지 않는다
    SW_EXPECT_TRUE( SrpgCombat::computeForecast( scene._field, gm, 2, zaku, forecast ) == SrpgWeaponStatus::OutOfRange );
    SW_EXPECT_TRUE( SrpgCombat::computeForecast( scene._field, gm, 1, gm, forecast ) == SrpgWeaponStatus::InvalidTarget );

    SrpgSettings sureHit = makeNoSupportSettings();
    sureHit._baseHit     = 300;
    SrpgTestScene clamp;
    SW_ASSERT_TRUE( clamp.initialize( 10, 5, sureHit ) );
    const int32 clampGm   = clamp.add( "gm", "ace", SrpgTeam::Player, 2, 2 );
    const int32 clampZaku = clamp.add( "zaku", "grunt", SrpgTeam::Enemy, 4, 2 );
    SW_ASSERT_TRUE( SrpgCombat::computeForecast( clamp._field, clampGm, 1, clampZaku, forecast ) == SrpgWeaponStatus::Ok );
    SW_EXPECT_EQUAL( 100, forecast._attack._hit ); // 0..100 으로 자른다

    // 같은 씨앗이면 같은 결과
    SrpgTestScene twin;
    SW_ASSERT_TRUE( twin.initialize( 10, 5, makeNoSupportSettings() ) );
    SW_ASSERT_TRUE( twin._field.setTerrain( int2{ 4, 2 }, hashed_string( "forest" ) ) );
    SW_ASSERT_TRUE( twin.add( "gm", "ace", SrpgTeam::Player, 2, 2 ) == gm && twin.add( "zaku", "grunt", SrpgTeam::Enemy, 4, 2 ) == zaku );
    SW_ASSERT_TRUE( twin.add( "tank", "grunt", SrpgTeam::Enemy, 2, 4 ) == tank );
    scene._field.beginBattle();
    twin._field.beginBattle();
    SrpgCombatResult resultA;
    SrpgCombatResult resultB;
    SW_ASSERT_TRUE( SrpgCombat::executeAttack( scene._field, gm, 1, zaku, resultA ) == SrpgWeaponStatus::Ok );
    SW_ASSERT_TRUE( SrpgCombat::executeAttack( twin._field, gm, 1, zaku, resultB ) == SrpgWeaponStatus::Ok );
    SW_ASSERT_TRUE( resultA._listStrike.size() == 2 && resultB._listStrike.size() == 2 ); // 주 공격 + 반격(1791 로도 자쿠는 남는다)
    for ( size_t index = 0; index < resultA._listStrike.size(); ++index )
    {
        SW_EXPECT_TRUE( resultA._listStrike[index]._bHit == resultB._listStrike[index]._bHit );
        SW_EXPECT_EQUAL( resultA._listStrike[index]._damage, resultB._listStrike[index]._damage );
    }
    const SrpgStrikeResult& main = resultA._listStrike[0];
    SW_EXPECT_TRUE( main._role == SrpgStrikeRole::Main && resultA._listStrike[1]._role == SrpgStrikeRole::Counter );
    const int32 expectedDamage = main._bHit == SW_FALSE ? 0 : ( main._bCrit == SW_TRUE ? 1791 : 1194 );
    SW_EXPECT_EQUAL( expectedDamage, main._damage );
    SW_EXPECT_EQUAL( 2800 - main._damage, scene._field.findUnit( zaku )->_hp );
    SW_EXPECT_EQUAL( scene._field.findUnit( gm )->_hp, twin._field.findUnit( gm )->_hp );
    SW_EXPECT_EQUAL( scene._field.findUnit( gm )->_morale, twin._field.findUnit( gm )->_morale );

    // 메탈슬러그 택틱스식 이동 회피 — 3 칸 걸으면 15 가 쌓이고, 끄면 없다
    for ( int32 pass = 0; pass < 2; ++pass )
    {
        SrpgSettings settings = makeNoSupportSettings();
        settings._bMoveDodge  = pass == 0 ? SW_TRUE : SW_FALSE;
        SrpgTestScene dodge;
        SW_ASSERT_TRUE( dodge.initialize( 10, 5, settings ) );
        const int32 runner = dodge.add( "gm", "ace", SrpgTeam::Player, 0, 2 );
        const int32 enemy  = dodge.add( "zaku", "grunt", SrpgTeam::Enemy, 5, 2 );
        dodge._field.beginBattle();
        SW_ASSERT_TRUE( dodge._field.moveUnit( runner, int2{ 3, 2 } ) );
        SW_ASSERT_TRUE( SrpgCombat::computeForecast( dodge._field, enemy, 1, runner, forecast ) == SrpgWeaponStatus::Ok );
        SW_EXPECT_EQUAL( pass == 0 ? 40 : 55, forecast._attack._hit );
    }
}

SW_TEST_CASE( TacticsSrpgTest, CounterSupportAttackSupportDefenseAndSync )
{
    SrpgTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 10, 6 ) );
    const int32 gm    = scene.add( "gm", "ace", SrpgTeam::Player, 2, 2 );
    const int32 buddy = scene.add( "gmcustom", "grunt", SrpgTeam::Player, 2, 1 );
    const int32 zaku  = scene.add( "zaku", "grunt", SrpgTeam::Enemy, 4, 2 );
    const int32 tank  = scene.add( "tank", "grunt", SrpgTeam::Enemy, 4, 3 );
    SW_ASSERT_TRUE( gm >= 0 && buddy >= 0 && zaku >= 0 && tank >= 0 );

    SrpgForecast forecast;
    SW_ASSERT_TRUE( SrpgCombat::computeForecast( scene._field, gm, 1, zaku, forecast ) == SrpgWeaponStatus::Ok );
    SW_EXPECT_EQUAL( tank, forecast._supportDefender ); // 자쿠 옆의 전차가 대신 맞는다
    SW_EXPECT_EQUAL( tank, forecast._attack._defender );
    SW_EXPECT_EQUAL( 697, forecast._attack._damage );    // ( 1800 − 405 ) × 50 / 100
    SW_ASSERT_TRUE( forecast._supportAttack.isValid() ); // 이웃한 짐 커스텀이 라이플로 함께
    SW_EXPECT_EQUAL( buddy, forecast._supportAttack._attacker );
    SW_EXPECT_EQUAL( 1, forecast._supportAttack._weapon );
    SW_EXPECT_EQUAL( zaku, forecast._supportAttack._defender );
    SW_ASSERT_TRUE( forecast._counter.isValid() );
    SW_EXPECT_EQUAL( zaku, forecast._counter._attacker );

    scene._field.beginBattle();
    SrpgCombatResult result;
    SW_ASSERT_TRUE( SrpgCombat::executeAttack( scene._field, gm, 1, zaku, result ) == SrpgWeaponStatus::Ok );
    SW_ASSERT_TRUE( result._listStrike.size() == 3 );
    SW_EXPECT_TRUE( result._listStrike[0]._role == SrpgStrikeRole::Main && result._listStrike[0]._preview._defender == tank );
    SW_EXPECT_TRUE( result._listStrike[1]._role == SrpgStrikeRole::Support && result._listStrike[1]._preview._defender == zaku );
    SW_EXPECT_TRUE( result._listStrike[2]._role == SrpgStrikeRole::Counter && result._listStrike[2]._preview._defender == gm );
    SW_EXPECT_EQUAL( 2000 - result._listStrike[0]._damage, scene._field.findUnit( tank )->_hp );
    SW_EXPECT_EQUAL( 2800 - result._listStrike[1]._damage, scene._field.findUnit( zaku )->_hp );
    SW_EXPECT_EQUAL( 3000 - result._listStrike[2]._damage, scene._field.findUnit( gm )->_hp );
    SW_EXPECT_TRUE( scene._field.findUnit( tank )->_bSupportUsed == SW_TRUE );
    SW_EXPECT_TRUE( scene._field.findUnit( buddy )->_bSupportUsed == SW_TRUE );
    SW_EXPECT_EQUAL( 110, scene._field.findUnit( buddy )->_en );                                                     // 지원 공격도 EN 을 쓴다
    SW_EXPECT_EQUAL( 70, scene._field.findUnit( zaku )->_en );                                                       // 반격도
    SW_EXPECT_TRUE( SrpgCombat::executeAttack( scene._field, gm, 1, zaku, result ) == SrpgWeaponStatus::CannotAct ); // 한 차례에 한 번

    // 이번 차례 지원을 다 쓴 뒤에는 대신 맞지 않는다, 규칙을 끄면 처음부터 없다
    SW_ASSERT_TRUE( SrpgCombat::computeForecast( scene._field, buddy, 1, zaku, forecast ) == SrpgWeaponStatus::Ok );
    SW_EXPECT_EQUAL( -1, forecast._supportDefender );
    SrpgTestScene noSupport;
    SW_ASSERT_TRUE( noSupport.initialize( 10, 6, makeNoSupportSettings() ) );
    (void)noSupport.add( "gm", "ace", SrpgTeam::Player, 2, 2 );
    (void)noSupport.add( "gmcustom", "grunt", SrpgTeam::Player, 2, 1 );
    (void)noSupport.add( "zaku", "grunt", SrpgTeam::Enemy, 4, 2 );
    (void)noSupport.add( "tank", "grunt", SrpgTeam::Enemy, 4, 3 );
    SW_ASSERT_TRUE( SrpgCombat::computeForecast( noSupport._field, gm, 1, zaku, forecast ) == SrpgWeaponStatus::Ok );
    SW_EXPECT_EQUAL( -1, forecast._supportDefender );
    SW_EXPECT_FALSE( forecast._supportAttack.isValid() );
    SW_EXPECT_EQUAL( zaku, forecast._attack._defender );

    // 동기 공격 — 같은 적을 사거리에 둔 아군이 절반 위력으로 함께(이웃이 아니어도)
    for ( int32 pass = 0; pass < 2; ++pass )
    {
        SrpgSettings settings = makeNoSupportSettings();
        settings._bSyncAttack = pass == 0 ? SW_TRUE : SW_FALSE;
        SrpgTestScene sync;
        SW_ASSERT_TRUE( sync.initialize( 10, 6, settings ) );
        const int32 shooter = sync.add( "gm", "ace", SrpgTeam::Player, 2, 2 );
        const int32 partner = sync.add( "gmcustom", "grunt", SrpgTeam::Player, 4, 5 );
        const int32 target  = sync.add( "zaku", "grunt", SrpgTeam::Enemy, 4, 2 );
        SW_ASSERT_TRUE( SrpgCombat::computeForecast( sync._field, shooter, 1, target, forecast ) == SrpgWeaponStatus::Ok );
        SW_ASSERT_TRUE( static_cast<int32>( forecast._listSync.size() ) == ( pass == 0 ? 1 : 0 ) );
        if ( pass == 0 )
        {
            SW_EXPECT_EQUAL( partner, forecast._listSync[0]._attacker );
            SW_EXPECT_EQUAL( 572, forecast._listSync[0]._damage ); // ( 1650 − 505 ) × 50 / 100
            SW_EXPECT_EQUAL( 65, forecast._listSync[0]._hit );
            sync._field.beginBattle();
            SrpgCombatResult syncResult;
            SW_ASSERT_TRUE( SrpgCombat::executeAttack( sync._field, shooter, 1, target, syncResult ) == SrpgWeaponStatus::Ok );
            SW_ASSERT_TRUE( syncResult._listStrike.size() == 3 );
            SW_EXPECT_TRUE( syncResult._listStrike[1]._role == SrpgStrikeRole::Sync );
            SW_EXPECT_EQUAL( 120, sync._field.findUnit( partner )->_en ); // 동기 공격은 EN 을 쓰지 않는다
        }
    }
}

SW_TEST_CASE( TacticsSrpgTest, EnergyAmmoAndMoraleGateWeapons )
{
    SrpgTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 10, 5, makeNoSupportSettings() ) );
    const int32 gm     = scene.add( "gm", "ace", SrpgTeam::Player, 2, 2 );
    const int32 gundam = scene.add( "gundam", "ace", SrpgTeam::Player, 2, 4 );
    const int32 zaku   = scene.add( "zaku", "grunt", SrpgTeam::Enemy, 3, 2 );
    SW_ASSERT_TRUE( gm >= 0 && gundam >= 0 && zaku >= 0 );
    scene._field.beginBattle();

    SrpgUnit& gmUnit = *scene._field.findUnit( gm );
    scene._field.consumeWeapon( gm, 0 );
    SW_EXPECT_EQUAL( 1, gmUnit._listAmmo[0] );
    scene._field.consumeWeapon( gm, 0 );
    SW_EXPECT_TRUE( scene._field.computeWeaponStatus( gmUnit, 0, false ) == SrpgWeaponStatus::NoAmmo );
    SrpgCombatResult result;
    SW_EXPECT_TRUE( SrpgCombat::executeAttack( scene._field, gm, 0, zaku, result ) == SrpgWeaponStatus::NoAmmo );
    SW_EXPECT_EQUAL( 2800, scene._field.findUnit( zaku )->_hp ); // 실패는 아무것도 바꾸지 않는다
    SW_EXPECT_TRUE( gmUnit._bAttacked == SW_FALSE );

    gmUnit._en = 5;
    SW_EXPECT_TRUE( scene._field.computeWeaponStatus( gmUnit, 1, false ) == SrpgWeaponStatus::NotEnoughEnergy );
    SW_EXPECT_TRUE( scene._field.computeWeaponStatus( gmUnit, 2, false ) == SrpgWeaponStatus::Ok );
    SW_ASSERT_TRUE( SrpgCombat::executeAttack( scene._field, gm, 2, zaku, result ) == SrpgWeaponStatus::Ok ); // 빔 사벨 EN 5
    SW_EXPECT_EQUAL( 0, gmUnit._en );

    const SrpgUnit& gundamUnit = *scene._field.findUnit( gundam );
    SW_EXPECT_TRUE( scene._field.computeWeaponStatus( gundamUnit, 2, false ) == SrpgWeaponStatus::LowMorale ); // 핀 판넬은 기력 120
    scene._field.addMorale( gundam, 19 );
    SW_EXPECT_TRUE( scene._field.computeWeaponStatus( gundamUnit, 2, false ) == SrpgWeaponStatus::LowMorale );
    scene._field.addMorale( gundam, 1 );
    SW_EXPECT_TRUE( scene._field.computeWeaponStatus( gundamUnit, 2, false ) == SrpgWeaponStatus::Ok );
    scene._field.addMorale( gundam, 500 );
    SW_EXPECT_EQUAL( 150, gundamUnit._morale ); // 위 끝
    SW_EXPECT_TRUE( scene._field.computeWeaponStatus( gundamUnit, 9, false ) == SrpgWeaponStatus::InvalidWeapon );
}

SW_TEST_CASE( TacticsSrpgTest, PhasesTurnOrderAndMissionOutcomes )
{
    SrpgTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 10, 3 ) );
    const int32 leader = scene.add( "gm", "ace", SrpgTeam::Player, 0, 0 );
    const int32 wing   = scene.add( "gmcustom", "grunt", SrpgTeam::Player, 0, 2 );
    const int32 boss   = scene.add( "zaku", "grunt", SrpgTeam::Enemy, 8, 0 );
    const int32 guard  = scene.add( "tank", "grunt", SrpgTeam::Enemy, 9, 2 );
    const int32 rogue  = scene.add( "core", "grunt", SrpgTeam::Third, 9, 0 );
    scene._field.setCommander( leader, true );
    scene._field.setCommander( boss, true );

    SrpgMissionRule defeatAll;
    SW_EXPECT_TRUE( SrpgMission::evaluate( scene._field, defeatAll ) == SrpgOutcome::Ongoing ); // 시작 전

    scene._field.beginBattle();
    SW_EXPECT_EQUAL( 1, scene._field.getTurn() );
    SW_EXPECT_TRUE( scene._field.getPhaseTeam() == SrpgTeam::Player );
    SW_EXPECT_FALSE( scene._field.canAct( boss ) );
    scene._field.endUnitAction( leader );
    SW_EXPECT_TRUE( scene._field.getPhaseTeam() == SrpgTeam::Player ); // 한 명 남았다
    SW_EXPECT_FALSE( scene._field.canAct( leader ) );
    scene._field.endUnitAction( wing );
    SW_EXPECT_TRUE( scene._field.getPhaseTeam() == SrpgTeam::Enemy );
    SW_EXPECT_TRUE( scene._field.canAct( boss ) && scene._field.canAct( guard ) );
    scene._field.endPhase();
    SW_EXPECT_TRUE( scene._field.getPhaseTeam() == SrpgTeam::Third );
    scene._field.endPhase();
    SW_EXPECT_TRUE( scene._field.getPhaseTeam() == SrpgTeam::Player );
    SW_EXPECT_EQUAL( 2, scene._field.getTurn() );
    SW_EXPECT_EQUAL( 4, scene.countEvents( SrpgEvent::Kind::PhaseStarted ) );
    SW_EXPECT_TRUE( scene._field.canAct( leader ) ); // 새 페이즈는 다시 움직인다

    SrpgMissionRule reach;
    reach._objective = SrpgObjective::ReachCell;
    reach._listGoalCell.push_back( int2{ 3, 0 } );
    SW_EXPECT_TRUE( SrpgMission::evaluate( scene._field, reach ) == SrpgOutcome::Ongoing );
    SW_ASSERT_TRUE( scene._field.moveUnit( leader, int2{ 3, 0 } ) );
    SW_EXPECT_TRUE( SrpgMission::evaluate( scene._field, reach ) == SrpgOutcome::Victory );

    SrpgMissionRule commander;
    commander._objective = SrpgObjective::DefeatCommander;
    SrpgMissionRule turnLimit;
    turnLimit._turnLimit = 1;
    SrpgMissionRule survive;
    survive._objective = SrpgObjective::SurviveTurns;
    survive._turnLimit = 1;
    SW_EXPECT_TRUE( SrpgMission::evaluate( scene._field, turnLimit ) == SrpgOutcome::Defeat ); // 2 턴 > 1
    SW_EXPECT_TRUE( SrpgMission::evaluate( scene._field, survive ) == SrpgOutcome::Victory );
    SW_EXPECT_TRUE( SrpgMission::evaluate( scene._field, commander ) == SrpgOutcome::Ongoing );
    SW_EXPECT_TRUE( scene._field.applyDamage( boss, 99999, leader ) );
    SW_EXPECT_TRUE( SrpgMission::evaluate( scene._field, commander ) == SrpgOutcome::Victory );
    SW_EXPECT_TRUE( SrpgMission::evaluate( scene._field, defeatAll ) == SrpgOutcome::Ongoing ); // 전차가 남았다(제3세력은 세지 않는다)
    SW_EXPECT_TRUE( scene._field.applyDamage( guard, 99999, leader ) );
    SW_EXPECT_TRUE( SrpgMission::evaluate( scene._field, defeatAll ) == SrpgOutcome::Victory );
    SW_EXPECT_TRUE( scene._field.applyDamage( leader, 99999, rogue ) );
    SW_EXPECT_TRUE( SrpgMission::evaluate( scene._field, defeatAll ) == SrpgOutcome::Defeat ); // 아군 지휘관 — 패배가 앞선다

    // 개별 행동 순 — 반응 + 운동성이 빠른 쪽부터, 모두 한 번씩 하면 다음 턴
    SrpgSettings individual;
    individual._turnMode = SrpgTurnMode::Individual;
    SrpgTestScene order;
    SW_ASSERT_TRUE( order.initialize( 10, 3, individual ) );
    const int32 slow = order.add( "zaku", "grunt", SrpgTeam::Enemy, 8, 0 );
    const int32 fast = order.add( "gm", "ace", SrpgTeam::Player, 0, 0 );
    order._field.beginBattle();
    SW_EXPECT_EQUAL( 1, order._field.getTurn() );
    SW_EXPECT_EQUAL( fast, order._field.getActiveUnit() );
    SW_EXPECT_FALSE( order._field.canAct( slow ) );
    order._field.endUnitAction( fast );
    SW_EXPECT_EQUAL( slow, order._field.getActiveUnit() );
    SW_EXPECT_TRUE( order._field.getPhaseTeam() == SrpgTeam::Enemy );
    order._field.endUnitAction( slow );
    SW_EXPECT_EQUAL( 2, order._field.getTurn() );
    SW_EXPECT_EQUAL( fast, order._field.getActiveUnit() );
}

SW_TEST_CASE( TacticsSrpgTest, AiPicksBestScoredTargetDeterministically )
{
    for ( int32 pass = 0; pass < 2; ++pass )
    {
        SrpgTestScene scene;
        SW_ASSERT_TRUE( scene.initialize( 12, 5, makeNoSupportSettings() ) );
        const int32 tank = scene.add( "tank", "grunt", SrpgTeam::Player, 2, 0 );
        const int32 core = scene.add( "core", "grunt", SrpgTeam::Player, 2, 2 );
        const int32 gm   = scene.add( "gm", "ace", SrpgTeam::Player, 2, 4 );
        const int32 zaku = scene.add( "zaku", "grunt", SrpgTeam::Enemy, 8, 2 );
        SW_ASSERT_TRUE( tank >= 0 && core >= 0 && gm >= 0 && zaku >= 0 );
        scene._field.findUnit( core )->_hp = 100; // 한 방이면 격파
        scene._field.beginBattle();
        scene._field.endPhase();
        SW_ASSERT_TRUE( scene._field.getPhaseTeam() == SrpgTeam::Enemy );

        SrpgAiController controller;
        SrpgAiSettings   aiSettings;
        if ( pass == 1 )
            aiSettings._killBonus = 3000; // 격파를 무겁게 보면 표적이 바뀐다
        controller.setSettings( aiSettings );
        SrpgAiPlan plan;
        SW_ASSERT_TRUE( controller.makePlan( scene._field, zaku, plan ) );
        SW_ASSERT_TRUE( plan._bAttack == SW_TRUE );
        const int2 target = scene._field.findUnit( plan._target )->_cell;
        if ( pass == 0 )
        {
            // 전차 라이플: 75 × min( 1650 − 405, 2000 ) / 100 = 933, 반격 없음. 짐(에이스)은 반격 위험이 커서 음수,
            // 코어는 라이플 45 + 450 = 495 · 발칸 55 + 550 − 반격 위험 55 = 550
            SW_EXPECT_EQUAL( tank, plan._target );
            SW_EXPECT_EQUAL( 1, plan._weapon );
            SW_EXPECT_EQUAL( 933, plan._score );
            SW_EXPECT_EQUAL( 4, SrpgBattlefield::computeDistance( plan._moveCell, target ) ); // 사거리 끝 — 덜 걷는 칸
        }
        else
        {
            // 코어 발칸: 55 + 55 × 3000 / 100 − 반격(80 × 155 / 100 = 124, 격파 못 할 몫 45 % → 55) = 1650 — 라이플(1395)보다 낫다
            SW_EXPECT_EQUAL( core, plan._target );
            SW_EXPECT_EQUAL( 0, plan._weapon );
            SW_EXPECT_EQUAL( 1650, plan._score );
            SW_EXPECT_EQUAL( 2, SrpgBattlefield::computeDistance( plan._moveCell, target ) );
        }
        SrpgAiPlan again;
        SW_ASSERT_TRUE( controller.makePlan( scene._field, zaku, again ) );
        SW_EXPECT_TRUE( again._moveCell == plan._moveCell && again._target == plan._target && again._weapon == plan._weapon );

        SW_EXPECT_EQUAL( 1, controller.runPhase( scene._field, SrpgTeam::Enemy ) );
        SW_EXPECT_TRUE( scene._field.findUnit( zaku )->_cell == plan._moveCell );
        SW_EXPECT_TRUE( scene._field.getPhaseTeam() == SrpgTeam::Player );
        SW_EXPECT_EQUAL( 2, scene._field.getTurn() );
        SW_EXPECT_EQUAL( 1, scene.countEvents( SrpgEvent::Kind::Moved ) );
    }

    // 칠 것이 없으면 가장 가까운 적 쪽으로
    SrpgTestScene farScene;
    SW_ASSERT_TRUE( farScene.initialize( 20, 1 ) );
    const int32 hunter = farScene.add( "zaku", "grunt", SrpgTeam::Enemy, 19, 0 );
    (void)farScene.add( "gm", "grunt", SrpgTeam::Player, 0, 0 );
    farScene._field.beginBattle();
    farScene._field.endPhase();
    SrpgAiController controller;
    SrpgAiPlan       plan;
    SW_ASSERT_TRUE( controller.makePlan( farScene._field, hunter, plan ) );
    SW_EXPECT_TRUE( plan._bAttack == SW_FALSE );
    SW_EXPECT_TRUE( plan._moveCell == ( int2{ 15, 0 } ) );
}

SW_TEST_CASE( TacticsSrpgTest, LevelUpDevelopmentAndRogueliteCampaign )
{
    SrpgTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 10, 5 ) );
    const int32 gm = scene.add( "gm", "ace", SrpgTeam::Player, 2, 2 );
    SW_ASSERT_TRUE( gm >= 0 );
    SW_EXPECT_EQUAL( 20, scene._field.findUnit( gm )->computeStat( SrpgPilotStat::Shooting ) );

    scene._field.grantXp( gm, 250 ); // 파일럿 100 · 200 곡선 → 2 레벨 150, 기체 50 · 100 · 150 → 3 레벨 100
    const SrpgUnit& unit = *scene._field.findUnit( gm );
    SW_EXPECT_EQUAL( 2, unit._pilotLevel.getLevel() );
    SW_EXPECT_EQUAL( 150, static_cast<int32>( unit._pilotLevel.getXp() ) );
    SW_EXPECT_EQUAL( 3, unit._unitLevel.getLevel() );
    SW_EXPECT_EQUAL( 22, unit.computeStat( SrpgPilotStat::Shooting ) ); // 성장 2
    SW_EXPECT_EQUAL( 1, scene.countEvents( SrpgEvent::Kind::PilotLevelUp ) );

    vector<hashed_string> listOption;
    scene._field.collectDevelopOptions( gm, listOption );
    SW_ASSERT_TRUE( listOption.size() == 1 );
    SW_EXPECT_TRUE( listOption[0] == hashed_string( "gmcustom" ) ); // 건담은 5 레벨
    SW_EXPECT_FALSE( scene._field.developUnit( gm, hashed_string( "gundam" ) ) );
    scene._field.findUnit( gm )->_hp = 10;
    SW_ASSERT_TRUE( scene._field.developUnit( gm, hashed_string( "gmcustom" ) ) );
    SW_EXPECT_TRUE( unit._pDef->_id == hashed_string( "gmcustom" ) );
    SW_EXPECT_EQUAL( 3600, unit._hp );
    SW_EXPECT_EQUAL( 1, unit._unitLevel.getLevel() );
    SW_EXPECT_EQUAL( 2, unit._pilotLevel.getLevel() ); // 파일럿은 그대로

    // 격파하면 맞힌 것 + 격파 경험치(레벨 차 보정)
    SrpgTestScene battle;
    SW_ASSERT_TRUE( battle.initialize( 10, 5, makeNoSupportSettings() ) );
    const int32 hunter                  = battle.add( "gm", "ace", SrpgTeam::Player, 2, 2 );
    const int32 prey                    = battle.add( "zaku", "grunt", SrpgTeam::Enemy, 4, 2 );
    battle._field.findUnit( prey )->_hp = 1;
    battle._field.beginBattle();
    for ( int32 attempt = 0; attempt < 8 && battle._field.findUnit( prey )->_bAlive == SW_TRUE; ++attempt )
    {
        SrpgCombatResult result;
        battle._field.findUnit( hunter )->_bAttacked = SW_FALSE;
        (void)SrpgCombat::executeAttack( battle._field, hunter, 1, prey, result );
    }
    SW_ASSERT_TRUE( battle._field.findUnit( prey )->_bAlive == SW_FALSE );
    SW_EXPECT_EQUAL( 40, static_cast<int32>( battle._field.findUnit( hunter )->_pilotLevel.getTotalXp() ) ); // 10 + 30, 같은 레벨

    // 로그라이트 — 작전 지도를 고르고, 명단을 내보내고, 레벨 · 개발이 돌아온다
    RunMapSettings mapSettings;
    mapSettings._floorCount  = 3;
    mapSettings._columnCount = 3;
    mapSettings._pathCount   = 2;
    RunNodeRule rule;
    rule._kind = hashed_string( "Battle" );
    mapSettings._listRule.push_back( rule );
    SrpgCampaign campaign;
    campaign.initialize( mapSettings, 99 );
    SW_EXPECT_EQUAL( 0, campaign.addRosterEntry( scene._catalog, hashed_string( "gm" ), hashed_string( "ace" ) ) );
    SW_EXPECT_EQUAL( 1, campaign.addRosterEntry( scene._catalog, hashed_string( "zaku" ), hashed_string( "grunt" ) ) );
    SW_EXPECT_EQUAL( -1, campaign.addRosterEntry( scene._catalog, hashed_string( "nothing" ), hashed_string( "ace" ) ) );
    vector<int32> listChoice;
    campaign.collectChoices( listChoice );
    SW_ASSERT_TRUE( listChoice.empty() == false );
    SW_ASSERT_TRUE( campaign.beginMission( listChoice[0] ) );
    SW_EXPECT_FALSE( campaign.beginMission( listChoice[0] ) ); // 작전 중
    SW_EXPECT_TRUE( campaign.getMissionKind() == hashed_string( "Battle" ) );
    SrpgCampaign twin;
    twin.initialize( mapSettings, 99 );
    SW_ASSERT_TRUE( twin.beginMission( listChoice[0] ) );
    SW_EXPECT_TRUE( twin.getMissionSeed() == campaign.getMissionSeed() ); // 같은 판 · 같은 칸이면 같은 전장

    SrpgBattlefield mission;
    mission.initialize( &scene._catalog, 8, 4, hashed_string( "plain" ), SrpgSettings{}, campaign.getMissionSeed() );
    vector<int2> listDeploy;
    listDeploy.push_back( int2{ 0, 0 } );
    listDeploy.push_back( int2{ 0, 1 } );
    SW_EXPECT_EQUAL( 2, campaign.deployRoster( mission, listDeploy ) );
    mission.grantXp( 0, 250 );
    SW_ASSERT_TRUE( mission.developUnit( 0, hashed_string( "gmcustom" ) ) );
    SW_EXPECT_TRUE( mission.applyDamage( 1, 99999, -1 ) );
    campaign.completeMission( mission, SrpgOutcome::Victory );
    SW_EXPECT_FALSE( campaign.isFailed() );
    SW_EXPECT_TRUE( campaign.getRoster()[0]._unitId == hashed_string( "gmcustom" ) );
    SW_EXPECT_EQUAL( 2, campaign.getRoster()[0]._pilotLevel.getLevel() );
    SW_EXPECT_TRUE( campaign.getRoster()[1]._bLost == SW_TRUE ); // 격파된 유닛은 이번 판에서 빠진다

    campaign.collectChoices( listChoice );
    SW_ASSERT_TRUE( listChoice.empty() == false );
    SW_ASSERT_TRUE( campaign.beginMission( listChoice[0] ) );
    SrpgBattlefield second;
    second.initialize( &scene._catalog, 8, 4, hashed_string( "plain" ), SrpgSettings{}, campaign.getMissionSeed() );
    SW_EXPECT_EQUAL( 1, campaign.deployRoster( second, listDeploy ) );
    SW_EXPECT_EQUAL( 2, second.findUnit( 0 )->_pilotLevel.getLevel() ); // 레벨이 이어진다
    campaign.completeMission( second, SrpgOutcome::Defeat );
    SW_EXPECT_TRUE( campaign.isFailed() );
    campaign.collectChoices( listChoice );
    SW_EXPECT_FALSE( listChoice.empty() == false && campaign.beginMission( listChoice[0] ) ); // 패배하면 판이 끝난다
}

/**
 * @brief [TacticsSrpgTest] 상태 바이트로 되살린 전장이 같은 전투를 잇는다 — 지형 · 유닛(탄 · EN · 기력 · 레벨 · 차례 비트) · 차례 · 난수가 같은 바이트이고,
 *        같은 걸음(적 페이즈의 반격 · 페이즈 넘김)을 둘 다 더 돌려도 같은 바이트다. 크기가 다른 전장과 잘린 바이트는 거절한다
 */
SW_TEST_CASE( TacticsSrpgTest, StateRoundTripContinuesTheSameBattlefield )
{
    SrpgTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 10, 5, makeNoSupportSettings() ) );
    SW_ASSERT_TRUE( scene._field.setTerrain( int2{ 5, 2 }, hashed_string( "forest" ) ) );
    const int32 gm   = scene.add( "gm", "ace", SrpgTeam::Player, 2, 2 );
    const int32 zaku = scene.add( "zaku", "grunt", SrpgTeam::Enemy, 4, 2 );
    const int32 core = scene.add( "core", "grunt", SrpgTeam::Third, 9, 4 );
    SW_ASSERT_TRUE( gm >= 0 && zaku >= 0 && core >= 0 );
    scene._field.beginBattle();
    SrpgCombatResult result;
    SW_EXPECT_TRUE( SrpgCombat::executeAttack( scene._field, gm, 0, zaku, result ) == SrpgWeaponStatus::Ok );
    scene._field.endPhase();
    SW_ASSERT_TRUE( scene._field.getPhaseTeam() == SrpgTeam::Enemy );

    Archive written;
    scene._field.writeState( written );
    SrpgTestScene restored;
    SW_ASSERT_TRUE( restored.initialize( 10, 5, makeNoSupportSettings(), 99 ) );
    Archive reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( restored._field.readState( reader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, reader.getRemainingBytes() );
    Archive rewritten;
    restored._field.writeState( rewritten );
    vector<uint8> originalBytes;
    vector<uint8> restoredBytes;
    written.writeData( originalBytes );
    rewritten.writeData( restoredBytes );
    SW_EXPECT_TRUE( originalBytes == restoredBytes );
    SW_EXPECT_TRUE( restored._field.findTerrainAt( int2{ 5, 2 } )->_id == hashed_string( "forest" ) );
    SW_ASSERT_EQUAL( 3, static_cast<int32>( restored._field.getUnits().size() ) );
    SW_EXPECT_EQUAL( scene._field.findUnit( zaku )->_hp, restored._field.findUnit( zaku )->_hp );
    SW_EXPECT_EQUAL( 1, restored._field.findUnit( gm )->_listAmmo[0] ); // 발칸 탄 하나를 썼다
    SW_EXPECT_TRUE( restored._field.getPhaseTeam() == SrpgTeam::Enemy );
    SW_EXPECT_EQUAL( scene._field.getTurn(), restored._field.getTurn() );

    // 같은 걸음을 둘 다 — 적이 라이플로 쏘고(명중 · 크리티컬 난수) 페이즈가 돌아 다음 턴이 된다.
    for ( SrpgTestScene* pScene : { &scene, &restored } )
    {
        SrpgCombatResult enemyResult;
        SW_EXPECT_TRUE( SrpgCombat::executeAttack( pScene->_field, zaku, 1, gm, enemyResult ) == SrpgWeaponStatus::Ok );
        pScene->_field.endPhase();
        pScene->_field.endPhase();
    }
    SW_EXPECT_EQUAL( 2, restored._field.getTurn() );
    Archive afterOriginal;
    Archive afterRestored;
    scene._field.writeState( afterOriginal );
    restored._field.writeState( afterRestored );
    afterOriginal.writeData( originalBytes );
    afterRestored.writeData( restoredBytes );
    SW_EXPECT_TRUE( originalBytes == restoredBytes );

    SrpgTestScene smaller;
    SW_ASSERT_TRUE( smaller.initialize( 8, 5 ) );
    Archive smallerReader( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( smaller._field.readState( smallerReader ) );
    SrpgTestScene truncated;
    SW_ASSERT_TRUE( truncated.initialize( 10, 5 ) );
    Archive cut( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncated._field.readState( cut ) );
    SW_EXPECT_TRUE( truncated._field.getUnits().empty() );
}

/**
 * @brief [TacticsSrpgTest] 상태 바이트로 되살린 캠페인이 같은 판을 잇는다 — 작전 지도 · 명단 · 씨앗 · 작전 중이 같은 바이트이고, 같은 작전을 끝내고 다음 칸을 골라도
 *        같은 바이트 · 같은 전장 씨앗이다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( TacticsSrpgTest, StateRoundTripContinuesTheSameCampaign )
{
    SrpgCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kSrpgTestXml, "TacticsSrpgTest" ) );
    RunMapSettings mapSettings;
    mapSettings._floorCount  = 3;
    mapSettings._columnCount = 3;
    mapSettings._pathCount   = 2;
    RunNodeRule rule;
    rule._kind = hashed_string( "Battle" );
    mapSettings._listRule.push_back( rule );
    SrpgCampaign campaign;
    campaign.initialize( mapSettings, 99 );
    SW_EXPECT_EQUAL( 0, campaign.addRosterEntry( catalog, hashed_string( "gm" ), hashed_string( "ace" ), 2 ) );
    SW_EXPECT_EQUAL( 1, campaign.addRosterEntry( catalog, hashed_string( "zaku" ), hashed_string( "grunt" ) ) );
    vector<int32> listChoice;
    campaign.collectChoices( listChoice );
    SW_ASSERT_TRUE( listChoice.empty() == false );
    SW_ASSERT_TRUE( campaign.beginMission( listChoice[0] ) );

    Archive written;
    campaign.writeState( written );
    SrpgCampaign restored;
    restored.initialize( mapSettings, 5 );
    Archive reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, reader.getRemainingBytes() );
    Archive rewritten;
    restored.writeState( rewritten );
    vector<uint8> originalBytes;
    vector<uint8> restoredBytes;
    written.writeData( originalBytes );
    rewritten.writeData( restoredBytes );
    SW_EXPECT_TRUE( originalBytes == restoredBytes );
    SW_EXPECT_TRUE( restored.isInMission() );
    SW_EXPECT_TRUE( restored.getMissionSeed() == campaign.getMissionSeed() );
    SW_ASSERT_EQUAL( 2, static_cast<int32>( restored.getRoster().size() ) );
    SW_EXPECT_EQUAL( 2, restored.getRoster()[0]._pilotLevel.getLevel() );

    // 같은 걸음을 둘 다 — 작전에 내보내 하나를 잃고 이긴 뒤 다음 칸을 고른다.
    vector<int2> listDeploy;
    listDeploy.push_back( int2{ 0, 0 } );
    listDeploy.push_back( int2{ 0, 1 } );
    for ( SrpgCampaign* pCampaign : { &campaign, &restored } )
    {
        SrpgBattlefield mission;
        mission.initialize( &catalog, 8, 4, hashed_string( "plain" ), SrpgSettings{}, pCampaign->getMissionSeed() );
        SW_EXPECT_EQUAL( 2, pCampaign->deployRoster( mission, listDeploy ) );
        mission.grantXp( 0, 120 );
        SW_EXPECT_TRUE( mission.applyDamage( 1, 99999, -1 ) );
        pCampaign->completeMission( mission, SrpgOutcome::Victory );
        vector<int32> listNext;
        pCampaign->collectChoices( listNext );
        SW_ASSERT_TRUE( listNext.empty() == false );
        SW_ASSERT_TRUE( pCampaign->beginMission( listNext[0] ) );
    }
    SW_EXPECT_TRUE( restored.getRoster()[1]._bLost == SW_TRUE );
    SW_EXPECT_TRUE( restored.getMissionSeed() == campaign.getMissionSeed() );
    Archive afterOriginal;
    Archive afterRestored;
    campaign.writeState( afterOriginal );
    restored.writeState( afterRestored );
    afterOriginal.writeData( originalBytes );
    afterRestored.writeData( restoredBytes );
    SW_EXPECT_TRUE( originalBytes == restoredBytes );

    SrpgCampaign truncated;
    truncated.initialize( mapSettings, 5 );
    Archive cut( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncated.readState( cut ) );
    SW_EXPECT_TRUE( truncated.getRoster().empty() );
    SW_EXPECT_FALSE( truncated.isInMission() );
}
