// 몬스터 수집 키트 — 능력치 공식(IV · EV · 성격) · 경험치 그룹, 피해 공식(자속 · 상성 · 급소 · 난수 · 화상), 우선도 → 스피드 순서,
// 상태이상 · 능력 변화 · 날씨, 포획 흔들림 · 파티 6 · 박스, 레벨업 기술 · 진화(레벨 · 아이템 · 친밀도), 야생 조우 테이블, 트레이너 AI 와 결정성.
#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Combat/ElementChart.h"
#include "GameFramework/Base/Foundation/Utility/GameRandom.h"
#include "GameFramework/Kits/Rpg/MonsterCollector/MonsterBattle.h"
#include "GameFramework/Kits/Rpg/MonsterCollector/MonsterCollectorCatalog.h"
#include "GameFramework/Kits/Rpg/MonsterCollector/MonsterInstance.h"
#include "GameFramework/Kits/Rpg/MonsterCollector/MonsterTrainerAi.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr const utf8* kMonsterChartXml = R"(
<ElementChart>
  <Element id="Normal"/><Element id="Fire"/><Element id="Water"/><Element id="Grass"/>
  <Element id="Electric"/><Element id="Ground"/><Element id="Flying"/><Element id="Rock"/>
  <Rule attack="Fire" defend="Grass" multiplier="2"/><Rule attack="Fire" defend="Water" multiplier="0.5"/>
  <Rule attack="Fire" defend="Fire" multiplier="0.5"/><Rule attack="Fire" defend="Rock" multiplier="0.5"/>
  <Rule attack="Water" defend="Fire" multiplier="2"/><Rule attack="Water" defend="Grass" multiplier="0.5"/>
  <Rule attack="Water" defend="Ground" multiplier="2"/><Rule attack="Water" defend="Rock" multiplier="2"/>
  <Rule attack="Grass" defend="Water" multiplier="2"/><Rule attack="Grass" defend="Fire" multiplier="0.5"/>
  <Rule attack="Grass" defend="Ground" multiplier="2"/><Rule attack="Grass" defend="Rock" multiplier="2"/>
  <Rule attack="Electric" defend="Water" multiplier="2"/><Rule attack="Electric" defend="Ground" multiplier="0"/>
  <Rule attack="Electric" defend="Flying" multiplier="2"/>
  <Rule attack="Ground" defend="Fire" multiplier="2"/><Rule attack="Ground" defend="Electric" multiplier="2"/>
  <Rule attack="Normal" defend="Rock" multiplier="0.5"/>
</ElementChart>
)";

    constexpr const utf8* kMonsterCatalogXml = R"(
<MonsterCollectorCatalog>
  <Move id="tackle" type="Normal" category="Physical" power="40" accuracy="100" pp="35"/>
  <Move id="scratch" type="Normal" category="Physical" power="40" accuracy="100" pp="35"/>
  <Move id="quickattack" type="Normal" category="Physical" power="40" accuracy="100" pp="30" priority="1"/>
  <Move id="growl" type="Normal" category="Status" accuracy="100" pp="40" stat="Attack" stages="-1" target="Foe"/>
  <Move id="swordsdance" type="Normal" category="Status" pp="20" stat="Attack" stages="2" target="Self"/>
  <Move id="ember" type="Fire" category="Special" power="40" accuracy="100" pp="25" status="Burn" statusChance="10"/>
  <Move id="willowisp" type="Fire" category="Status" accuracy="0" pp="15" status="Burn"/>
  <Move id="watergun" type="Water" category="Special" power="40" accuracy="100" pp="25"/>
  <Move id="vinewhip" type="Grass" category="Physical" power="45" accuracy="100" pp="25"/>
  <Move id="thundershock" type="Electric" category="Special" power="40" accuracy="100" pp="30" status="Paralysis" statusChance="10"/>
  <Move id="thunderwave" type="Electric" category="Status" accuracy="90" pp="20" status="Paralysis"/>
  <Move id="raindance" type="Water" category="Status" pp="5" weather="Rain"/>
  <Nature id="Hardy" up="Attack" down="Attack"/>
  <Nature id="Adamant" up="Attack" down="SpecialAttack"/>
  <Nature id="Timid" up="Speed" down="Attack"/>
  <Weather id="Rain" boost="Water" weaken="Fire" turns="5"/>
  <Weather id="Sandstorm" chip="16" immune="Rock,Ground" turns="5"/>
  <StatusImmunity status="Burn" types="Fire"/>
  <StatusImmunity status="Paralysis" types="Electric"/>
  <Species id="charmander" types="Fire" stats="39 52 43 60 50 65" catchRate="45" baseExp="62" expGroup="Medium" evYield="0 0 0 0 0 1">
    <Learn level="1" move="scratch"/><Learn level="1" move="growl"/><Learn level="7" move="ember"/>
    <Evolve to="charmeleon" level="16"/>
  </Species>
  <Species id="charmeleon" types="Fire" stats="58 64 58 80 65 80" catchRate="45" baseExp="142"/>
  <Species id="squirtle" types="Water" stats="44 48 65 50 64 43" baseExp="63">
    <Learn level="1" move="tackle"/><Learn level="7" move="watergun"/>
  </Species>
  <Species id="bulbasaur" types="Grass" stats="45 49 49 65 65 45" baseExp="64">
    <Learn level="1" move="tackle"/><Learn level="7" move="vinewhip"/>
  </Species>
  <Species id="pikachu" types="Electric" stats="35 55 40 50 50 90" catchRate="190" baseExp="112" evYield="0 0 0 0 0 2">
    <Learn level="1" move="thundershock"/><Learn level="1" move="growl"/><Learn level="5" move="quickattack"/>
    <Learn level="15" move="swordsdance"/><Learn level="9" move="thunderwave"/>
    <Evolve to="raichu" item="thunderstone"/>
  </Species>
  <Species id="raichu" types="Electric" stats="60 90 55 90 80 110" baseExp="218"/>
  <Species id="eevee" types="Normal" stats="55 55 50 45 65 55" baseExp="65"><Evolve to="espeon" friendship="220"/></Species>
  <Species id="espeon" types="Normal" stats="65 65 60 130 95 110" baseExp="184"/>
  <Species id="geodude" types="Rock,Ground" stats="40 80 100 30 30 20" catchRate="255" baseExp="86" evYield="0 0 1 0 0 0"/>
  <Species id="rattata" types="Normal" stats="30 56 35 25 35 72" catchRate="255" baseExp="57" expGroup="Fast"/>
  <Species id="pidgey" types="Normal,Flying" stats="40 45 40 35 35 56" catchRate="255" baseExp="55"/>
  <Encounter id="route1_day" area="route1" time="Morning,Day">
    <Slot species="pidgey" min="2" max="4" weight="70"/><Slot species="rattata" min="2" max="3" weight="30"/>
  </Encounter>
  <Encounter id="route1_night" area="route1" time="Night"><Slot species="rattata" min="3" max="5" weight="100"/></Encounter>
  <Encounter id="cave" area="cave"><Slot species="geodude" min="7" max="9" weight="1"/></Encounter>
