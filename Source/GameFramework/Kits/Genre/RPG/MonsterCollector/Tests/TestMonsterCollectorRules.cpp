#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterBattle.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterCollectorCatalog.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterInstance.h"
#include "GameFramework/Runtime/Character/Combat/Damage/ElementChart.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

#include <algorithm>

// 몬스터 수집 키트의 규칙: 능력치 · 경험치 공식, 개체 성장 · 진화 · 보관함, 전투(피해 · 포획 · 순서 · 상태이상 · 능력 변화 · 날씨 · 교체 · 승패), 카탈로그 오류, 상태 바이트의 범위 검사.
// 개체값 0 · 성격 없음인 레벨 50 개체는 기본 능력치 50 이면 HP 110, 나머지 55 이다. 명중 0 은 반드시 맞는다. 급소는 난수라 전투의 피해는 범위로 본다. 키트 GF_MonsterCollector(nogpu).

namespace
{
    constexpr const utf8* kMonsterXML = R"(<MonsterCollectorCatalog>
  <Move id="tackle" name="Tackle" type="Normal" power="40" accuracy="0" pp="2"/>
  <Move id="strike" name="Strike" type="Normal" power="40" accuracy="0" pp="30"/>
  <Move id="ember" name="Ember" type="Fire" category="Special" power="40" accuracy="0" pp="25"/>
  <Move id="quick" name="Quick" type="Normal" power="10" accuracy="0" priority="1" pp="30"/>
  <Move id="haunt" name="Haunt" type="Ghost" power="40" accuracy="0"/>
  <Move id="growl" name="Growl" type="Normal" category="Status" stat="Attack" stages="-1" target="Foe" accuracy="0" pp="30"/>
  <Move id="swords" name="Swords" type="Normal" category="Status" stat="Attack" stages="2" target="Self" pp="30"/>
  <Move id="toxin" name="Toxin" type="Normal" category="Status" status="Toxic" statusChance="100" accuracy="0"/>
  <Move id="burnup" name="Burnup" type="Fire" category="Status" status="Burn" statusChance="100" accuracy="0"/>
  <Move id="rainfall" name="Rainfall" type="Water" category="Status" weather="Rain"/>
  <Nature id="Adamant" up="Attack" down="SpecialAttack"/>
  <Nature id="Plain"/>
  <Weather id="Rain" boost="Water" weaken="Fire" turns="2"/>
  <Weather id="Sand" chip="16" immune="Rock" turns="5"/>
  <StatusImmunity status="Burn" types="Fire"/>
  <Species id="flamo" name="Flamo" types="Fire" stats="50 50 50 50 50 50" evYield="0 2 0 0 0 1" catchRate="255" baseExp="70">
    <Learn level="1" move="strike"/><Learn level="3" move="growl"/><Evolve to="flamor" level="3"/><Evolve to="flamox" item="stone"/>
  </Species>
  <Species id="flamor" name="Flamor" types="Fire" stats="80 80 80 80 80 80"/>
  <Species id="flamox" name="Flamox" types="Fire,Rock" stats="60 60 60 60 60 60"/>
  <Species id="pal" name="Pal" types="Normal" stats="50 50 50 50 50 50"><Evolve to="flamor" friendship="220"/></Species>
  <Species id="normie" name="Normie" types="Normal" stats="50 50 50 50 50 50" evYield="0 2 0 0 0 1" baseExp="70">
    <Learn level="5" move="tackle"/><Learn level="1" move="strike"/><Learn level="1" move="ember"/><Learn level="1" move="growl"/><Learn level="1" move="swords"/><Learn level="1" move="quick"/>
  </Species>
  <Species id="swift" name="Swift" types="Normal" stats="50 50 50 50 50 200"/>
  <Species id="rocky" name="Rocky" types="Rock" stats="50 50 50 50 50 50"/>
  <Encounter id="night" area="route1" time="Night,Dusk"><Slot species="flamo" min="2" max="4" weight="30"/></Encounter>
  <Encounter id="any" area="cave"><Slot species="normie" min="5" max="5" weight="1"/><Slot species="rocky" min="5" max="5" weight="0"/></Encounter>
</MonsterCollectorCatalog>)";

    /** @brief 상태 바이트의 비교입니다. */
    struct MonsterTestUtil
    {
        template <typename T>
        static bool isSameState( const T& lhs, const T& rhs )
        {
            sw::Archive left;
            sw::Archive right;
            lhs.writeState( left );
            rhs.writeState( right );
            return left.getSize() == right.getSize() && std::equal( left.getData(), left.getData() + left.getSize(), right.getData() );
        }
    };

    /** @brief 전투 상태 바이트를 손으로 씁니다. 플레이어 편의 칸만 망칩니다(같은 규칙을 두 편에 쓴다). */
    struct MonsterForgedBattle
    {
        sw::vector<sw::MonsterInstance> _listPlayer{};
        sw::MonsterInstance             _foe{};
        sw::MonsterInstance             _captured{};
        sw::hashed_string               _weatherID{};
        float32                         _ballMultiplier{ 1.0f };
        int32                           _stage{ 0 };
        int32                           _actionIndex{ 0 };
        int32                           _activeIndex{ 0 };
        int32                           _weatherTurns{ 0 };
        int32                           _escapeAttempts{ 0 };
        uint8                           _actionKind{ 0 };
        uint8                           _needsSwitch{ 0 };
        uint8                           _result{ 0 };
        uint8                           _wild{ 1 };

        void write( sw::Archive& written ) const
        {
            sw::TurnOrder  order;
            sw::GameRandom random;
            random.setSeed( 3 );
            written << static_cast<uint32>( _listPlayer.size() );
            for ( const sw::MonsterInstance& monster : _listPlayer )
            {
                monster.writeState( written );
            }
            for ( int32 stat = 0; stat < sw::kMonsterStatCount; ++stat )
            {
                written << ( stat == 0 ? _stage : 0 );
            }
            written << _ballMultiplier;
            written << _actionIndex;
            written << _actionKind;
            written << _activeIndex;
            written << _needsSwitch;

            written << static_cast<uint32>( 1 );
            _foe.writeState( written );
            for ( int32 stat = 0; stat < sw::kMonsterStatCount; ++stat )
            {
                written << static_cast<int32>( 0 );
            }
            written << 1.0f;
            written << static_cast<int32>( 0 );
            written << static_cast<uint8>( 0 );
            written << static_cast<int32>( 0 );
            written << static_cast<uint8>( 0 );

            order.writeState( written );
            sw::StateIO::writeRandom( written, random );
            _captured.writeState( written );
            sw::StateIO::writeName( written, _weatherID );
            written << _weatherTurns;
            written << _escapeAttempts;
            written << _result;
            written << _wild;
        }
    };

    /** @brief 카탈로그, 상성표, 전투 하나입니다. */
    struct MonsterCollectorRulesTest
    {
        sw::MonsterCollectorCatalog        _catalog{};
        sw::ElementChart                   _chart{};
        sw::MonsterBattle                  _battle{};
        sw::vector<sw::MonsterBattleEvent> _listEvent{};
        sw::vector<sw::MonsterGrowthEvent> _listGrowth{};

        MonsterCollectorRulesTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kMonsterXML, "MonsterTest" ) );
            _chart.addElement( "Normal" );
            _chart.addElement( "Fire" );
            _chart.addElement( "Water" );
            _chart.addElement( "Ghost" );
            _chart.addElement( "Rock" );
            SW_REQUIRE_TRUE( _chart.setMultiplier( "Water", "Fire", 2.0f ) );
            SW_REQUIRE_TRUE( _chart.setMultiplier( "Ghost", "Normal", 0.0f ) );
            _battle.initialize( &_catalog, &_chart, 7 );
        }

        /** @brief 개체값 0 · 성격 없음인 개체를 만듭니다(HP 가득). 기술은 적은 순서대로 슬롯에 들어갑니다. */
        sw::MonsterInstance make( const sw::hashed_string& speciesID, int32 level, const sw::hashed_string& moveA = sw::hashed_string{}, const sw::hashed_string& moveB = sw::hashed_string{} )
        {
            const sw::MonsterSpeciesDef* pSpecies = _catalog.findSpecies( speciesID );
            sw::MonsterInstance          monster;
            monster._speciesID = speciesID;
            monster._level     = level;
            monster._nickname  = pSpecies != nullptr ? pSpecies->_name : sw::string{};
            monster._exp       = pSpecies != nullptr ? sw::MonsterCollectorCatalog::computeTotalExp( pSpecies->_expGroup, level ) : 0;
            sw::MonsterInstance::recomputeStats( _catalog, monster );
            if ( moveA.empty() == false )
                SW_EXPECT_TRUE( sw::MonsterInstance::learnMove( _catalog, monster, moveA ) );
            if ( moveB.empty() == false )
                SW_EXPECT_TRUE( sw::MonsterInstance::learnMove( _catalog, monster, moveB ) );
            return monster;
        }

        void open( const sw::MonsterInstance& player, const sw::MonsterInstance& foe, bool bWild = false )
        {
            sw::vector<sw::MonsterInstance> listPlayer;
            sw::vector<sw::MonsterInstance> listFoe;
            listPlayer.push_back( player );
            listFoe.push_back( foe );
            _battle.start( listPlayer, listFoe, bWild );
        }

        int32 countEvents( sw::MonsterBattleEvent::Kind kind )
        {
            _battle.pollEvents( _listEvent );
            return sw::VectorUtil::countBy<&sw::MonsterBattleEvent::_kind>( _listEvent, kind );
        }

        /** @brief 지금까지의 이벤트에서 @p kind 가 처음 나온 값입니다. 없으면 비어 있습니다. */
        sw::optional<int32> findFirstValue( sw::MonsterBattleEvent::Kind kind )
        {
            _battle.pollEvents( _listEvent );
            const sw::MonsterBattleEvent* pEvent = sw::VectorUtil::findBy<&sw::MonsterBattleEvent::_kind>( _listEvent, kind );
            return pEvent != nullptr ? sw::optional<int32>{ pEvent->_value } : sw::nullopt;
        }

        /** @brief 이번에 처음 기술을 쓴 편입니다. 이벤트 목록을 비웁니다. */
        int32 takeFirstActor()
        {
            _battle.pollEvents( _listEvent );
            const sw::MonsterBattleEvent* pEvent = sw::VectorUtil::findBy<&sw::MonsterBattleEvent::_kind>( _listEvent, sw::MonsterBattleEvent::Kind::MoveUsed );
            const int32                   side   = pEvent != nullptr ? pEvent->_side : -1;
            _listEvent.clear();
            return side;
        }

        int32 countGrowth( sw::MonsterGrowthEvent::Kind kind ) const { return sw::VectorUtil::countBy<&sw::MonsterGrowthEvent::_kind>( _listGrowth, kind ); }

        /** @brief 맞는 전투 상태(플레이어 하나가 strike, 상대 하나, 야생)입니다. */
        MonsterForgedBattle makeForged()
        {
            MonsterForgedBattle forged;
            forged._listPlayer.push_back( make( "normie", 50, "strike" ) );
            forged._foe = make( "normie", 50, "strike" );
            return forged;
        }

        bool readForged( const MonsterForgedBattle& forged )
        {
            sw::Archive written;
            forged.write( written );
            sw::Archive reading( written.getData(), written.getSize() );
            return _battle.readState( reading );
        }

        /** @brief 개체 바이트를 왕복합니다. */
        static bool readsBack( const sw::MonsterInstance& monster )
        {
            sw::Archive written;
            monster.writeState( written );
            sw::Archive         reading( written.getData(), written.getSize() );
            sw::MonsterInstance restored;
            return restored.readState( reading );
        }
    };
} // namespace