</MonsterCollectorCatalog>
)";

    struct MonsterTestWorld
    {
        MonsterCollectorCatalog _catalog;
        ElementChart            _chart;

        bool initialize()
        {
            return _chart.loadFromXmlText( kMonsterChartXml, "MonsterCollectorTest" ) && _catalog.loadFromXmlText( kMonsterCatalogXml, "MonsterCollectorTest" );
        }

        /** @brief 개체값 0 · 무보정 성격 · 정한 기술로 개체를 만듭니다(능력치를 손으로 셀 수 있게). */
        MonsterInstance makeMonster( const utf8* pSpecies, int32 level, std::initializer_list<const utf8*> listMove ) const
        {
            GameRandom      random( 1 );
            MonsterInstance monster = MonsterRules::createMonster( _catalog, hashed_string( pSpecies ), level, random );
            for ( int32& iv : monster._arrIv )
            {
                iv = 0;
            }
            monster._natureId = hashed_string( "Hardy" );
            for ( MonsterMoveSlot& slot : monster._arrMove )
            {
                slot = MonsterMoveSlot{};
            }
            int32 slotIndex = 0;
            for ( const utf8* pMove : listMove )
            {
                const MonsterMoveDef* pDef = _catalog.findMove( hashed_string( pMove ) );
                if ( pDef != nullptr && slotIndex < MonsterInstance::kMoveSlotCount )
                    monster._arrMove[slotIndex++] = MonsterMoveSlot{ pDef->_id, pDef->_pp, pDef->_pp };
            }
            MonsterRules::recomputeStats( _catalog, monster );
            monster._hp = monster.getMaxHp();
            return monster;
        }
    };

    int32 countKind( const vector<MonsterBattleEvent>& listEvent, MonsterBattleEvent::Kind kind, int32 side = -1 )
    {
        int32 count = 0;
        for ( const MonsterBattleEvent& event : listEvent )
        {
            count += ( event._kind == kind && ( side < 0 || event._side == side ) ) ? 1 : 0;
        }
        return count;
    }

    int32 findFirstMoveSide( const vector<MonsterBattleEvent>& listEvent )
    {
        for ( const MonsterBattleEvent& event : listEvent )
        {
            if ( event._kind == MonsterBattleEvent::Kind::MoveUsed )
                return event._side;
        }
        return -1;
    }

    /** @brief 상태 하나의 바이트입니다. */
    template <typename TState>
    vector<uint8> captureMonsterBytes( const TState& state )
    {
        Archive archive;
        state.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }
} // namespace

/**
 * @brief [MonsterCollectorTest] 성격의 능력치 이름이 틀리면 그 성격을 빼고 경고한다 — 조용히 Attack(무보정 쪽)으로 남지 않는다
 */
SW_TEST_CASE( MonsterCollectorTest, NatureWithAnUnknownStatIsSkippedAndReported )
{
    constexpr const utf8*    kXml = R"(
<MonsterCollectorCatalog>
  <Nature id="Brave" up="Atack" down="Speed"/>
  <Nature id="Calm" up="SpecialDefense" down="Attack"/>
  <Nature id="Hardy"/>
</MonsterCollectorCatalog>
)";
    MonsterCollectorCatalog  catalog;
    test::ScopedLogCollector collector;
    {
        SW_TEST_DEFENSIVE_SCOPE( "a nature naming an unknown stat is skipped with a warning" );
        SW_ASSERT_TRUE( catalog.loadFromXmlText( kXml, "MonsterCollectorTest" ) );
    }
    SW_EXPECT_TRUE( catalog.findNature( hashed_string( "Brave" ) ) == nullptr );
    SW_EXPECT_TRUE_MSG( collector.countContaining( "Brave" ) > 0, collector.joined().c_str() );

    const MonsterNatureDef* pCalm = catalog.findNature( hashed_string( "Calm" ) );
    SW_ASSERT_NOT_NULL( pCalm );
    SW_EXPECT_TRUE( pCalm->_raised == MonsterStat::SpecialDefense );
    SW_EXPECT_TRUE( pCalm->_lowered == MonsterStat::Attack );
    SW_EXPECT_TRUE( catalog.findNature( hashed_string( "Hardy" ) ) != nullptr ); // 칸이 없으면 무보정 성격이다
}

SW_TEST_CASE( MonsterCollectorTest, StatFormulaIvEvNatureAndExpGroups )
{
    // 종족값 100 · 개체값 31 · 노력치 252 · 레벨 100 — 잘 알려진 최대치(HP 404, 무보정 299, 올림 328, 내림 269).
    SW_EXPECT_EQUAL( 404, MonsterRules::computeStat( MonsterStat::Hp, 100, 31, 252, 100, 100 ) );
    SW_EXPECT_EQUAL( 299, MonsterRules::computeStat( MonsterStat::Attack, 100, 31, 252, 100, 100 ) );
    SW_EXPECT_EQUAL( 328, MonsterRules::computeStat( MonsterStat::Attack, 100, 31, 252, 100, 110 ) );
    SW_EXPECT_EQUAL( 269, MonsterRules::computeStat( MonsterStat::Attack, 100, 31, 252, 100, 90 ) );
    // 노력치는 4 마다 1 — 248 과 251 은 같고 252 는 하나 더, 개체값 · 노력치는 범위 밖을 자른다.
    SW_EXPECT_EQUAL( MonsterRules::computeStat( MonsterStat::Speed, 100, 31, 248, 100, 100 ), MonsterRules::computeStat( MonsterStat::Speed, 100, 31, 251, 100, 100 ) );
    SW_EXPECT_EQUAL( 298, MonsterRules::computeStat( MonsterStat::Speed, 100, 31, 251, 100, 100 ) );
    SW_EXPECT_EQUAL( 299, MonsterRules::computeStat( MonsterStat::Attack, 100, 99, 999, 100, 100 ) );
    SW_EXPECT_EQUAL( 160, MonsterRules::computeStat( MonsterStat::Hp, 100, 0, 0, 50, 100 ) );

    // 경험치 그룹: 레벨 100 총량 빠름 800000 · 보통 1000000 · 느림 1250000.
    SW_EXPECT_EQUAL( 800000, MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup::Fast, 100 ) );
    SW_EXPECT_EQUAL( 1000000, MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup::Medium, 100 ) );
    SW_EXPECT_EQUAL( 1250000, MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup::Slow, 100 ) );
    SW_EXPECT_EQUAL( 0, MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup::Slow, 1 ) );
    SW_EXPECT_EQUAL( 99, MonsterCollectorCatalog::computeLevelForExp( MonsterExpGroup::Medium, 999999 ) );
    SW_EXPECT_EQUAL( 100, MonsterCollectorCatalog::computeLevelForExp( MonsterExpGroup::Medium, 1000000 ) );

    MonsterTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );

    // 같은 씨앗이면 같은 개체 — 개체값은 0..31, 기술은 그 레벨까지 배운 것의 마지막 넷(적힌 순서가 흐트러져도 레벨 순).
    GameRandom            randomA( 77 );
    GameRandom            randomB( 77 );
    const MonsterInstance pikachuA = MonsterRules::createMonster( world._catalog, hashed_string( "pikachu" ), 15, randomA );
    const MonsterInstance pikachuB = MonsterRules::createMonster( world._catalog, hashed_string( "pikachu" ), 15, randomB );
    bool                  bSameIv  = true;
    for ( int32 index = 0; index < kMonsterStatCount; ++index )
    {
        bSameIv = bSameIv && pikachuA._arrIv[index] == pikachuB._arrIv[index];
        SW_EXPECT_TRUE( pikachuA._arrIv[index] >= 0 && pikachuA._arrIv[index] <= 31 );
    }
    SW_EXPECT_TRUE( bSameIv );
    SW_EXPECT_TRUE( pikachuA._natureId == pikachuB._natureId );
    SW_EXPECT_EQUAL( 4, pikachuA.countMoves() );
    SW_EXPECT_EQUAL( -1, pikachuA.findMoveSlot( hashed_string( "thundershock" ) ) );
    SW_EXPECT_EQUAL( 3, pikachuA.findMoveSlot( hashed_string( "swordsdance" ) ) );
    SW_EXPECT_EQUAL( 2, pikachuA.findMoveSlot( hashed_string( "thunderwave" ) ) );
    SW_EXPECT_EQUAL( MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup::Medium, 15 ), pikachuA._exp );
    SW_EXPECT_EQUAL( pikachuA.getMaxHp(), pikachuA._hp );

    // 성격: Adamant 는 공격 ×1.1, 특공 ×0.9.
    MonsterInstance adamant        = world.makeMonster( "squirtle", 50, { "tackle" } );
    const int32     neutralAttack  = adamant.getStat( MonsterStat::Attack );
    const int32     neutralSpecial = adamant.getStat( MonsterStat::SpecialAttack );
    adamant._natureId              = hashed_string( "Adamant" );
    MonsterRules::recomputeStats( world._catalog, adamant );
    SW_EXPECT_EQUAL( neutralAttack * 110 / 100, adamant.getStat( MonsterStat::Attack ) );
    SW_EXPECT_EQUAL( neutralSpecial * 90 / 100, adamant.getStat( MonsterStat::SpecialAttack ) );

    // 노력치: 한 능력치 252, 합 510 에서 멈춘다.
    const MonsterSpeciesDef* pPikachu = world._catalog.findSpecies( hashed_string( "pikachu" ) );
    SW_ASSERT_NOT_NULL( pPikachu );
    MonsterInstance trainee = world.makeMonster( "squirtle", 50, { "tackle" } );
    for ( int32 battle = 0; battle < 200; ++battle )
    {
        MonsterRules::addEffortValues( world._catalog, trainee, *pPikachu );
    }
    SW_EXPECT_EQUAL( 252, trainee._arrEv[static_cast<size_t>( MonsterStat::Speed )] );
    trainee._arrEv[0]                 = 252;
    trainee._arrEv[1]                 = 0;
    trainee._arrEv[5]                 = 252;
    const MonsterSpeciesDef* pGeodude = world._catalog.findSpecies( hashed_string( "geodude" ) );
    SW_ASSERT_NOT_NULL( pGeodude );
    for ( int32 battle = 0; battle < 20; ++battle )
    {
        MonsterRules::addEffortValues( world._catalog, trainee, *pGeodude );
    }
    SW_EXPECT_EQUAL( 6, trainee._arrEv[static_cast<size_t>( MonsterStat::Defense )] );
}