/**
 * @brief [MonsterCollectorRulesTest] 능력치 공식: HP 와 나머지, 성격, 개체값 · 노력치 · 레벨의 범위 자르기
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, StatFormula )
{
    using Rules = sw::MonsterInstance;
    SW_EXPECT_EQUAL( 110, Rules::computeStat( sw::MonsterStat::HP, 50, 0, 0, 50, 100 ) );
    SW_EXPECT_EQUAL( 55, Rules::computeStat( sw::MonsterStat::Attack, 50, 0, 0, 50, 100 ) );
    SW_EXPECT_EQUAL( 60, Rules::computeStat( sw::MonsterStat::Attack, 50, 0, 0, 50, 110 ) );
    SW_EXPECT_EQUAL( 49, Rules::computeStat( sw::MonsterStat::Attack, 50, 0, 0, 50, 90 ) );
    SW_EXPECT_EQUAL( 70, Rules::computeStat( sw::MonsterStat::Attack, 50, 99, 0, 50, 100 ) );  // 개체값 31 까지: 131 × 50 / 100 + 5
    SW_EXPECT_EQUAL( 86, Rules::computeStat( sw::MonsterStat::Attack, 50, 0, 999, 50, 100 ) ); // 노력치 252 까지: 163 × 50 / 100 + 5
    SW_EXPECT_EQUAL( 6, Rules::computeStat( sw::MonsterStat::Attack, 50, 0, 0, 0, 100 ) );     // 레벨 1 까지

    const sw::MonsterNatureDef* pAdamant = _catalog.findNature( "Adamant" );
    SW_REQUIRE_TRUE( pAdamant != nullptr );
    SW_EXPECT_EQUAL( 110, pAdamant->computePercent( sw::MonsterStat::Attack ) );
    SW_EXPECT_EQUAL( 90, pAdamant->computePercent( sw::MonsterStat::SpecialAttack ) );
    SW_EXPECT_EQUAL( 100, pAdamant->computePercent( sw::MonsterStat::Speed ) );
    SW_EXPECT_EQUAL( 100, pAdamant->computePercent( sw::MonsterStat::HP ) );
    SW_EXPECT_EQUAL( 100, _catalog.findNature( "Plain" )->computePercent( sw::MonsterStat::Attack ) );

    sw::MonsterInstance strong = make( "normie", 50 );
    strong._natureID           = "Adamant";
    sw::MonsterInstance::recomputeStats( _catalog, strong );
    SW_EXPECT_EQUAL( 60, strong.getStat( sw::MonsterStat::Attack ) );
    SW_EXPECT_EQUAL( 49, strong.getStat( sw::MonsterStat::SpecialAttack ) );
    SW_EXPECT_EQUAL( 110, strong.getMaxHP() );
}

/**
 * @brief [MonsterCollectorRulesTest] 경험치 그룹: 레벨 1 은 0, 빠름 4n³/5 · 보통 n³ · 느림 5n³/4, 최고 레벨에서 멈춘다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, ExpGroups )
{
    using Catalog = sw::MonsterCollectorCatalog;
    SW_EXPECT_EQUAL( 0, Catalog::computeTotalExp( sw::MonsterExpGroup::Medium, 1 ) );
    SW_EXPECT_EQUAL( 800, Catalog::computeTotalExp( sw::MonsterExpGroup::Fast, 10 ) );
    SW_EXPECT_EQUAL( 1000, Catalog::computeTotalExp( sw::MonsterExpGroup::Medium, 10 ) );
    SW_EXPECT_EQUAL( 1250, Catalog::computeTotalExp( sw::MonsterExpGroup::Slow, 10 ) );
    SW_EXPECT_EQUAL( 1000000, Catalog::computeTotalExp( sw::MonsterExpGroup::Medium, 500 ) );

    SW_EXPECT_EQUAL( 9, Catalog::computeLevelForExp( sw::MonsterExpGroup::Medium, 999 ) );
    SW_EXPECT_EQUAL( 10, Catalog::computeLevelForExp( sw::MonsterExpGroup::Medium, 1000 ) );
    SW_EXPECT_EQUAL( 1, Catalog::computeLevelForExp( sw::MonsterExpGroup::Medium, 0 ) );
    SW_EXPECT_EQUAL( Catalog::kMaxLevel, Catalog::computeLevelForExp( sw::MonsterExpGroup::Medium, 999999999 ) );

    const sw::MonsterSpeciesDef* pNormie = _catalog.findSpecies( "normie" );
    SW_EXPECT_EQUAL( 500, sw::MonsterInstance::computeExpYield( *pNormie, 50, false ) );
    SW_EXPECT_EQUAL( 750, sw::MonsterInstance::computeExpYield( *pNormie, 50, true ) );
    SW_EXPECT_EQUAL( 10, sw::MonsterInstance::computeExpYield( *pNormie, 0, false ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 새 개체: 마지막 네 기술(배운 순서 유지), HP 가득, 같은 시드면 같은 개체. 모르는 종 · 범위 밖 레벨은 빈 개체
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, CreateMonster )
{
    sw::GameRandom random;
    random.setSeed( 9 );
    const sw::MonsterInstance monster = sw::MonsterInstance::createMonster( _catalog, "normie", 1, random );
    SW_EXPECT_TRUE( monster._arrMove[0]._moveID == sw::hashed_string( "ember" ) );
    SW_EXPECT_TRUE( monster._arrMove[3]._moveID == sw::hashed_string( "quick" ) );
    SW_EXPECT_EQUAL( 25, monster._arrMove[0]._pp );
    SW_EXPECT_EQUAL( monster.getMaxHP(), monster._hp );
    SW_EXPECT_EQUAL( 0, monster._exp );
    SW_EXPECT_TRUE( _catalog.findNature( monster._natureID ) != nullptr );
    for ( const int32 iv : monster._arrIv )
    {
        SW_EXPECT_TRUE( 0 <= iv && iv <= sw::MonsterInstance::kMaxIv );
    }

    // 레벨 5 의 기술은 XML 에서 앞에 적혔어도 카탈로그가 정렬해 마지막에 온다.
    const sw::MonsterInstance grown = sw::MonsterInstance::createMonster( _catalog, "normie", 5, random );
    SW_EXPECT_TRUE( grown._arrMove[0]._moveID == sw::hashed_string( "growl" ) );
    SW_EXPECT_TRUE( grown._arrMove[3]._moveID == sw::hashed_string( "tackle" ) );
    SW_EXPECT_EQUAL( 125, grown._exp );

    sw::GameRandom randomA;
    sw::GameRandom randomB;
    randomA.setSeed( 4 );
    randomB.setSeed( 4 );
    SW_EXPECT_TRUE( MonsterTestUtil::isSameState( sw::MonsterInstance::createMonster( _catalog, "normie", 7, randomA ), sw::MonsterInstance::createMonster( _catalog, "normie", 7, randomB ) ) );

    SW_EXPECT_TRUE( sw::MonsterInstance::createMonster( _catalog, "nope", 5, random )._speciesID.empty() );
    SW_EXPECT_LOG( "MonsterInstance.MonsterRulesRefusedLevelMust" );
    SW_EXPECT_TRUE( sw::MonsterInstance::createMonster( _catalog, "normie", 0, random )._speciesID.empty() );
    SW_EXPECT_TRUE( sw::MonsterInstance::createMonster( _catalog, "normie", 101, random )._speciesID.empty() );
}

/**
 * @brief [MonsterCollectorRulesTest] 경험치: 레벨 · 친밀도 · 능력치 · HP · 기술 · 진화 알림, 최고 레벨 상한, 0 이하 · 모르는 종 거절
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, GainExp )
{
    sw::MonsterInstance monster = make( "flamo", 2, "strike" );
    SW_EXPECT_EQUAL( 8, monster._exp );
    SW_EXPECT_EQUAL( 14, monster.getMaxHP() );
    SW_EXPECT_EQUAL( 0, sw::MonsterInstance::gainExp( _catalog, monster, 0, _listGrowth ) );
    SW_EXPECT_EQUAL( 0, sw::MonsterInstance::gainExp( _catalog, monster, -5, _listGrowth ) );
    SW_EXPECT_TRUE( _listGrowth.empty() );

    SW_EXPECT_EQUAL( 1, sw::MonsterInstance::gainExp( _catalog, monster, 19, _listGrowth ) ); // 27 = 3³
    SW_EXPECT_EQUAL( 3, monster._level );
    SW_EXPECT_EQUAL( 16, monster.getMaxHP() );
    SW_EXPECT_EQUAL( 16, monster._hp );
    SW_EXPECT_EQUAL( 75, monster._friendship );
    SW_EXPECT_TRUE( monster.findMoveSlot( "growl" ).has_value() );
    SW_EXPECT_EQUAL( 1, countGrowth( sw::MonsterGrowthEvent::Kind::LevelUp ) );
    SW_EXPECT_EQUAL( 1, countGrowth( sw::MonsterGrowthEvent::Kind::LearnedMove ) );
    SW_EXPECT_EQUAL( 1, countGrowth( sw::MonsterGrowthEvent::Kind::CanEvolve ) );

    // 쓰러진 개체는 HP 가 그대로이고, 슬롯이 차면 막힘 알림만 낸다.
    _listGrowth.clear();
    sw::MonsterInstance full = make( "flamo", 2, "strike", "ember" );
    SW_EXPECT_TRUE( sw::MonsterInstance::learnMove( _catalog, full, "quick" ) );
    SW_EXPECT_TRUE( sw::MonsterInstance::learnMove( _catalog, full, "swords" ) );
    full._hp = 0;
    SW_EXPECT_EQUAL( 1, sw::MonsterInstance::gainExp( _catalog, full, 19, _listGrowth ) );
    SW_EXPECT_EQUAL( 0, full._hp );
    SW_EXPECT_EQUAL( 1, countGrowth( sw::MonsterGrowthEvent::Kind::MoveLearnBlocked ) );
    SW_EXPECT_FALSE( full.findMoveSlot( "growl" ).has_value() );

    _listGrowth.clear();
    sw::MonsterInstance veteran = make( "normie", 99 );
    SW_EXPECT_EQUAL( 1, sw::MonsterInstance::gainExp( _catalog, veteran, 99999999, _listGrowth ) );
    SW_EXPECT_EQUAL( sw::MonsterCollectorCatalog::kMaxLevel, veteran._level );
    SW_EXPECT_EQUAL( 1000000, veteran._exp );
    SW_EXPECT_EQUAL( 0, sw::MonsterInstance::gainExp( _catalog, veteran, 100, _listGrowth ) );

    sw::MonsterInstance unknown;
    unknown._speciesID = "nope";
    SW_EXPECT_EQUAL( 0, sw::MonsterInstance::gainExp( _catalog, unknown, 100, _listGrowth ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 기술 배우기: 빈 슬롯, 찬 슬롯의 교체 번호, 이미 아는 기술 · 모르는 기술 거절, 교체하면 PP 가 찬다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, LearnMove )
{
    sw::MonsterInstance monster = make( "normie", 50, "strike", "ember" );
    SW_EXPECT_EQUAL( 2, monster.countMoves() );
    SW_EXPECT_FALSE( sw::MonsterInstance::learnMove( _catalog, monster, "strike" ) );
    SW_EXPECT_FALSE( sw::MonsterInstance::learnMove( _catalog, monster, "nope" ) );
    SW_EXPECT_TRUE( sw::MonsterInstance::learnMove( _catalog, monster, "growl", 3 ) ); // 빈 슬롯이 있으면 교체 번호는 보지 않는다
    SW_EXPECT_TRUE( monster.findMoveSlot( "growl" ) == 2 );
    SW_EXPECT_TRUE( sw::MonsterInstance::learnMove( _catalog, monster, "swords" ) );

    SW_EXPECT_FALSE( sw::MonsterInstance::learnMove( _catalog, monster, "quick" ) );
    SW_EXPECT_FALSE( sw::MonsterInstance::learnMove( _catalog, monster, "quick", 4 ) );
    SW_EXPECT_TRUE( sw::MonsterInstance::learnMove( _catalog, monster, "quick", 1 ) );
    SW_EXPECT_FALSE( monster.findMoveSlot( "ember" ).has_value() );
    SW_EXPECT_EQUAL( 30, monster._arrMove[1]._pp );
    SW_EXPECT_FALSE( monster.findMoveSlot( sw::hashed_string{} ).has_value() );
}

/**
 * @brief [MonsterCollectorRulesTest] 노력치: 능력치 하나 252, 합 510 에서 멈추고 능력치를 다시 계산한다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, EffortValueCaps )
{
    const sw::MonsterSpeciesDef& defeated = *_catalog.findSpecies( "normie" ); // 공격 2, 스피드 1
    sw::MonsterInstance          monster  = make( "normie", 50 );

    monster._arrEv[static_cast<size_t>( sw::MonsterStat::Attack )] = 251;
    sw::MonsterInstance::addEffortValues( _catalog, monster, defeated );
    SW_EXPECT_EQUAL( 252, monster._arrEv[static_cast<size_t>( sw::MonsterStat::Attack )] );
    SW_EXPECT_EQUAL( 1, monster._arrEv[static_cast<size_t>( sw::MonsterStat::Speed )] );
    SW_EXPECT_EQUAL( 86, monster.getStat( sw::MonsterStat::Attack ) );

    monster._arrEv[static_cast<size_t>( sw::MonsterStat::Defense )] = 252;
    monster._arrEv[static_cast<size_t>( sw::MonsterStat::Speed )]   = 5; // 합 509
    sw::MonsterInstance::addEffortValues( _catalog, monster, defeated );
    SW_EXPECT_EQUAL( 252, monster._arrEv[static_cast<size_t>( sw::MonsterStat::Attack )] );
    SW_EXPECT_EQUAL( 6, monster._arrEv[static_cast<size_t>( sw::MonsterStat::Speed )] );
    sw::MonsterInstance::addEffortValues( _catalog, monster, defeated );
    SW_EXPECT_EQUAL( 6, monster._arrEv[static_cast<size_t>( sw::MonsterStat::Speed )] );
}

/**
 * @brief [MonsterCollectorRulesTest] 진화: 레벨 · 친밀도 · 아이템 분기, 별명을 따로 붙이지 않았으면 이름도 바뀌고, 쓰러진 개체는 그대로다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, Evolution )
{
    sw::MonsterInstance monster = make( "flamo", 2 );
    SW_EXPECT_TRUE( sw::MonsterInstance::findEvolution( _catalog, monster, sw::hashed_string{} ).empty() );
    monster._level = 3;
    SW_EXPECT_TRUE( sw::MonsterInstance::findEvolution( _catalog, monster, sw::hashed_string{} ) == sw::hashed_string( "flamor" ) );
    SW_EXPECT_TRUE( sw::MonsterInstance::findEvolution( _catalog, monster, "stone" ) == sw::hashed_string( "flamox" ) );
    SW_EXPECT_TRUE( sw::MonsterInstance::findEvolution( _catalog, monster, "moon" ).empty() );

    sw::MonsterInstance pal = make( "pal", 5 );
    SW_EXPECT_TRUE( sw::MonsterInstance::findEvolution( _catalog, pal, sw::hashed_string{} ).empty() );
    pal._friendship = 220;
    SW_EXPECT_TRUE( sw::MonsterInstance::findEvolution( _catalog, pal, sw::hashed_string{} ) == sw::hashed_string( "flamor" ) );

    sw::MonsterInstance adult = make( "flamo", 50 );
    SW_EXPECT_FALSE( sw::MonsterInstance::evolve( _catalog, adult, "nope" ) );
    SW_EXPECT_TRUE( sw::MonsterInstance::evolve( _catalog, adult, "flamor" ) );
    SW_EXPECT_TRUE( adult._speciesID == sw::hashed_string( "flamor" ) );
    SW_EXPECT_TRUE( adult._nickname == "Flamor" );
    SW_EXPECT_EQUAL( 85, adult.getStat( sw::MonsterStat::Attack ) ); // 160 × 50 / 100 + 5
    SW_EXPECT_EQUAL( 140, adult.getMaxHP() );
    SW_EXPECT_EQUAL( 140, adult._hp ); // 오른 최대치만큼 HP 도 오른다

    sw::MonsterInstance named = make( "flamo", 50 );
    named._nickname           = "Sparky";
    named._hp                 = 0;
    SW_EXPECT_TRUE( sw::MonsterInstance::evolve( _catalog, named, "flamor" ) );
    SW_EXPECT_TRUE( named._nickname == "Sparky" );
    SW_EXPECT_EQUAL( 0, named._hp );

    sw::MonsterInstance tired = make( "normie", 50, "strike" );
    tired._hp                 = 1;
    tired._status             = sw::MonsterStatus::Burn;
    tired._arrMove[0]._pp     = 0;
    sw::MonsterInstance::restore( tired );
    SW_EXPECT_EQUAL( 110, tired._hp );
    SW_EXPECT_EQUAL( sw::MonsterStatus::None, tired._status );
    SW_EXPECT_EQUAL( 30, tired._arrMove[0]._pp );
}

/**
 * @brief [MonsterCollectorRulesTest] 개체 상태: 왕복하면 같고 깨진 바이트는 거절한다.
 *        결함: 값을 거의 보지 않았다. 음수 · 최대를 넘는 HP, 최대를 넘는 PP(`restore` 를 지나면 도로 낮아진다), 범위 밖 개체값 · 노력치, 능력치 0, 친밀도, 상태이상 턴을 받았다.
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, InstanceStateRange )
{
    sw::MonsterInstance source = make( "normie", 50, "strike" );
    source._nickname           = "Pat";
    source._status             = sw::MonsterStatus::Toxic;
    source._statusTurns        = 4;
    source._arrEv[0]           = 8;
    sw::Archive written;
    source.writeState( written );
    SW_EXPECT_TRUE( written.getSize() >= sw::MonsterInstance::kStateMinBytes );
    sw::Archive         reading( written.getData(), written.getSize() );
    sw::MonsterInstance restored;
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( MonsterTestUtil::isSameState( source, restored ) );
    SW_EXPECT_TRUE( restored._nickname == "Pat" );

    sw::MonsterInstance keeps = make( "normie", 50 );
    const auto          bad   = [this]( auto mutate )
    {
        sw::MonsterInstance monster = make( "normie", 50, "strike" );
        mutate( monster );
        return readsBack( monster ) == false;
    };
    SW_EXPECT_FALSE( bad( []( sw::MonsterInstance& ) {} ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._hp = -1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._hp = monster.getMaxHP() + 1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._arrMove[0]._pp = -1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._arrMove[0]._pp = monster._arrMove[0]._ppMax + 1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._arrIv[1] = -1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._arrIv[1] = sw::MonsterInstance::kMaxIv + 1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._arrEv[1] = -1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._arrEv[1] = sw::MonsterInstance::kMaxEvPerStat + 1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    {
        monster._arrEv[0] = 252;
        monster._arrEv[1] = 252;
        monster._arrEv[2] = 7; // 합 511
    } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._arrStat[1] = 0; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._level = 0; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._level = sw::MonsterCollectorCatalog::kMaxLevel + 1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._friendship = -1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._friendship = sw::MonsterInstance::kMaxFriendship + 1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._statusTurns = -1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._statusTurns = sw::MonsterInstance::kMaxStatusTurns + 1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._exp = -1; } ) );
    SW_EXPECT_TRUE( bad( []( sw::MonsterInstance& monster )
    { monster._status = static_cast<sw::MonsterStatus>( 9 ); } ) );

    SW_EXPECT_TRUE( keeps.isKnownTo( _catalog ) );
    keeps._natureID = "nope";
    SW_EXPECT_FALSE( keeps.isKnownTo( _catalog ) );
    keeps._natureID           = sw::hashed_string{};
    keeps._arrMove[2]._moveID = "nope";
    SW_EXPECT_FALSE( keeps.isKnownTo( _catalog ) );
    keeps._arrMove[2]._moveID = sw::hashed_string{};
    keeps._speciesID          = "nope";
    SW_EXPECT_FALSE( keeps.isKnownTo( _catalog ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 보관함: 파티 6 다음 박스, 정원, 마지막 싸울 개체는 맡기지 못한다, 범위 밖 번호, 회복.
 *        결함: `createMonster` 가 거절한 빈 개체를 그대로 넣었다 - 거절한다.
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, Storage )
{
    sw::MonsterStorage storage;
    SW_EXPECT_LOG( "MonsterInstance.MonsterStorageRefusedBoxCapacity" );
    SW_EXPECT_FALSE( storage.initialize( &_catalog, -1 ) );
    SW_EXPECT_FALSE( storage.initialize( nullptr, 1 ) );
    SW_EXPECT_EQUAL( sw::MonsterStoragePlace::Refused, storage.add( make( "normie", 5 ) ) ); // 시작하지 않았다
    SW_REQUIRE_TRUE( storage.initialize( &_catalog, 1 ) );
    SW_EXPECT_EQUAL( 1, storage.getBoxCapacity() );
    SW_EXPECT_FALSE( storage.hasUsableMonster() );

    SW_EXPECT_EQUAL( sw::MonsterStoragePlace::Refused, storage.add( sw::MonsterInstance{} ) );
    for ( int32 index = 0; index < sw::MonsterStorage::kPartySize; ++index )
    {
        SW_EXPECT_EQUAL( sw::MonsterStoragePlace::Party, storage.add( make( "normie", 5 + index ) ) );
    }
    SW_EXPECT_EQUAL( sw::MonsterStoragePlace::Box, storage.add( make( "rocky", 5 ) ) );
    SW_EXPECT_EQUAL( sw::MonsterStoragePlace::Full, storage.add( make( "rocky", 6 ) ) );
    SW_EXPECT_TRUE( storage.hasUsableMonster() );

    SW_EXPECT_FALSE( storage.depositToBox( 0 ) );    // 박스가 찼다
    SW_EXPECT_FALSE( storage.withdrawFromBox( 0 ) ); // 파티가 찼다
    SW_EXPECT_FALSE( storage.withdrawFromBox( -1 ) );
    SW_EXPECT_FALSE( storage.depositToBox( 99 ) );
    SW_EXPECT_FALSE( storage.swapPartyOrder( 0, 6 ) );
    SW_EXPECT_FALSE( storage.swapPartyOrder( -1, 0 ) );
    SW_EXPECT_TRUE( storage.swapPartyOrder( 0, 5 ) );
    SW_EXPECT_EQUAL( 10, storage.getParty()[0]._level );

    sw::MonsterStorage last;
    SW_REQUIRE_TRUE( last.initialize( &_catalog, 5 ) );
    (void)last.add( make( "normie", 5 ) );
    (void)last.add( make( "rocky", 5 ) );
    last.getParty()[1]._hp = 0;
    SW_EXPECT_FALSE( last.depositToBox( 0 ) ); // 쓰러진 개체만 남는다
    SW_EXPECT_TRUE( last.depositToBox( 1 ) );
    SW_EXPECT_EQUAL( 1u, last.getBox().size() );
    SW_EXPECT_TRUE( last.withdrawFromBox( 0 ) );
    last.getParty()[0]._hp = 1;
    last.restoreParty();
    SW_EXPECT_EQUAL( last.getParty()[0].getMaxHP(), last.getParty()[0]._hp );
    SW_EXPECT_EQUAL( last.getParty()[1].getMaxHP(), last.getParty()[1]._hp );
}

/**
 * @brief [MonsterCollectorRulesTest] 보관함 상태: 왕복, 정원 초과 거절.
 *        결함: 개체의 id 를 "호출하는 쪽이 본다" 고 적었지만 보관함을 부르는 쪽은 카탈로그가 없거나 볼 곳이 없었다 - 모르는 종 · 기술 · 성격을 그대로 읽었다. 보관함이 카탈로그를 빌려 거절한다.
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, StorageState )
{
    sw::MonsterStorage source;
    SW_REQUIRE_TRUE( source.initialize( &_catalog, 8 ) );
    for ( int32 index = 0; index < 8; ++index )
    {
        SW_EXPECT_TRUE( source.add( make( "normie", 5 + index, "strike" ) ) != sw::MonsterStoragePlace::Full );
    }
    sw::Archive written;
    source.writeState( written );

    sw::Archive        reading( written.getData(), written.getSize() );
    sw::MonsterStorage restored;
    SW_REQUIRE_TRUE( restored.initialize( &_catalog, 8 ) );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( MonsterTestUtil::isSameState( source, restored ) );
    SW_EXPECT_EQUAL( 6u, restored.getParty().size() );
    SW_EXPECT_EQUAL( 2u, restored.getBox().size() );

    sw::Archive        readingSmall( written.getData(), written.getSize() );
    sw::MonsterStorage small;
    SW_REQUIRE_TRUE( small.initialize( &_catalog, 1 ) );
    SW_EXPECT_FALSE( small.readState( readingSmall ) ); // 박스 정원 1
    SW_EXPECT_TRUE( small.getParty().empty() );

    sw::Archive        readingLoose( written.getData(), written.getSize() );
    sw::MonsterStorage loose;
    SW_EXPECT_FALSE( loose.readState( readingLoose ) ); // 카탈로그가 없다

    const auto refuses = [this]( const sw::MonsterInstance& monster, uint32 partyCount )
    {
        sw::Archive forged;
        forged << partyCount;
        for ( uint32 index = 0; index < partyCount; ++index )
        {
            monster.writeState( forged );
        }
        forged << static_cast<uint32>( 0 );
        sw::Archive        readingForged( forged.getData(), forged.getSize() );
        sw::MonsterStorage target;
        return target.initialize( &_catalog, 8 ) && target.readState( readingForged ) == false && target.getParty().empty();
    };
    const sw::MonsterInstance good = make( "normie", 5, "strike" );
    SW_EXPECT_FALSE( refuses( good, 6 ) );
    SW_EXPECT_TRUE( refuses( good, 7 ) );

    sw::MonsterInstance unknownSpecies = good;
    unknownSpecies._speciesID          = "nope";
    SW_EXPECT_TRUE( refuses( unknownSpecies, 1 ) );
    sw::MonsterInstance unknownMove = good;
    unknownMove._arrMove[1]._moveID = "nope";
    SW_EXPECT_TRUE( refuses( unknownMove, 1 ) );
    sw::MonsterInstance unknownNature = good;
    unknownNature._natureID           = "nope";
    SW_EXPECT_TRUE( refuses( unknownNature, 1 ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 피해 공식: 단계마다 내림, 급소 · 난수 · 자속 · 상성 · 날씨 · 화상, 위력 · 상성 0 이면 0, 아니면 최소 1
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, DamageFormula )
{
    sw::MonsterDamageInput input;
    input._level   = 50;
    input._power   = 40;
    input._attack  = 55;
    input._defense = 55;
    SW_EXPECT_EQUAL( 19, sw::MonsterBattle::computeDamage( input ) ); // (22 × 40 × 55 / 55) / 50 + 2

    const auto with = [input]( auto mutate )
    {
        sw::MonsterDamageInput changed = input;
        mutate( changed );
        return sw::MonsterBattle::computeDamage( changed );
    };
    SW_EXPECT_EQUAL( 28, with( []( sw::MonsterDamageInput& value )
    { value._bCritical = true; } ) );
    SW_EXPECT_EQUAL( 16, with( []( sw::MonsterDamageInput& value )
    { value._randomPercent = 85; } ) );
    SW_EXPECT_EQUAL( 16, with( []( sw::MonsterDamageInput& value )
    { value._randomPercent = 1; } ) );
    SW_EXPECT_EQUAL( 28, with( []( sw::MonsterDamageInput& value )
    { value._bStab = true; } ) );
    SW_EXPECT_EQUAL( 38, with( []( sw::MonsterDamageInput& value )
    { value._typeMultiplier = 2.0f; } ) );
    SW_EXPECT_EQUAL( 9, with( []( sw::MonsterDamageInput& value )
    { value._typeMultiplier = 0.5f; } ) );
    SW_EXPECT_EQUAL( 9, with( []( sw::MonsterDamageInput& value )
    { value._weatherMultiplier = 0.5f; } ) );
    SW_EXPECT_EQUAL( 9, with( []( sw::MonsterDamageInput& value )
    { value._bBurned = true; } ) );
    SW_EXPECT_EQUAL( 0, with( []( sw::MonsterDamageInput& value )
    { value._power = 0; } ) );
    SW_EXPECT_EQUAL( 0, with( []( sw::MonsterDamageInput& value )
    { value._typeMultiplier = 0.0f; } ) );
    SW_EXPECT_EQUAL( 1, with( []( sw::MonsterDamageInput& value )
    {
        value._attack         = 0;
        value._defense        = 9999;
        value._typeMultiplier = 0.25f;
    } ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 능력 변화 배율과 급소 분모, 실효 스피드(능력 변화와 마비), 복합 타입 상성
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, StagesAndTypes )
{
    SW_EXPECT_NEAR( 1.0, sw::MonsterBattle::computeStageMultiplier( 0 ), 1.0e-5 );
    SW_EXPECT_NEAR( 2.0, sw::MonsterBattle::computeStageMultiplier( 2 ), 1.0e-5 );
    SW_EXPECT_NEAR( 0.5, sw::MonsterBattle::computeStageMultiplier( -2 ), 1.0e-5 );
    SW_EXPECT_NEAR( 4.0, sw::MonsterBattle::computeStageMultiplier( 99 ), 1.0e-5 );
    SW_EXPECT_NEAR( 0.25, sw::MonsterBattle::computeStageMultiplier( -99 ), 1.0e-5 );
    SW_EXPECT_EQUAL( 24, sw::MonsterBattle::computeCriticalDivisor( 0 ) );
    SW_EXPECT_EQUAL( 8, sw::MonsterBattle::computeCriticalDivisor( 1 ) );
    SW_EXPECT_EQUAL( 2, sw::MonsterBattle::computeCriticalDivisor( 2 ) );
    SW_EXPECT_EQUAL( 1, sw::MonsterBattle::computeCriticalDivisor( 9 ) );

    const sw::MonsterInstance fire = make( "flamo", 50 );
    SW_EXPECT_NEAR( 2.0, _battle.computeTypeMultiplier( "Water", fire ), 1.0e-5 );
    SW_EXPECT_NEAR( 1.0, _battle.computeTypeMultiplier( "Normal", fire ), 1.0e-5 );
    SW_EXPECT_NEAR( 1.0, _battle.computeTypeMultiplier( sw::hashed_string{}, fire ), 1.0e-5 );
    SW_EXPECT_NEAR( 0.0, _battle.computeTypeMultiplier( "Ghost", make( "normie", 50 ) ), 1.0e-5 );

    sw::MonsterInstance slow = make( "normie", 50 );
    slow._status             = sw::MonsterStatus::Paralysis;
    open( slow, make( "swift", 50 ) );
    SW_EXPECT_NEAR( 27.5, _battle.computeEffectiveSpeed( sw::MonsterBattle::kPlayerSide ), 1.0e-3 );
    SW_EXPECT_NEAR( 205.0, _battle.computeEffectiveSpeed( sw::MonsterBattle::kFoeSide ), 1.0e-3 );
}

/**
 * @brief [MonsterCollectorRulesTest] 기대 피해: 자속 · 날씨 · 화상을 반영하고 분류가 변화이거나 모르는 기술이면 0
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, ExpectedDamage )
{
    const sw::MonsterInstance fire  = make( "flamo", 50 );
    sw::MonsterInstance       plain = make( "normie", 50 );
    open( fire, plain );
    SW_EXPECT_NEAR( 25.9, _battle.computeExpectedDamage( -1, fire, "ember", -1, plain ), 1.0e-2 );    // 19 × 3 / 2 = 28, × 0.925
    SW_EXPECT_NEAR( 17.575, _battle.computeExpectedDamage( -1, fire, "strike", -1, plain ), 1.0e-2 ); // 자속이 아니다: 19 × 0.925
    SW_EXPECT_NEAR( 0.0, _battle.computeExpectedDamage( -1, fire, "growl", -1, plain ), 1.0e-5 );
    SW_EXPECT_NEAR( 0.0, _battle.computeExpectedDamage( -1, fire, "nope", -1, plain ), 1.0e-5 );

    _battle.setWeather( "Rain", 0 );                                                                 // Fire 약화
    SW_EXPECT_NEAR( 12.025, _battle.computeExpectedDamage( -1, fire, "ember", -1, plain ), 1.0e-2 ); // 9 × 3 / 2 = 13, × 0.925

    sw::MonsterInstance burned = make( "flamo", 50 );
    burned._status             = sw::MonsterStatus::Burn;
    _battle.setWeather( sw::hashed_string{}, 0 );
    SW_EXPECT_NEAR( 8.325, _battle.computeExpectedDamage( -1, burned, "strike", -1, plain ), 1.0e-2 ); // 물리는 절반: 9
    SW_EXPECT_NEAR( 25.9, _battle.computeExpectedDamage( -1, burned, "ember", -1, plain ), 1.0e-2 );   // 특수는 그대로
}

/**
 * @brief [MonsterCollectorRulesTest] 포획 공식: a 와 b, 상태 배율, 255 이상이면 반드시 잡히고, 볼 · 포획률 · HP 는 범위로 잘린다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, CaptureFormula )
{
    sw::GameRandom random;
    random.setSeed( 3 );
    const auto capture = [&random]( int32 hp, int32 catchRate, float32 ball, sw::MonsterStatus status )
    { return sw::MonsterBattle::computeCapture( 30, hp, catchRate, ball, status, random ); };

    const sw::MonsterCaptureResult normal = capture( 30, 255, 1.0f, sw::MonsterStatus::None ); // (90 - 60) × 255 / 90 = 85
    SW_EXPECT_EQUAL( 85, normal._catchValue );
    SW_EXPECT_EQUAL( 49931, normal._shakeChance ); // 1048560 / ⌊√⌊√196608⌋⌋ = 1048560 / 21
    SW_EXPECT_TRUE( 0 <= normal._shakes && normal._shakes <= 3 );

    SW_EXPECT_EQUAL( 170, capture( 30, 255, 1.0f, sw::MonsterStatus::Sleep )._catchValue );
    SW_EXPECT_EQUAL( 170, capture( 30, 255, 1.0f, sw::MonsterStatus::Freeze )._catchValue );
    SW_EXPECT_EQUAL( 127, capture( 30, 255, 1.0f, sw::MonsterStatus::Burn )._catchValue );

    const sw::MonsterCaptureResult sure = capture( 30, 255, 3.0f, sw::MonsterStatus::None );
    SW_EXPECT_TRUE( sure._bCaught );
    SW_EXPECT_EQUAL( 3, sure._shakes );
    SW_EXPECT_EQUAL( 65536, sure._shakeChance );

    SW_EXPECT_EQUAL( 28, capture( 30, 999, 0.33f, sw::MonsterStatus::None )._catchValue ); // 포획률 255 까지
    SW_EXPECT_EQUAL( 1, capture( 30, -5, 0.0f, sw::MonsterStatus::None )._catchValue );    // 최소 1
    SW_EXPECT_EQUAL( capture( 1, 45, 1.0f, sw::MonsterStatus::None )._catchValue, capture( -9, 45, 1.0f, sw::MonsterStatus::None )._catchValue );
    SW_EXPECT_EQUAL( capture( 30, 45, 1.0f, sw::MonsterStatus::None )._catchValue, capture( 99, 45, 1.0f, sw::MonsterStatus::None )._catchValue );
}

/**
 * @brief [MonsterCollectorRulesTest] 행동 순서: 우선도, 다음이 스피드, 교체는 기술보다 먼저(들어온 개체가 맞는다)
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, ActionOrder )
{
    const int32 player = sw::MonsterBattle::kPlayerSide;
    const int32 foe    = sw::MonsterBattle::kFoeSide;
    open( make( "normie", 50, "strike", "quick" ), make( "swift", 50, "strike" ) );

    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
    SW_EXPECT_TRUE( _battle.setAction( foe, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( foe, takeFirstActor() );

    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 1 ) ) ); // 우선도 +1
    SW_EXPECT_TRUE( _battle.setAction( foe, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( player, takeFirstActor() );

    sw::vector<sw::MonsterInstance> listPlayer;
    listPlayer.push_back( make( "normie", 50, "strike" ) );
    listPlayer.push_back( make( "rocky", 50, "strike" ) );
    sw::vector<sw::MonsterInstance> listFoe;
    listFoe.push_back( make( "swift", 50, "strike" ) );
    _battle.start( listPlayer, listFoe, false );
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeSwitch( 1 ) ) );
    SW_EXPECT_TRUE( _battle.setAction( foe, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, _battle.getActiveIndex( player ) );
    SW_EXPECT_EQUAL( 110, _battle.getParty( player )[0]._hp );
    SW_EXPECT_TRUE( _battle.getParty( player )[1]._hp < 110 );
}

/**
 * @brief [MonsterCollectorRulesTest] 행동 거절: 없는 편, 빈 슬롯, 범위 밖 슬롯 · 교체 번호, 쓰러진 개체로 교체, 트레이너전과 상대 편의 볼 · 도망, 0 이하 볼 배율
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, ActionRefusals )
{
    const int32                     player = sw::MonsterBattle::kPlayerSide;
    sw::vector<sw::MonsterInstance> listPlayer;
    listPlayer.push_back( make( "normie", 50, "strike" ) );
    listPlayer.push_back( make( "rocky", 50, "strike" ) );
    listPlayer.push_back( make( "rocky", 50, "strike" ) );
    listPlayer[2]._hp = 0;
    sw::vector<sw::MonsterInstance> listFoe;
    listFoe.push_back( make( "swift", 50, "strike" ) );
    _battle.start( listPlayer, listFoe, false );

    SW_EXPECT_FALSE( _battle.setAction( -1, sw::MonsterAction::makeMove( 0 ) ) );
    SW_EXPECT_FALSE( _battle.setAction( 2, sw::MonsterAction::makeMove( 0 ) ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeMove( 1 ) ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeMove( -1 ) ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeMove( sw::MonsterInstance::kMoveSlotCount ) ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeSwitch( 0 ) ) ); // 지금 나선 개체
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeSwitch( 2 ) ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeSwitch( 3 ) ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeSwitch( -1 ) ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeBall( 1.0f ) ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeRun() ) );

    open( make( "normie", 50, "strike" ), make( "swift", 50, "strike" ), true );
    SW_EXPECT_FALSE( _battle.setAction( sw::MonsterBattle::kFoeSide, sw::MonsterAction::makeBall( 1.0f ) ) );
    SW_EXPECT_FALSE( _battle.setAction( sw::MonsterBattle::kFoeSide, sw::MonsterAction::makeRun() ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeBall( 0.0f ) ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeBall( -1.0f ) ) );
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeBall( 1.0f ) ) );
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeRun() ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 결함: `start` 전의 `resolveRound` 가 턴 끝 처리에서 빈 파티의 나선 개체를 범위 밖으로 읽었다(`getActive` 의 빈 개체는 살아 있어 막지 못했다). 시작 전 전투는 진행하지 않는다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, BattleBeforeStartDoesNothing )
{
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 0, _battle.getRound() );
    SW_EXPECT_FALSE( _battle.setAction( sw::MonsterBattle::kPlayerSide, sw::MonsterAction{} ) );

    sw::MonsterBattle noCatalog;
    noCatalog.resolveRound();
    SW_EXPECT_EQUAL( 0, noCatalog.getRound() );

    // 시작해 보니 한쪽이 비었으면 곧바로 끝이다.
    sw::vector<sw::MonsterInstance> listNone;
    sw::vector<sw::MonsterInstance> listOne;
    listOne.push_back( make( "normie", 50, "strike" ) );
    _battle.start( listNone, listOne, false );
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Lost, _battle.getResult() );
    _battle.start( listOne, listNone, false );
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Won, _battle.getResult() );
    listOne[0]._hp = 0;
    _battle.start( listOne, listOne, false );
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Lost, _battle.getResult() );
    SW_EXPECT_FALSE( _battle.setAction( sw::MonsterBattle::kPlayerSide, sw::MonsterAction::makeMove( 0 ) ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 맨 앞의 쓰러진 개체는 건너뛰고 싸울 수 있는 개체가 나선다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, StartSkipsFaintedMonsters )
{
    sw::vector<sw::MonsterInstance> listPlayer;
    listPlayer.push_back( make( "normie", 50, "strike" ) );
    listPlayer.push_back( make( "rocky", 50, "strike" ) );
    listPlayer[0]._hp = 0;
    sw::vector<sw::MonsterInstance> listFoe;
    listFoe.push_back( make( "swift", 50, "strike" ) );
    _battle.start( listPlayer, listFoe, false );
    SW_EXPECT_EQUAL( 1, _battle.getActiveIndex( sw::MonsterBattle::kPlayerSide ) );
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Ongoing, _battle.getResult() );
}

/**
 * @brief [MonsterCollectorRulesTest] 턴 끝 상태이상 피해: 독 1/8, 화상 1/16, 맹독 n/16(매 턴 오른다), 교체하면 맹독의 n 은 1 로
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, StatusDamage )
{
    const int32 player = sw::MonsterBattle::kPlayerSide;

    sw::MonsterInstance poisoned = make( "normie", 50 );
    poisoned._status             = sw::MonsterStatus::Poison;
    open( poisoned, make( "swift", 50 ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 97, _battle.getActive( player )._hp );

    sw::MonsterInstance burned = make( "normie", 50 );
    burned._status             = sw::MonsterStatus::Burn;
    open( burned, make( "swift", 50 ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 104, _battle.getActive( player )._hp );

    sw::MonsterInstance toxic = make( "normie", 50 );
    toxic._status             = sw::MonsterStatus::Toxic;
    toxic._statusTurns        = 1;
    sw::vector<sw::MonsterInstance> listPlayer;
    listPlayer.push_back( toxic );
    listPlayer.push_back( make( "rocky", 50 ) );
    sw::vector<sw::MonsterInstance> listFoe;
    listFoe.push_back( make( "swift", 50 ) );
    _battle.start( listPlayer, listFoe, false );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 104, _battle.getActive( player )._hp );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 91, _battle.getActive( player )._hp ); // 110 × 2 / 16 = 13
    SW_EXPECT_EQUAL( 3, _battle.getActive( player )._statusTurns );

    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeSwitch( 1 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, _battle.getParty( player )[0]._statusTurns );
}

/**
 * @brief [MonsterCollectorRulesTest] 행동 전 상태이상: 수면은 남은 턴만큼 쉬고 깬 라운드에 움직인다. 상태이상 피해로 쓰러지면 패배
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, SleepAndStatusFaint )
{
    const int32         player  = sw::MonsterBattle::kPlayerSide;
    sw::MonsterInstance sleeper = make( "normie", 50, "strike" );
    sleeper._status             = sw::MonsterStatus::Sleep;
    sleeper._statusTurns        = 1;
    open( sleeper, make( "swift", 50 ) );

    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, countEvents( sw::MonsterBattleEvent::Kind::Asleep ) );
    SW_EXPECT_EQUAL( 0, countEvents( sw::MonsterBattleEvent::Kind::MoveUsed ) );

    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, countEvents( sw::MonsterBattleEvent::Kind::Woke ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::MonsterBattleEvent::Kind::MoveUsed ) );
    SW_EXPECT_EQUAL( sw::MonsterStatus::None, _battle.getActive( player )._status );

    sw::MonsterInstance dying = make( "normie", 50 );
    dying._status             = sw::MonsterStatus::Poison;
    dying._hp                 = 1;
    open( dying, make( "swift", 50 ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Lost, _battle.getResult() );
    SW_EXPECT_EQUAL( 0, _battle.getActive( player )._hp );
}

/**
 * @brief [MonsterCollectorRulesTest] 상태이상 걸기: 한 번에 하나, 타입 면역(불꽃은 화상), 맹독은 n = 1 에서 시작한다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, ApplyStatus )
{
    const int32 player = sw::MonsterBattle::kPlayerSide;
    const int32 foe    = sw::MonsterBattle::kFoeSide;

    open( make( "normie", 50, "burnup" ), make( "flamo", 50 ) );
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::MonsterStatus::None, _battle.getActive( foe )._status );
    SW_EXPECT_EQUAL( 0, countEvents( sw::MonsterBattleEvent::Kind::StatusApplied ) );

    open( make( "normie", 50, "toxin", "burnup" ), make( "normie", 50 ) );
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::MonsterStatus::Toxic, _battle.getActive( foe )._status );
    SW_EXPECT_EQUAL( 2, _battle.getActive( foe )._statusTurns ); // 턴 끝에 한 번 올랐다
    SW_EXPECT_EQUAL( 104, _battle.getActive( foe )._hp );

    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 1 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::MonsterStatus::Toxic, _battle.getActive( foe )._status ); // 겹쳐 걸리지 않는다
}

/**
 * @brief [MonsterCollectorRulesTest] 능력 변화: 자기 / 상대, 상한에서 멈춘다(실제로 바뀐 단계를 알린다), 교체하면 사라진다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, StatStages )
{
    const int32                     player = sw::MonsterBattle::kPlayerSide;
    const int32                     foe    = sw::MonsterBattle::kFoeSide;
    sw::vector<sw::MonsterInstance> listPlayer;
    listPlayer.push_back( make( "normie", 50, "swords", "growl" ) );
    listPlayer.push_back( make( "rocky", 50 ) );
    sw::vector<sw::MonsterInstance> listFoe;
    listFoe.push_back( make( "swift", 50 ) );
    _battle.start( listPlayer, listFoe, false );

    for ( int32 use = 0; use < 4; ++use )
    {
        SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
        _battle.resolveRound();
    }
    SW_EXPECT_EQUAL( sw::MonsterBattle::kMaxStage, _battle.getStage( player, sw::MonsterStat::Attack ) );
    _battle.pollEvents( _listEvent );
    SW_REQUIRE_TRUE( _listEvent.empty() == false );
    int32 lastChange = -1;
    for ( const sw::MonsterBattleEvent& event : _listEvent )
    {
        if ( event._kind == sw::MonsterBattleEvent::Kind::StatChanged )
            lastChange = event._value;
    }
    SW_EXPECT_EQUAL( 0, lastChange ); // 넷째는 더 오르지 않았다

    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 1 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( -1, _battle.getStage( foe, sw::MonsterStat::Attack ) );

    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeSwitch( 1 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 0, _battle.getStage( player, sw::MonsterStat::Attack ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 기술 사용: PP 를 쓰고 다 쓰면 NoPp, 상성 0 은 Immune 이고 피해가 없다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, PpAndImmunity )
{
    const int32 player = sw::MonsterBattle::kPlayerSide;
    const int32 foe    = sw::MonsterBattle::kFoeSide;
    open( make( "normie", 50, "tackle", "haunt" ), make( "rocky", 50 ) );

    for ( int32 use = 0; use < 3; ++use )
    {
        SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
        _battle.resolveRound();
    }
    SW_EXPECT_EQUAL( 0, _battle.getActive( player )._arrMove[0]._pp );
    SW_EXPECT_EQUAL( 2, countEvents( sw::MonsterBattleEvent::Kind::MoveUsed ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::MonsterBattleEvent::Kind::NoPp ) );

    open( make( "rocky", 50, "haunt" ), make( "normie", 50 ) );
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, countEvents( sw::MonsterBattleEvent::Kind::Immune ) );
    SW_EXPECT_EQUAL( 110, _battle.getActive( foe )._hp );
    SW_EXPECT_EQUAL( 9, _battle.getActive( player )._arrMove[0]._pp );
}

/**
 * @brief [MonsterCollectorRulesTest] 쓰러짐과 교체: 경험치(트레이너 ×1.5)와 노력치, 교체 대기 중에는 행동 · 라운드를 받지 않고, 쓰러진 자리만 채운다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, FaintWaitsForSwitch )
{
    const int32                     player = sw::MonsterBattle::kPlayerSide;
    const int32                     foe    = sw::MonsterBattle::kFoeSide;
    sw::vector<sw::MonsterInstance> listPlayer;
    listPlayer.push_back( make( "swift", 50, "strike" ) );
    sw::vector<sw::MonsterInstance> listFoe;
    listFoe.push_back( make( "normie", 50, "strike" ) );
    listFoe.push_back( make( "rocky", 50, "strike" ) );
    listFoe[0]._hp = 1;
    _battle.start( listPlayer, listFoe, false );
    const int64 expBefore = _battle.getActive( player )._exp;

    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
    SW_EXPECT_TRUE( _battle.setAction( foe, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, countEvents( sw::MonsterBattleEvent::Kind::Fainted ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::MonsterBattleEvent::Kind::NeedsSwitch ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::MonsterBattleEvent::Kind::MoveUsed ) ); // 쓰러진 쪽의 행동은 버린다
    SW_EXPECT_EQUAL( expBefore + 750, _battle.getActive( player )._exp );
    SW_EXPECT_EQUAL( 2, _battle.getActive( player )._arrEv[static_cast<size_t>( sw::MonsterStat::Attack )] );
    SW_EXPECT_TRUE( _battle.needsSwitch( foe ) );

    const int32 roundBefore = _battle.getRound();
    SW_EXPECT_FALSE( _battle.setAction( foe, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( roundBefore, _battle.getRound() );

    SW_EXPECT_FALSE( _battle.switchFainted( player, 0 ) ); // 기다리는 쪽이 아니다
    SW_EXPECT_FALSE( _battle.switchFainted( 2, 0 ) );
    SW_EXPECT_FALSE( _battle.switchFainted( foe, 0 ) ); // 쓰러진 개체
    SW_EXPECT_FALSE( _battle.switchFainted( foe, 9 ) );
    SW_EXPECT_TRUE( _battle.switchFainted( foe, 1 ) );
    SW_EXPECT_FALSE( _battle.needsSwitch( foe ) );
    SW_EXPECT_EQUAL( 1, _battle.getActiveIndex( foe ) );
    SW_EXPECT_TRUE( _battle.setAction( foe, sw::MonsterAction::makeMove( 0 ) ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 승패: 상대가 모두 쓰러지면 승리(야생은 ×1), 내가 모두 쓰러지면 패배이고 경험치가 없다. 끝난 전투는 행동을 받지 않는다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, WinAndLose )
{
    const int32 player = sw::MonsterBattle::kPlayerSide;
    const int32 foe    = sw::MonsterBattle::kFoeSide;

    sw::MonsterInstance weak = make( "normie", 50, "strike" );
    weak._hp                 = 1;
    open( make( "swift", 50, "strike" ), weak, true );
    const int64 expBefore = _battle.getActive( player )._exp;
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Won, _battle.getResult() );
    SW_EXPECT_EQUAL( expBefore + 500, _battle.getActive( player )._exp );
    SW_EXPECT_TRUE( findFirstValue( sw::MonsterBattleEvent::Kind::BattleEnded ) == static_cast<int32>( sw::MonsterBattleResult::Won ) );
    SW_EXPECT_FALSE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );

    open( weak, make( "swift", 50, "strike" ) );
    const int64 weakExp = _battle.getActive( player )._exp;
    SW_EXPECT_TRUE( _battle.setAction( foe, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Lost, _battle.getResult() );
    SW_EXPECT_EQUAL( weakExp, _battle.getActive( player )._exp );
}

/**
 * @brief [MonsterCollectorRulesTest] 볼: 포획률 255 · 볼 ×3 은 반드시 잡고 개체를 남긴다. 도망: 내가 빠르면 바로, 느리면 시도마다 쉬워져 끝내 도망친다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, BallAndRun )
{
    const int32 player = sw::MonsterBattle::kPlayerSide;
    open( make( "normie", 50 ), make( "flamo", 50 ), true );
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeBall( 3.0f ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Captured, _battle.getResult() );
    SW_EXPECT_TRUE( _battle.getCaptured()._speciesID == sw::hashed_string( "flamo" ) );
    SW_EXPECT_EQUAL( 3, countEvents( sw::MonsterBattleEvent::Kind::CaptureShake ) );

    open( make( "swift", 50 ), make( "normie", 50 ), true );
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeRun() ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Escaped, _battle.getResult() );

    open( make( "normie", 50 ), make( "swift", 50 ), true ); // F = 55 × 128 / 205 + 30 × (시도 - 1): 아홉째에 255 를 넘는다
    for ( int32 attempt = 0; attempt < 9 && _battle.getResult() == sw::MonsterBattleResult::Ongoing; ++attempt )
    {
        SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeRun() ) );
        _battle.resolveRound();
    }
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Escaped, _battle.getResult() );
}

/**
 * @brief [MonsterCollectorRulesTest] 날씨: 턴이 줄어 끝나고, 0 이하면 끝없고, 턴 끝 피해와 면역 타입, 기술이 부르는 날씨.
 *        결함: 모르는 날씨 id 가 지금 날씨를 말없이 지웠다 - 오류를 남기고 그대로 둔다. 기술의 모르는 날씨는 카탈로그가 보지 않아 5 턴 폴백으로 갔다.
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, Weather )
{
    const int32 player = sw::MonsterBattle::kPlayerSide;
    const int32 foe    = sw::MonsterBattle::kFoeSide;
    open( make( "normie", 50, "rainfall" ), make( "rocky", 50 ) );

    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_TRUE( _battle.getWeather() == sw::hashed_string( "Rain" ) );
    SW_EXPECT_EQUAL( 1, _battle.getWeatherTurns() ); // 데이터의 2 턴에서 라운드 끝에 하나
    SW_EXPECT_TRUE( findFirstValue( sw::MonsterBattleEvent::Kind::WeatherStarted ) == 2 );
    _battle.resolveRound();
    SW_EXPECT_TRUE( _battle.getWeather().empty() );
    SW_EXPECT_EQUAL( 0, _battle.getWeatherTurns() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::MonsterBattleEvent::Kind::WeatherEnded ) );

    _battle.setWeather( "Sand", 0 );
    SW_EXPECT_EQUAL( -1, _battle.getWeatherTurns() );
    _battle.resolveRound();
    _battle.resolveRound();
    SW_EXPECT_EQUAL( -1, _battle.getWeatherTurns() );
    SW_EXPECT_EQUAL( 110 - 12, _battle.getActive( player )._hp ); // 110 / 16 = 6 씩 두 번
    SW_EXPECT_EQUAL( 110, _battle.getActive( foe )._hp );         // 바위는 면역

    SW_EXPECT_LOG( "MonsterBattle.UnknownWeather" );
    _battle.setWeather( "nope", 3 );
    SW_EXPECT_TRUE( _battle.getWeather() == sw::hashed_string( "Sand" ) );
    SW_EXPECT_EQUAL( -1, _battle.getWeatherTurns() );

    _listEvent.clear();
    _battle.setWeather( sw::hashed_string{}, 3 );
    SW_EXPECT_TRUE( _battle.getWeather().empty() );
    SW_EXPECT_EQUAL( 0, _battle.getWeatherTurns() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::MonsterBattleEvent::Kind::WeatherEnded ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 전투 상태 왕복: 개체 · 능력 변화 · 정한 행동 · 날씨를 싣고, 읽은 전투가 같은 명령에 같은 수열을 잇는다. 잡은 개체도 왕복한다
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, BattleStateRoundTrip )
{
    const int32 player = sw::MonsterBattle::kPlayerSide;
    const int32 foe    = sw::MonsterBattle::kFoeSide;
    open( make( "normie", 50, "growl", "swords" ), make( "rocky", 50, "strike" ), true ); // 피해를 주지 않아 세 라운드 뒤에도 전투가 이어진다
    _battle.setWeather( "Sand", 4 );
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 1 ) ) );
    _battle.resolveRound();
    SW_EXPECT_TRUE( _battle.setAction( foe, sw::MonsterAction::makeMove( 0 ) ) );

    sw::Archive written;
    _battle.writeState( written );
    sw::Archive       reading( written.getData(), written.getSize() );
    sw::MonsterBattle restored;
    restored.initialize( &_catalog, &_chart, 99 );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( MonsterTestUtil::isSameState( _battle, restored ) );
    SW_EXPECT_EQUAL( 2, restored.getStage( player, sw::MonsterStat::Attack ) );
    SW_EXPECT_TRUE( restored.isWild() );

    for ( int32 round = 0; round < 3; ++round )
    {
        SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
        SW_EXPECT_TRUE( restored.setAction( player, sw::MonsterAction::makeMove( 0 ) ) );
        _battle.resolveRound();
        restored.resolveRound();
    }
    SW_EXPECT_TRUE( MonsterTestUtil::isSameState( _battle, restored ) );

    open( make( "normie", 50 ), make( "flamo", 50 ), true );
    SW_EXPECT_TRUE( _battle.setAction( player, sw::MonsterAction::makeBall( 3.0f ) ) );
    _battle.resolveRound();
    sw::Archive writtenCaptured;
    _battle.writeState( writtenCaptured );
    sw::Archive readingCaptured( writtenCaptured.getData(), writtenCaptured.getSize() );
    SW_REQUIRE_TRUE( restored.readState( readingCaptured ) );
    SW_EXPECT_EQUAL( sw::MonsterBattleResult::Captured, restored.getResult() );
    SW_EXPECT_TRUE( restored.getCaptured()._speciesID == sw::hashed_string( "flamo" ) );

    // 시작 전 전투도 왕복한다. 카탈로그 없이는 읽지 않는다.
    sw::MonsterBattle fresh;
    fresh.initialize( &_catalog, &_chart, 5 );
    sw::Archive writtenFresh;
    fresh.writeState( writtenFresh );
    sw::Archive readingFresh( writtenFresh.getData(), writtenFresh.getSize() );
    SW_EXPECT_TRUE( restored.readState( readingFresh ) );
    sw::Archive       readingNone( writtenFresh.getData(), writtenFresh.getSize() );
    sw::MonsterBattle noCatalog;
    SW_EXPECT_FALSE( noCatalog.readState( readingNone ) );
}

/**
 * @brief [MonsterCollectorRulesTest] 결함: 전투 상태가 정한 행동을 보지 않았다. 범위 밖 기술 슬롯은 `resolveRound` 가 배열을 넘어 읽었고, 트레이너전의 볼 · 상대의 도망 · 0 배 볼을 받았다.
 *        플래그 칸을 bool 로 바로 읽어 2 같은 바이트가 그대로 들어갔다. 교체할 개체가 없는 교체 대기는 전투를 영원히 멈췄고, 잡은 개체의 id 와 날씨 턴도 보지 않았다.
 */