SW_TEST_CASE( MonsterCollectorTest, DamageFormulaStabTypeCriticalRandomAndBurn )
{
    // 레벨 50 · 위력 80 · 공격 120 · 방어 100 → ⌊⌊22 × 80 × 120 / 100⌋ / 50⌋ + 2 = 44.
    MonsterDamageInput input;
    input._level   = 50;
    input._power   = 80;
    input._attack  = 120;
    input._defense = 100;
    SW_EXPECT_EQUAL( 44, MonsterBattle::computeDamage( input ) );
    input._bStab          = true;
    input._typeMultiplier = 2.0f;
    SW_EXPECT_EQUAL( 132, MonsterBattle::computeDamage( input ) ); // 44 × 1.5 × 2
    input._bStab = false;
    SW_EXPECT_EQUAL( 88, MonsterBattle::computeDamage( input ) ); // 자속을 끄면 132 가 아니다
    input._bStab         = true;
    input._randomPercent = 85;
    SW_EXPECT_EQUAL( 110, MonsterBattle::computeDamage( input ) ); // ⌊44 × 0.85⌋ = 37 → 55 → 110
    input._randomPercent = 100;
    input._bCritical     = true;
    SW_EXPECT_EQUAL( 198, MonsterBattle::computeDamage( input ) ); // 66 → 99 → 198
    input._bCritical      = false;
    input._bStab          = false;
    input._typeMultiplier = 1.0f;
    input._bBurned        = true;
    SW_EXPECT_EQUAL( 22, MonsterBattle::computeDamage( input ) );
    input._bBurned           = false;
    input._weatherMultiplier = 1.5f;
    SW_EXPECT_EQUAL( 66, MonsterBattle::computeDamage( input ) );
    input._typeMultiplier = 0.0f;
    SW_EXPECT_EQUAL( 0, MonsterBattle::computeDamage( input ) ); // 면역
    input._typeMultiplier    = 0.25f;
    input._weatherMultiplier = 1.0f;
    input._power             = 1;
    input._attack            = 1;
    input._defense           = 999;
    SW_EXPECT_EQUAL( 1, MonsterBattle::computeDamage( input ) ); // 상성이 0 이 아니면 최소 1

    SW_EXPECT_NEAR_EQUAL( 4.0f, MonsterBattle::computeStageMultiplier( 6 ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, MonsterBattle::computeStageMultiplier( -6 ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f / 3.0f, MonsterBattle::computeStageMultiplier( -1 ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, MonsterBattle::computeStageMultiplier( 9 ), 1.0e-5f );
    SW_EXPECT_EQUAL( 24, MonsterBattle::computeCriticalDivisor( 0 ) );
    SW_EXPECT_EQUAL( 1, MonsterBattle::computeCriticalDivisor( 3 ) );

    // 전투 안의 피해: 물대포(특수 · 자속 · 2 배)가 불꽃 개체에 공식 범위 [85%, 100% × 급소] 안에서 들어간다.
    MonsterTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    const MonsterInstance squirtle   = world.makeMonster( "squirtle", 50, { "watergun" } );
    const MonsterInstance charmander = world.makeMonster( "charmander", 50, { "growl" } );
    MonsterBattle         battle;
    battle.initialize( &world._catalog, &world._chart, 5 );
    battle.start( { squirtle }, { charmander }, false );
    SW_EXPECT_NEAR_EQUAL( 2.0f, battle.computeTypeMultiplier( hashed_string( "Water" ), charmander ), 1.0e-5f );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 0 ) ) );
    SW_EXPECT_FALSE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 2 ) ) );    // 빈 칸
    SW_EXPECT_FALSE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeBall( 1.0f ) ) ); // 트레이너전
    battle.resolveRound();
    vector<MonsterBattleEvent> listEvent;
    battle.drainEvents( listEvent );
    int32 dealt = -1;
    for ( const MonsterBattleEvent& event : listEvent )
    {
        if ( event._kind == MonsterBattleEvent::Kind::Damage && event._side == MonsterBattle::kFoeSide )
            dealt = event._value;
    }
    MonsterDamageInput low;
    low._level              = 50;
    low._power              = 40;
    low._attack             = squirtle.getStat( MonsterStat::SpecialAttack );
    low._defense            = charmander.getStat( MonsterStat::SpecialDefense );
    low._bStab              = true;
    low._typeMultiplier     = 2.0f;
    low._randomPercent      = 85;
    MonsterDamageInput high = low;
    high._randomPercent     = 100;
    high._bCritical         = true;
    SW_EXPECT_TRUE( dealt >= MonsterBattle::computeDamage( low ) && dealt <= MonsterBattle::computeDamage( high ) );
    SW_EXPECT_EQUAL( charmander.getMaxHp() - dealt, battle.getActive( MonsterBattle::kFoeSide )._hp );
    SW_EXPECT_EQUAL( 24, battle.getActive( MonsterBattle::kPlayerSide )._arrMove[0]._pp ); // PP 1 소모
}

SW_TEST_CASE( MonsterCollectorTest, PriorityThenSpeedOrdersTheRoundAndParalysisHalvesSpeed )
{
    MonsterTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    // 레벨 50 · 개체값 0: 피카츄 스피드 95, 꼬부기 48.
    const MonsterInstance pikachu  = world.makeMonster( "pikachu", 50, { "thundershock", "growl" } );
    const MonsterInstance squirtle = world.makeMonster( "squirtle", 50, { "tackle", "watergun" } );
    SW_EXPECT_EQUAL( 95, pikachu.getStat( MonsterStat::Speed ) );
    SW_EXPECT_EQUAL( 48, squirtle.getStat( MonsterStat::Speed ) );

    MonsterBattle              battle;
    vector<MonsterBattleEvent> listEvent;
    battle.initialize( &world._catalog, &world._chart, 11 );
    battle.start( { squirtle }, { pikachu }, false );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 0 ) ) );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kFoeSide, MonsterAction::makeMove( 1 ) ) );
    battle.resolveRound();
    listEvent.clear();
    battle.drainEvents( listEvent );
    SW_EXPECT_EQUAL( MonsterBattle::kFoeSide, findFirstMoveSide( listEvent ) ); // 빠른 쪽 먼저

    // 우선도 +1(전광석화)은 스피드를 이긴다.
    MonsterInstance quickSquirtle = world.makeMonster( "squirtle", 50, { "quickattack" } );
    battle.start( { quickSquirtle }, { pikachu }, false );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 0 ) ) );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kFoeSide, MonsterAction::makeMove( 1 ) ) );
    battle.resolveRound();
    listEvent.clear();
    battle.drainEvents( listEvent );
    SW_EXPECT_EQUAL( MonsterBattle::kPlayerSide, findFirstMoveSide( listEvent ) );

    // 마비는 스피드 절반 — 95 → 47.5 < 48 이라 꼬부기가 먼저다.
    MonsterInstance paralyzed = pikachu;
    paralyzed._status         = MonsterStatus::Paralysis;
    battle.start( { squirtle }, { paralyzed }, false );
    SW_EXPECT_NEAR_EQUAL( 47.5f, battle.computeEffectiveSpeed( MonsterBattle::kFoeSide ), 1.0e-4f );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 0 ) ) );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kFoeSide, MonsterAction::makeMove( 1 ) ) );
    battle.resolveRound();
    listEvent.clear();
    battle.drainEvents( listEvent );
    SW_EXPECT_EQUAL( MonsterBattle::kPlayerSide, findFirstMoveSide( listEvent ) );

    // 교체는 기술보다 먼저 — 교체해 들어온 개체가 상대 기술을 맞는다.
    const MonsterInstance bulbasaur = world.makeMonster( "bulbasaur", 50, { "tackle" } );
    battle.start( { squirtle, bulbasaur }, { pikachu }, false );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeSwitch( 1 ) ) );
    SW_EXPECT_FALSE( battle.setAction( MonsterBattle::kFoeSide, MonsterAction::makeSwitch( 0 ) ) ); // 이미 나와 있다
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kFoeSide, MonsterAction::makeMove( 0 ) ) );
    battle.resolveRound();
    listEvent.clear();
    battle.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() == false && listEvent.front()._kind == MonsterBattleEvent::Kind::Switched );
    SW_EXPECT_EQUAL( 1, battle.getActiveIndex( MonsterBattle::kPlayerSide ) );
    SW_EXPECT_TRUE( battle.getActive( MonsterBattle::kPlayerSide )._hp < bulbasaur.getMaxHp() );
}

SW_TEST_CASE( MonsterCollectorTest, StatusConditionsStatStagesAndWeather )
{
    MonsterTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    MonsterInstance toxic  = world.makeMonster( "squirtle", 50, { "tackle" } );
    toxic._status          = MonsterStatus::Toxic;
    toxic._statusTurns     = 1;
    MonsterInstance burned = world.makeMonster( "bulbasaur", 50, { "tackle" } );
    burned._status         = MonsterStatus::Burn;
    const int32 toxicMax   = toxic.getMaxHp();
    const int32 burnMax    = burned.getMaxHp();

    MonsterBattle              battle;
    vector<MonsterBattleEvent> listEvent;
    battle.initialize( &world._catalog, &world._chart, 3 );
    battle.start( { toxic }, { burned }, false );
    battle.resolveRound(); // 둘 다 행동 없음 — 턴 끝 피해만
    SW_EXPECT_EQUAL( toxicMax - toxicMax / 16, battle.getActive( MonsterBattle::kPlayerSide )._hp );
    SW_EXPECT_EQUAL( burnMax - burnMax / 16, battle.getActive( MonsterBattle::kFoeSide )._hp );
    battle.resolveRound(); // 맹독은 n/16 로 커진다
    SW_EXPECT_EQUAL( toxicMax - toxicMax / 16 - toxicMax * 2 / 16, battle.getActive( MonsterBattle::kPlayerSide )._hp );

    // 수면 2 턴: 두 번 잠들어 있고 세 번째에 깨어 행동한다.
    MonsterInstance sleeper = world.makeMonster( "squirtle", 50, { "tackle" } );
    sleeper._status         = MonsterStatus::Sleep;
    sleeper._statusTurns    = 2;
    battle.start( { sleeper }, { world.makeMonster( "geodude", 50, { "tackle" } ) }, false );
    for ( int32 round = 0; round < 3; ++round )
    {
        SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 0 ) ) );
        battle.resolveRound();
    }
    listEvent.clear();
    battle.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 2, countKind( listEvent, MonsterBattleEvent::Kind::Asleep ) );
    SW_EXPECT_EQUAL( 1, countKind( listEvent, MonsterBattleEvent::Kind::Woke ) );
    SW_EXPECT_EQUAL( 1, countKind( listEvent, MonsterBattleEvent::Kind::MoveUsed, MonsterBattle::kPlayerSide ) );

    // 상태이상 면역 타입: 도깨비불은 불꽃에 듣지 않고 물에는 반드시 듣는다(명중 0 = 반드시 맞는다).
    battle.start( { world.makeMonster( "charmander", 50, { "willowisp" } ) }, { world.makeMonster( "charmander", 50, { "growl" } ) }, false );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 0 ) ) );
    battle.resolveRound();
    SW_EXPECT_TRUE( battle.getActive( MonsterBattle::kFoeSide )._status == MonsterStatus::None );
    battle.start( { world.makeMonster( "charmander", 50, { "willowisp" } ) }, { world.makeMonster( "squirtle", 50, { "growl" } ) }, false );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 0 ) ) );
    battle.resolveRound();
    SW_EXPECT_TRUE( battle.getActive( MonsterBattle::kFoeSide )._status == MonsterStatus::Burn );

    // 능력 변화: 칼춤 넷 — +2 +2 +2 다음은 더 오르지 않는다(+6 에서 멈춤).
    listEvent.clear();
    battle.drainEvents( listEvent );
    battle.start( { world.makeMonster( "pikachu", 50, { "swordsdance" } ) }, { world.makeMonster( "geodude", 50, { "growl" } ) }, false );
    for ( int32 round = 0; round < 4; ++round )
    {
        SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 0 ) ) );
        battle.resolveRound();
    }
    listEvent.clear();
    battle.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 6, battle.getStage( MonsterBattle::kPlayerSide, MonsterStat::Attack ) );
    SW_EXPECT_TRUE( listEvent.empty() == false );
    int32 lastChange = -1;
    for ( const MonsterBattleEvent& event : listEvent )
    {
        if ( event._kind == MonsterBattleEvent::Kind::StatChanged )
            lastChange = event._value;
    }
    SW_EXPECT_EQUAL( 0, lastChange );

    // 날씨: 비바라기 — 물 ×1.5 · 불 ×0.5, 5 턴 뒤 그친다. 모래바람은 바위 · 땅을 빼고 1/16.
    battle.start( { world.makeMonster( "squirtle", 50, { "raindance" } ) }, { world.makeMonster( "geodude", 50, { "growl" } ) }, false );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 0 ) ) );
    battle.resolveRound();
    SW_EXPECT_TRUE( battle.getWeather() == hashed_string( "Rain" ) );
    for ( int32 round = 0; round < 3; ++round )
    {
        battle.resolveRound();
    }
    SW_EXPECT_EQUAL( 1, battle.getWeatherTurns() );
    battle.resolveRound();
    SW_EXPECT_TRUE( battle.getWeather().empty() );

    const MonsterInstance sandSquirtle = world.makeMonster( "squirtle", 50, { "tackle" } );
    battle.start( { sandSquirtle }, { world.makeMonster( "geodude", 50, { "tackle" } ) }, false );
    battle.setWeather( hashed_string( "Sandstorm" ), 0 );
    battle.resolveRound();
    battle.resolveRound();
    SW_EXPECT_EQUAL( sandSquirtle.getMaxHp() - 2 * ( sandSquirtle.getMaxHp() / 16 ), battle.getActive( MonsterBattle::kPlayerSide )._hp );
    SW_EXPECT_EQUAL( battle.getActive( MonsterBattle::kFoeSide ).getMaxHp(), battle.getActive( MonsterBattle::kFoeSide )._hp );
    SW_EXPECT_EQUAL( -1, battle.getWeatherTurns() ); // 필드 날씨는 그치지 않는다
}