SW_TEST_CASE_F( MonsterCollectorRulesTest, BattleStateRefusals )
{
    const int32 player = sw::MonsterBattle::kPlayerSide;
    open( make( "rocky", 50, "strike" ), make( "rocky", 50 ) );
    const sw::MonsterBattle snapshot = _battle;

    const auto refuses = [this, &snapshot]( auto mutate )
    {
        MonsterForgedBattle forged = makeForged();
        mutate( forged );
        return readForged( forged ) == false && MonsterTestUtil::isSameState( snapshot, _battle );
    };
    const auto moveKind   = static_cast<uint8>( sw::MonsterActionKind::Move );
    const auto switchKind = static_cast<uint8>( sw::MonsterActionKind::Switch );
    const auto ballKind   = static_cast<uint8>( sw::MonsterActionKind::Ball );

    SW_EXPECT_TRUE( refuses( [&]( MonsterForgedBattle& forged )
    {
        forged._actionKind  = moveKind;
        forged._actionIndex = 9; // 배열을 넘는다
    } ) );
    SW_EXPECT_TRUE( refuses( [&]( MonsterForgedBattle& forged )
    {
        forged._actionKind  = moveKind;
        forged._actionIndex = -1;
    } ) );
    SW_EXPECT_TRUE( refuses( [&]( MonsterForgedBattle& forged )
    {
        forged._actionKind  = moveKind;
        forged._actionIndex = 3; // 빈 슬롯
    } ) );
    SW_EXPECT_TRUE( refuses( [&]( MonsterForgedBattle& forged )
    {
        forged._actionKind  = switchKind;
        forged._actionIndex = 0; // 나선 개체
    } ) );
    SW_EXPECT_TRUE( refuses( [&]( MonsterForgedBattle& forged )
    {
        forged._actionKind = ballKind;
        forged._wild       = 0;
    } ) );
    SW_EXPECT_TRUE( refuses( [&]( MonsterForgedBattle& forged )
    {
        forged._actionKind     = ballKind;
        forged._ballMultiplier = 0.0f;
    } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._actionKind = 9; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._stage = sw::MonsterBattle::kMaxStage + 1; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._stage = sw::MonsterBattle::kMinStage - 1; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._activeIndex = 1; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._activeIndex = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._needsSwitch = 2; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._wild = 2; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._result = 9; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._escapeAttempts = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._listPlayer[0]._hp = 0; } ) ); // 진행 중인데 쓰러진 개체가 교체를 기다리지 않는다
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    {
        forged._listPlayer[0]._hp = 0;
        forged._needsSwitch       = 1; // 대신 나설 개체가 없다
    } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._listPlayer[0]._speciesID = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._listPlayer[0]._natureID = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._listPlayer[0]._arrMove[1]._moveID = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._foe._speciesID = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    {
        forged._weatherID    = "nope";
        forged._weatherTurns = 2;
    } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._weatherTurns = 2; } ) ); // 날씨가 없는데 턴이 있다
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._weatherID = "Rain"; } ) ); // 턴 0 인 날씨는 없다
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    {
        forged._weatherID    = "Rain";
        forged._weatherTurns = -2;
    } ) );
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._result = static_cast<uint8>( sw::MonsterBattleResult::Captured ); } ) ); // 잡은 개체가 없다
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    { forged._captured._speciesID = "nope"; } ) );

    // 빈 파티의 행동은 시작 전 전투에 없다.
    SW_EXPECT_TRUE( refuses( []( MonsterForgedBattle& forged )
    {
        forged._listPlayer.clear();
        forged._actionKind = 1;
    } ) );

    // 맞는 상태는 읽히고, 그대로 쓸 수 있다.
    MonsterForgedBattle valid = makeForged();
    valid._actionKind         = moveKind;
    valid._weatherID          = "Rain";
    valid._weatherTurns       = -1;
    SW_REQUIRE_TRUE( readForged( valid ) );
    SW_EXPECT_EQUAL( 110, _battle.getActive( player )._hp );
    SW_EXPECT_EQUAL( -1, _battle.getWeatherTurns() );
    SW_EXPECT_TRUE( _battle.isWild() );
}