SW_TEST_CASE( MonsterCollectorTest, CaptureShakesPartyBoxAndEscape )
{
    // a = ⌊(3M − 2H) × 포획률 × 볼 / 3M⌋ — 가득 찬 HP · 45 · 볼 1 은 15, b = ⌊1048560 / ⌊√⌊√1114112⌋⌋⌋ = 32767.
    GameRandom                 random( 9 );
    const MonsterCaptureResult full = MonsterBattle::computeCapture( 100, 100, 45, 1.0f, MonsterStatus::None, random );
    SW_EXPECT_EQUAL( 15, full._catchValue );
    SW_EXPECT_EQUAL( 32767, full._shakeChance );
    const MonsterCaptureResult lowHp  = MonsterBattle::computeCapture( 100, 1, 45, 1.0f, MonsterStatus::None, random );
    const MonsterCaptureResult asleep = MonsterBattle::computeCapture( 100, 1, 45, 1.0f, MonsterStatus::Sleep, random );
    const MonsterCaptureResult burned = MonsterBattle::computeCapture( 100, 1, 45, 1.0f, MonsterStatus::Burn, random );
    SW_EXPECT_EQUAL( 44, lowHp._catchValue );
    SW_EXPECT_EQUAL( 88, asleep._catchValue );
    SW_EXPECT_EQUAL( 66, burned._catchValue );
    // 255 이상이면 흔들림 없이 반드시 — 포획률 255 · HP 1 은 253 이라 볼 배율 1.5 가 있어야 넘는다.
    SW_EXPECT_EQUAL( 253, MonsterBattle::computeCapture( 100, 1, 255, 1.0f, MonsterStatus::None, random )._catchValue );
    SW_EXPECT_TRUE( MonsterBattle::computeCapture( 100, 1, 255, 1.5f, MonsterStatus::None, random )._bCaught );

    // 결정성과 경향: 같은 씨앗은 같은 흔들림, HP 를 깎을수록 더 잘 잡힌다.
    GameRandom randomA( 2024 );
    GameRandom randomB( 2024 );
    GameRandom randomLow( 2025 );
    int32      fullCaught = 0;
    int32      lowCaught  = 0;
    bool       bSame      = true;
    for ( int32 trial = 0; trial < 2000; ++trial )
    {
        const MonsterCaptureResult resultA = MonsterBattle::computeCapture( 100, 100, 45, 1.0f, MonsterStatus::None, randomA );
        const MonsterCaptureResult resultB = MonsterBattle::computeCapture( 100, 100, 45, 1.0f, MonsterStatus::None, randomB );
        bSame                              = bSame && resultA._shakes == resultB._shakes && resultA._bCaught == resultB._bCaught;
        fullCaught += resultA._bCaught ? 1 : 0;
        lowCaught += MonsterBattle::computeCapture( 100, 1, 45, 1.0f, MonsterStatus::None, randomLow )._bCaught ? 1 : 0;
    }
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_TRUE( fullCaught > 60 && fullCaught < 200 ); // (32767 / 65536)^4 ≈ 6.25%
    SW_EXPECT_TRUE( lowCaught > fullCaught * 2 );

    // 전투 안: 야생 꼬렛(포획률 255)을 마스터볼급 배율로 — 잡히면 파티(6 까지) → 박스.
    MonsterTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    MonsterBattle battle;
    battle.initialize( &world._catalog, &world._chart, 1 );
    battle.start( { world.makeMonster( "squirtle", 10, { "tackle" } ) }, { world.makeMonster( "rattata", 3, { "tackle" } ) }, true );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeBall( 255.0f ) ) );
    SW_EXPECT_FALSE( battle.setAction( MonsterBattle::kFoeSide, MonsterAction::makeBall( 1.0f ) ) );
    battle.resolveRound();
    SW_EXPECT_TRUE( battle.getOutcome() == MonsterBattleOutcome::Captured );
    SW_EXPECT_TRUE( battle.getCaptured()._speciesId == hashed_string( "rattata" ) );

    MonsterStorage storage;
    storage.initialize( 2 );
    for ( int32 index = 0; index < MonsterStorage::kPartySize; ++index )
    {
        SW_EXPECT_TRUE( storage.add( battle.getCaptured() ) == MonsterStoragePlace::Party );
    }
    SW_EXPECT_TRUE( storage.add( battle.getCaptured() ) == MonsterStoragePlace::Box );
    SW_EXPECT_TRUE( storage.add( battle.getCaptured() ) == MonsterStoragePlace::Box );
    SW_EXPECT_TRUE( storage.add( battle.getCaptured() ) == MonsterStoragePlace::Full );
    SW_EXPECT_FALSE( storage.withdrawFromBox( 0 ) ); // 파티가 가득
    for ( int32 index = 1; index < MonsterStorage::kPartySize; ++index )
    {
        storage.getParty()[static_cast<size_t>( index )]._hp = 0;
    }
    SW_EXPECT_FALSE( storage.depositToBox( 0 ) ); // 박스도 가득
    storage.initialize( 5 );
    SW_EXPECT_TRUE( storage.add( battle.getCaptured() ) == MonsterStoragePlace::Party );
    MonsterInstance fainted = battle.getCaptured();
    fainted._hp             = 0;
    SW_EXPECT_TRUE( storage.add( fainted ) == MonsterStoragePlace::Party );
    SW_EXPECT_FALSE( storage.depositToBox( 0 ) ); // 마지막 싸울 수 있는 개체는 맡기지 못한다
    SW_EXPECT_TRUE( storage.depositToBox( 1 ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( storage.getBox().size() ) );
    storage.restoreParty();
    SW_EXPECT_TRUE( storage.withdrawFromBox( 0 ) );
    SW_EXPECT_TRUE( storage.hasUsableMonster() );

    // 도망: 내가 빠르면 반드시, 느리면 시도할수록 쉬워진다(3 세대 공식). 트레이너전은 도망 불가.
    battle.start( { world.makeMonster( "pikachu", 50, { "tackle" } ) }, { world.makeMonster( "geodude", 50, { "tackle" } ) }, true );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeRun() ) );
    battle.resolveRound();
    SW_EXPECT_TRUE( battle.getOutcome() == MonsterBattleOutcome::Escaped );
    battle.start( { world.makeMonster( "geodude", 5, { "tackle" } ) }, { world.makeMonster( "pikachu", 50, { "growl" } ) }, true );
    int32 attempts = 0;
    while ( battle.getOutcome() == MonsterBattleOutcome::Ongoing && attempts < 20 )
    {
        SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeRun() ) );
        battle.resolveRound();
        ++attempts;
    }
    SW_EXPECT_TRUE( battle.getOutcome() == MonsterBattleOutcome::Escaped );
    SW_EXPECT_TRUE( attempts <= 10 ); // F 는 시도마다 30 씩 오른다 — 9 번째에는 255 를 넘는다
    battle.start( { world.makeMonster( "pikachu", 50, { "tackle" } ) }, { world.makeMonster( "geodude", 50, { "tackle" } ) }, false );
    SW_EXPECT_FALSE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeRun() ) );
}

SW_TEST_CASE( MonsterCollectorTest, LevelUpLearnsMovesAndEvolvesByLevelItemAndFriendship )
{
    MonsterTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    GameRandom      random( 4 );
    MonsterInstance charmander = MonsterRules::createMonster( world._catalog, hashed_string( "charmander" ), 6, random );
    SW_EXPECT_EQUAL( 216, charmander._exp );
    SW_EXPECT_EQUAL( 2, charmander.countMoves() );

    vector<MonsterGrowthEvent> listGrowth;
    const int32                gained = MonsterRules::gainExp( world._catalog, charmander, MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup::Medium, 16 ) - 216, listGrowth );
    SW_EXPECT_EQUAL( 10, gained );
    SW_EXPECT_EQUAL( 16, charmander._level );
    SW_EXPECT_EQUAL( 120, charmander._friendship ); // 70 + 5 × 10
    int32 levelUps = 0;
    bool  bEmber   = false;
    bool  bEvolve  = false;
    for ( const MonsterGrowthEvent& event : listGrowth )
    {
        levelUps += event._kind == MonsterGrowthEvent::Kind::LevelUp ? 1 : 0;
        bEmber  = bEmber || ( event._kind == MonsterGrowthEvent::Kind::LearnedMove && event._id == hashed_string( "ember" ) && event._level == 7 );
        bEvolve = bEvolve || ( event._kind == MonsterGrowthEvent::Kind::CanEvolve && event._id == hashed_string( "charmeleon" ) );
    }
    SW_EXPECT_EQUAL( 10, levelUps );
    SW_EXPECT_TRUE( bEmber );
    SW_EXPECT_TRUE( bEvolve );
    const int32 attackBefore = charmander.getStat( MonsterStat::Attack );
    const int32 hpLost       = 5;
    charmander._hp -= hpLost;
    SW_ASSERT_TRUE( MonsterRules::evolve( world._catalog, charmander, hashed_string( "charmeleon" ) ) );
    SW_EXPECT_TRUE( charmander._speciesId == hashed_string( "charmeleon" ) );
    SW_EXPECT_TRUE( charmander.getStat( MonsterStat::Attack ) > attackBefore );
    SW_EXPECT_EQUAL( charmander.getMaxHp() - hpLost, charmander._hp ); // 늘어난 최대 HP 만큼 지금 HP 도 는다
    SW_EXPECT_EQUAL( 3, charmander.countMoves() );

    // 아이템 진화: 레벨업으로는 안 되고 맞는 돌에만.
    MonsterInstance pikachu = world.makeMonster( "pikachu", 14, { "thundershock", "growl", "quickattack", "thunderwave" } );
    SW_EXPECT_TRUE( MonsterRules::findEvolution( world._catalog, pikachu, hashed_string{} ).empty() );
    SW_EXPECT_TRUE( MonsterRules::findEvolution( world._catalog, pikachu, hashed_string( "firestone" ) ).empty() );
    SW_EXPECT_TRUE( MonsterRules::findEvolution( world._catalog, pikachu, hashed_string( "thunderstone" ) ) == hashed_string( "raichu" ) );

    // 칸이 넷 다 차면 새 기술은 막히고, 게임이 고른 칸을 바꿔 배운다.
    pikachu._exp = MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup::Medium, 14 );
    listGrowth.clear();
    (void)MonsterRules::gainExp( world._catalog, pikachu, MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup::Medium, 15 ) - pikachu._exp, listGrowth );
    bool bBlocked = false;
    for ( const MonsterGrowthEvent& event : listGrowth )
    {
        bBlocked = bBlocked || ( event._kind == MonsterGrowthEvent::Kind::MoveLearnBlocked && event._id == hashed_string( "swordsdance" ) );
    }
    SW_EXPECT_TRUE( bBlocked );
    SW_EXPECT_EQUAL( -1, pikachu.findMoveSlot( hashed_string( "swordsdance" ) ) );
    SW_EXPECT_FALSE( MonsterRules::learnMove( world._catalog, pikachu, hashed_string( "swordsdance" ) ) );
    SW_EXPECT_TRUE( MonsterRules::learnMove( world._catalog, pikachu, hashed_string( "swordsdance" ), 1 ) );
    SW_EXPECT_EQUAL( 1, pikachu.findMoveSlot( hashed_string( "swordsdance" ) ) );
    SW_EXPECT_FALSE( MonsterRules::learnMove( world._catalog, pikachu, hashed_string( "swordsdance" ), 2 ) ); // 이미 안다

    // 친밀도 진화: 219 에서 레벨이 오르면(+5) 220 을 넘어 진화 조건이 맞는다. 218 이하로 시작하지 않으면 레벨업 없이는 안 된다.
    MonsterInstance eevee = world.makeMonster( "eevee", 10, {} );
    eevee._exp            = MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup::Medium, 10 );
    SW_EXPECT_TRUE( MonsterRules::findEvolution( world._catalog, eevee, hashed_string{} ).empty() );
    eevee._friendship = 219;
    listGrowth.clear();
    (void)MonsterRules::gainExp( world._catalog, eevee, MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup::Medium, 11 ) - eevee._exp, listGrowth );
    SW_EXPECT_TRUE( listGrowth.empty() == false && listGrowth.back()._kind == MonsterGrowthEvent::Kind::CanEvolve &&
                    listGrowth.back()._id == hashed_string( "espeon" ) );

    // 경험치 지급: ⌊b × L / 7⌋, 트레이너전 ×1.5.
    const MonsterSpeciesDef* pCharmander = world._catalog.findSpecies( hashed_string( "charmander" ) );
    SW_ASSERT_NOT_NULL( pCharmander );
    SW_EXPECT_EQUAL( 88, MonsterRules::computeExpYield( *pCharmander, 10, false ) );
    SW_EXPECT_EQUAL( 132, MonsterRules::computeExpYield( *pCharmander, 10, true ) );
}