/**
 * @brief [MonsterCollectorTest] 카탈로그: 능력치 묶음 · 타입 · 분류 · 조우표 · 면역 · 정렬된 기술
 */
SW_TEST_CASE( MonsterCollectorTest, CatalogReadsDefinitions )
{
    sw::MonsterCollectorCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kMonsterXML, "MonsterTest" ) );
    const sw::MonsterSpeciesDef* pFlamo = catalog.findSpecies( "flamo" );
    SW_REQUIRE_TRUE( pFlamo != nullptr );
    SW_EXPECT_TRUE( pFlamo->hasType( "Fire" ) );
    SW_EXPECT_FALSE( pFlamo->hasType( "Water" ) );
    SW_EXPECT_EQUAL( 255, pFlamo->_catchRate );
    SW_EXPECT_EQUAL( 2, pFlamo->_arrEvYield[static_cast<size_t>( sw::MonsterStat::Attack )] );
    SW_EXPECT_EQUAL( 2u, pFlamo->_listEvolution.size() );
    SW_EXPECT_EQUAL( 2u, catalog.findSpecies( "flamox" )->_listType.size() );
    SW_EXPECT_EQUAL( 1, catalog.findSpecies( "normie" )->_listLearn.front()._level ); // 레벨 오름차순으로 정렬한다
    SW_EXPECT_EQUAL( 5, catalog.findSpecies( "normie" )->_listLearn.back()._level );

    const sw::MonsterMoveDef* pEmber = catalog.findMove( "ember" );
    SW_REQUIRE_TRUE( pEmber != nullptr );
    SW_EXPECT_EQUAL( sw::MonsterMoveCategory::Special, pEmber->_category );
    SW_EXPECT_TRUE( pEmber->isDamaging() );
    SW_EXPECT_FALSE( catalog.findMove( "growl" )->isDamaging() );
    SW_EXPECT_EQUAL( -1, catalog.findMove( "growl" )->_statStages );
    SW_EXPECT_EQUAL( 16, catalog.findWeather( "Sand" )->_chipDivisor );
    SW_EXPECT_EQUAL( 2u, catalog.getNatures().size() );

    sw::vector<sw::hashed_string> listFire;
    listFire.push_back( "Fire" );
    sw::vector<sw::hashed_string> listWater;
    listWater.push_back( "Water" );
    SW_EXPECT_TRUE( catalog.isStatusImmune( sw::MonsterStatus::Burn, listFire ) );
    SW_EXPECT_FALSE( catalog.isStatusImmune( sw::MonsterStatus::Burn, listWater ) );
    SW_EXPECT_FALSE( catalog.isStatusImmune( sw::MonsterStatus::Poison, listFire ) );

    sw::MonsterStat stat = sw::MonsterStat::HP;
    SW_EXPECT_TRUE( sw::MonsterCollectorCatalog::parseStat( "Speed", stat ) );
    SW_EXPECT_EQUAL( sw::MonsterStat::Speed, stat );
    SW_EXPECT_FALSE( sw::MonsterCollectorCatalog::parseStat( "nope", stat ) );

    catalog.clear();
    SW_EXPECT_TRUE( catalog.findSpecies( "flamo" ) == nullptr );
    SW_EXPECT_TRUE( catalog.getNatures().empty() );
}

/**
 * @brief [MonsterCollectorTest] 야생 조우: 지역 · 시간대가 맞는 표에서 굴리고, 레벨은 범위 안, 가중치 0 은 나오지 않으며, 맞는 표가 없으면 false
 */
SW_TEST_CASE( MonsterCollectorTest, RollEncounter )
{
    sw::MonsterCollectorCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kMonsterXML, "MonsterTest" ) );
    sw::GameRandom random;
    random.setSeed( 5 );
    sw::hashed_string speciesID;
    int32             level = 0;

    SW_EXPECT_FALSE( catalog.rollEncounter( "route1", "Day", random, speciesID, level ) );
    SW_EXPECT_FALSE( catalog.rollEncounter( "nope", "Night", random, speciesID, level ) );
    SW_EXPECT_TRUE( speciesID.empty() );
    for ( int32 roll = 0; roll < 32; ++roll )
    {
        SW_REQUIRE_TRUE( catalog.rollEncounter( "route1", "Dusk", random, speciesID, level ) );
        SW_EXPECT_TRUE( speciesID == sw::hashed_string( "flamo" ) );
        SW_EXPECT_TRUE( 2 <= level && level <= 4 );
        SW_REQUIRE_TRUE( catalog.rollEncounter( "cave", "Day", random, speciesID, level ) ); // 시간대를 적지 않은 표는 아무 때나
        SW_EXPECT_TRUE( speciesID == sw::hashed_string( "normie" ) );
        SW_EXPECT_EQUAL( 5, level );
    }
}