SW_TEST_CASE( MonsterCollectorTest, WildEncounterTableRespectsAreaTimeLevelAndWeight )
{
    MonsterTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    GameRandom    random( 31 );
    hashed_string speciesId;
    int32         level      = 0;
    int32         pidgeyDay  = 0;
    bool          bDayLevels = true;
    for ( int32 roll = 0; roll < 2000; ++roll )
    {
        SW_ASSERT_TRUE( world._catalog.rollEncounter( hashed_string( "route1" ), hashed_string( "Day" ), random, speciesId, level ) );
        if ( speciesId == hashed_string( "pidgey" ) )
        {
            ++pidgeyDay;
            bDayLevels = bDayLevels && level >= 2 && level <= 4;
        }
        else
        {
            bDayLevels = bDayLevels && speciesId == hashed_string( "rattata" ) && level >= 2 && level <= 3;
        }
    }
    SW_EXPECT_TRUE( bDayLevels );
    SW_EXPECT_TRUE( pidgeyDay > 1300 && pidgeyDay < 1500 ); // 가중치 70 : 30

    bool bNightOnlyRattata = true;
    for ( int32 roll = 0; roll < 200; ++roll )
    {
        SW_ASSERT_TRUE( world._catalog.rollEncounter( hashed_string( "route1" ), hashed_string( "Night" ), random, speciesId, level ) );
        bNightOnlyRattata = bNightOnlyRattata && speciesId == hashed_string( "rattata" ) && level >= 3 && level <= 5;
    }
    SW_EXPECT_TRUE( bNightOnlyRattata );
    // 시간대가 없는 테이블은 아무 때나, 없는 지역은 조우가 없다.
    SW_EXPECT_TRUE( world._catalog.rollEncounter( hashed_string( "cave" ), hashed_string( "Night" ), random, speciesId, level ) );
    SW_EXPECT_TRUE( speciesId == hashed_string( "geodude" ) && level >= 7 && level <= 9 );
    SW_EXPECT_FALSE( world._catalog.rollEncounter( hashed_string( "sea" ), hashed_string( "Day" ), random, speciesId, level ) );
    SW_EXPECT_FALSE( world._catalog.rollEncounter( hashed_string( "route1" ), hashed_string( "Dusk" ), random, speciesId, level ) );

    GameRandom    randomA( 500 );
    GameRandom    randomB( 500 );
    hashed_string speciesB;
    int32         levelB = 0;
    bool          bSame  = true;
    for ( int32 roll = 0; roll < 50; ++roll )
    {
        (void)world._catalog.rollEncounter( hashed_string( "route1" ), hashed_string( "Morning" ), randomA, speciesId, level );
        (void)world._catalog.rollEncounter( hashed_string( "route1" ), hashed_string( "Morning" ), randomB, speciesB, levelB );
        bSame = bSame && speciesId == speciesB && level == levelB;
    }
    SW_EXPECT_TRUE( bSame );
}

SW_TEST_CASE( MonsterCollectorTest, TrainerAiPicksBestExpectedDamageSwitchesWhenDisadvantagedAndIsDeterministic )
{
    MonsterTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    const MonsterInstance squirtle = world.makeMonster( "squirtle", 30, { "tackle", "watergun" } );

    // 땅 · 바위 앞의 피카츄: 전기는 0 배라 첫 칸이어도 고르지 않고 전광석화(0.5 배)를 고른다.
    MonsterBattle battle;
    battle.initialize( &world._catalog, &world._chart, 8 );
    battle.start( { world.makeMonster( "geodude", 30, { "tackle" } ) }, { world.makeMonster( "pikachu", 30, { "thundershock", "quickattack" } ) }, false );
    MonsterAction action = MonsterTrainerAi::chooseAction( battle, MonsterBattle::kFoeSide );
    SW_EXPECT_TRUE( action._kind == MonsterActionKind::Move && action._index == 1 );
    // 물 앞에서는 10 만볼트 쪽(2 배)이다.
    battle.start( { squirtle }, { world.makeMonster( "pikachu", 30, { "quickattack", "thundershock" } ) }, false );
    action = MonsterTrainerAi::chooseAction( battle, MonsterBattle::kFoeSide );
    SW_EXPECT_TRUE( action._kind == MonsterActionKind::Move && action._index == 1 );

    // 불리(불꽃 기술만 · 물이 2 배로 들어옴)하면 교체 — 후보 중 기대 피해가 가장 큰 개체로.
    const MonsterInstance charmander = world.makeMonster( "charmander", 30, { "ember" } );
    const MonsterInstance bulbasaur  = world.makeMonster( "bulbasaur", 30, { "vinewhip" } );
    const MonsterInstance pikachu    = world.makeMonster( "pikachu", 30, { "thundershock" } );
    battle.start( { squirtle }, { charmander, bulbasaur, pikachu }, false );
    SW_EXPECT_TRUE( MonsterTrainerAi::isDisadvantaged( battle, MonsterBattle::kFoeSide, 0 ) );
    float32 bulbasaurDamage = 0.0f;
    float32 pikachuDamage   = 0.0f;
    (void)MonsterTrainerAi::chooseBestMoveSlot( battle, MonsterBattle::kFoeSide, 1, bulbasaurDamage );
    (void)MonsterTrainerAi::chooseBestMoveSlot( battle, MonsterBattle::kFoeSide, 2, pikachuDamage );
    action = MonsterTrainerAi::chooseAction( battle, MonsterBattle::kFoeSide );
    SW_EXPECT_TRUE( action._kind == MonsterActionKind::Switch );
    SW_EXPECT_EQUAL( bulbasaurDamage >= pikachuDamage ? 1 : 2, action._index );
    // 갈 곳이 없으면(다른 개체가 쓰러졌다) 불리해도 버틴다.
    MonsterInstance faintedBulbasaur = bulbasaur;
    MonsterInstance faintedPikachu   = pikachu;
    faintedBulbasaur._hp             = 0;
    faintedPikachu._hp               = 0;
    battle.start( { squirtle }, { charmander, faintedBulbasaur, faintedPikachu }, false );
    action = MonsterTrainerAi::chooseAction( battle, MonsterBattle::kFoeSide );
    SW_EXPECT_TRUE( action._kind == MonsterActionKind::Move );

    // 양쪽을 AI 로 끝까지 — 같은 씨앗이면 같은 사건 줄과 같은 결과다.
    const auto runBattle = [&]( uint32 seed, vector<MonsterBattleEvent>& outListEvent ) -> MonsterBattleOutcome
    {
        MonsterBattle fullBattle;
        fullBattle.initialize( &world._catalog, &world._chart, seed );
        fullBattle.start( { squirtle, world.makeMonster( "pikachu", 30, { "thundershock", "quickattack" } ) }, { charmander, bulbasaur, pikachu }, false );
        outListEvent.clear();
        vector<MonsterBattleEvent> listRound;
        for ( int32 round = 0; round < 200 && fullBattle.getOutcome() == MonsterBattleOutcome::Ongoing; ++round )
        {
            for ( int32 side = 0; side < MonsterBattle::kSideCount; ++side )
            {
                if ( fullBattle.needsSwitch( side ) )
                    (void)fullBattle.switchFainted( side, MonsterTrainerAi::chooseReplacement( fullBattle, side ) );
            }
            for ( int32 side = 0; side < MonsterBattle::kSideCount; ++side )
            {
                (void)fullBattle.setAction( side, MonsterTrainerAi::chooseAction( fullBattle, side ) );
            }
            fullBattle.resolveRound();
            listRound.clear();
            fullBattle.drainEvents( listRound );
            outListEvent.insert( outListEvent.end(), listRound.begin(), listRound.end() );
        }
        return fullBattle.getOutcome();
    };
    vector<MonsterBattleEvent> listA;
    vector<MonsterBattleEvent> listB;
    const MonsterBattleOutcome outcomeA = runBattle( 42, listA );
    const MonsterBattleOutcome outcomeB = runBattle( 42, listB );
    SW_EXPECT_TRUE( outcomeA != MonsterBattleOutcome::Ongoing );
    SW_EXPECT_TRUE( outcomeA == outcomeB );
    SW_ASSERT_TRUE( listA.size() == listB.size() );
    bool bSame = true;
    for ( size_t index = 0; index < listA.size(); ++index )
    {
        bSame = bSame && listA[index]._kind == listB[index]._kind && listA[index]._side == listB[index]._side && listA[index]._value == listB[index]._value;
    }
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_TRUE( countKind( listA, MonsterBattleEvent::Kind::Switched ) > 0 );
}