/**
 * @brief [MonsterCollectorTest] 결함: 손으로 읽는 원소(기술 · 종 · 날씨 · 조우표)는 겹친 id 가 앞 정의를 말없이 덮어썼다. 원소 하나를 넣지 않고 오류다
 */
SW_TEST_CASE( MonsterCollectorTest, DuplicateIDKeepsTheFirstDefinition )
{
    sw::MonsterCollectorCatalog catalog;
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<MonsterCollectorCatalog>
  <Move id="m" name="First" power="1"/><Move id="m" name="Second" power="2"/>
  <Species id="s" name="First" types="Fire" stats="1 1 1 1 1 1"/><Species id="s" name="Second" types="Fire" stats="2 2 2 2 2 2"/>
  <Weather id="w" turns="1"/><Weather id="w" turns="2"/>
  <Nature id="n" up="Speed"/><Nature id="n" up="Defense"/>
</MonsterCollectorCatalog>)",
                                              "MonsterTest" ) );
    SW_EXPECT_EQUAL( 1, catalog.findMove( "m" )->_power );
    SW_EXPECT_EQUAL( 1, catalog.findSpecies( "s" )->_arrBaseStat[0] );
    SW_EXPECT_EQUAL( 1, catalog.findWeather( "w" )->_turns );
    SW_EXPECT_EQUAL( sw::MonsterStat::Speed, catalog.findNature( "n" )->_raised );
}

/**
 * @brief [MonsterCollectorTest] 깨진 원소: 타입 수 · 능력치 묶음 · 확률 없는 상태이상 · HP 능력 변화 · None 면역 · 모르는 자식과 원소 · 범위 밖 레벨은 그 원소를 넣지 않고 오류다
 */
SW_TEST_CASE( MonsterCollectorTest, CatalogRefusesBrokenElements )
{
    sw::MonsterCollectorCatalog catalog;
    SW_EXPECT_LOG( "MonsterCollectorCatalog.SpeciesTypesNeeds" );
    SW_EXPECT_LOG( "MonsterCollectorCatalog.NeedsHPAttackDefense" );
    SW_EXPECT_LOG( "MonsterCollectorCatalog.NeedsSixIntegersMore" );
    SW_EXPECT_LOG( "MonsterCollectorCatalog.MoveSetsStatusBut" );
    SW_EXPECT_LOG( "MonsterCollectorCatalog.MoveCannotChangeHP" );
    SW_EXPECT_LOG( "MonsterCollectorCatalog.StatusImmunityNeedsKnownStatus" );
    SW_EXPECT_LOG( "MonsterCollectorCatalog.SpeciesUnknownChild" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<MonsterCollectorCatalog>
  <Move id="good" name="Good"/>
  <Species id="typeless" name="T" stats="1 1 1 1 1 1"/>
  <Species id="triple" name="T" types="A,B,C" stats="1 1 1 1 1 1"/>
  <Species id="statless" name="T" types="A"/>
  <Species id="five" name="T" types="A" stats="1 1 1 1 1"/>
  <Species id="seven" name="T" types="A" stats="1 1 1 1 1 1 1"/>
  <Species id="zero" name="T" types="A" stats="0 1 1 1 1 1"/>
  <Species id="negyield" name="T" types="A" stats="1 1 1 1 1 1" evYield="-1 0 0 0 0 0"/>
  <Species id="child" name="T" types="A" stats="1 1 1 1 1 1"><Nope/></Species>
  <Move id="nochance" name="N" status="Burn"/>
  <Move id="hpstage" name="H" stat="HP" stages="1"/>
  <StatusImmunity status="None" types="Fire"/>
  <Dragon id="x"/>
  <Move id="wide" name="W" accuracy="101"/>
</MonsterCollectorCatalog>)",
                                              "MonsterTest" ) );
    SW_EXPECT_TRUE( catalog.findMove( "good" ) != nullptr );
    SW_EXPECT_TRUE( catalog.getSpecies().empty() );
    SW_EXPECT_TRUE( catalog.findMove( "nochance" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findMove( "hpstage" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findMove( "wide" ) == nullptr );
}

/**
 * @brief [MonsterCollectorTest] 서로 가리키는 id: 모르는 기술 · 진화 대상 · 조우 종을 읽은 뒤 알린다.
 *        결함: 기술이 부르는 날씨는 보지 않았다.
 */
SW_TEST_CASE( MonsterCollectorTest, CatalogReportsUnknownReferences )
{
    sw::MonsterCollectorCatalog catalog;
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<MonsterCollectorCatalog>
  <Species id="s" name="S" types="A" stats="1 1 1 1 1 1"><Learn move="nope"/><Evolve to="nope"/></Species>
  <Encounter id="e" area="a"><Slot species="nope" min="1" max="1"/></Encounter>
  <Move id="m" name="M" category="Status" weather="nope"/>
</MonsterCollectorCatalog>)",
                                              "MonsterTest" ) );
}