/**
 * @brief [MonsterCollectorTest] 보관함 · 전투 상태 바이트 — 파티 · 박스 개체, 양쪽 개체 · 능력 변화 · 둔 행동 · 턴 순서 · 난수 · 날씨가 그대로 와서 같은 라운드가 이어진다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( MonsterCollectorTest, StateRoundTripContinuesTheSameBattle )
{
    MonsterTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );

    MonsterStorage storage;
    storage.initialize( 3 );
    GameRandom random( 21 );
    for ( const utf8* pSpecies : { "squirtle", "bulbasaur", "pikachu" } )
    {
        SW_EXPECT_TRUE( storage.add( MonsterRules::createMonster( world._catalog, hashed_string( pSpecies ), 12, random ) ) == MonsterStoragePlace::Party );
    }
    storage.getParty()[0]._nickname = "Shelly";
    SW_ASSERT_TRUE( storage.depositToBox( 2 ) );

    const vector<uint8> storageBytes = captureMonsterBytes( storage );
    MonsterStorage      restoredStorage;
    restoredStorage.initialize( 3 );
    Archive storageReader( storageBytes.data(), storageBytes.size() );
    SW_ASSERT_TRUE( restoredStorage.readState( storageReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, storageReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureMonsterBytes( restoredStorage ) == storageBytes );
    SW_ASSERT_TRUE( restoredStorage.getParty().size() == 2 && restoredStorage.getBox().size() == 1 );
    SW_EXPECT_STREQ( "Shelly", restoredStorage.getParty()[0]._nickname.c_str() );
    SW_EXPECT_TRUE( restoredStorage.getBox()[0]._speciesId == hashed_string( "pikachu" ) );

    // 같은 걸음을 둘 다 더 돌리면 바이트가 같다.
    SW_ASSERT_TRUE( storage.withdrawFromBox( 0 ) );
    SW_ASSERT_TRUE( restoredStorage.withdrawFromBox( 0 ) );
    SW_ASSERT_TRUE( storage.swapPartyOrder( 0, 2 ) );
    SW_ASSERT_TRUE( restoredStorage.swapPartyOrder( 0, 2 ) );
    SW_EXPECT_TRUE( captureMonsterBytes( storage ) == captureMonsterBytes( restoredStorage ) );

    MonsterStorage noBox;
    noBox.initialize( 0 );
    Archive noBoxReader( storageBytes.data(), storageBytes.size() );
    SW_EXPECT_FALSE( noBox.readState( noBoxReader ) ); // 박스 정원을 넘는다
    MonsterStorage truncatedStorage;
    truncatedStorage.initialize( 3 );
    Archive storageCut( storageBytes.data(), storageBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedStorage.readState( storageCut ) );
    SW_EXPECT_TRUE( truncatedStorage.getParty().empty() );

    MonsterBattle battle;
    battle.initialize( &world._catalog, &world._chart, 7 );
    battle.start( { world.makeMonster( "squirtle", 30, { "tackle", "watergun", "raindance" } ), world.makeMonster( "bulbasaur", 30, { "tackle" } ) },
                  { world.makeMonster( "pikachu", 30, { "thundershock", "growl" } ) }, false );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 2 ) ) );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kFoeSide, MonsterAction::makeMove( 1 ) ) );
    battle.resolveRound();
    SW_ASSERT_TRUE( battle.getOutcome() == MonsterBattleOutcome::Ongoing );
    SW_EXPECT_TRUE( battle.getWeather() == hashed_string( "Rain" ) );
    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( 1 ) ) ); // 둔 행동도 싣는다

    const vector<uint8> battleBytes = captureMonsterBytes( battle );
    MonsterBattle       restoredBattle;
    restoredBattle.initialize( &world._catalog, &world._chart, 999 );
    Archive battleReader( battleBytes.data(), battleBytes.size() );
    SW_ASSERT_TRUE( restoredBattle.readState( battleReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, battleReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureMonsterBytes( restoredBattle ) == battleBytes );
    SW_EXPECT_EQUAL( battle.getStage( MonsterBattle::kPlayerSide, MonsterStat::Attack ), restoredBattle.getStage( MonsterBattle::kPlayerSide, MonsterStat::Attack ) );
    SW_EXPECT_EQUAL( battle.getWeatherTurns(), restoredBattle.getWeatherTurns() );

    SW_ASSERT_TRUE( battle.setAction( MonsterBattle::kFoeSide, MonsterAction::makeMove( 0 ) ) );
    SW_ASSERT_TRUE( restoredBattle.setAction( MonsterBattle::kFoeSide, MonsterAction::makeMove( 0 ) ) );
    battle.resolveRound();
    restoredBattle.resolveRound();
    battle.resolveRound();
    restoredBattle.resolveRound();
    SW_EXPECT_TRUE( captureMonsterBytes( battle ) == captureMonsterBytes( restoredBattle ) );

    MonsterBattle truncatedBattle;
    truncatedBattle.initialize( &world._catalog, &world._chart, 999 );
    Archive battleCut( battleBytes.data(), battleBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedBattle.readState( battleCut ) );
    SW_EXPECT_TRUE( truncatedBattle.getParty( MonsterBattle::kPlayerSide ).empty() );
}
