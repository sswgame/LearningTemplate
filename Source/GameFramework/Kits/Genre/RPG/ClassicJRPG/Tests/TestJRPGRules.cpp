#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGBattle.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGCatalog.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGEncounter.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGParty.h"
#include "GameFramework/Runtime/Character/Input/TimingJudge.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

#include <algorithm>

// 클래식 JRPG 키트의 규칙: 전투(피해 공식 · 명령 거절 · 타이밍 · 합동기 · 시전 잠금 · 도망 · 승패 · 내공), 파티(성장 · 전직 · 여관 · 교회 · 장비 · 비급), 걸음 조우, 카탈로그 오류, 상태 바이트의 범위 검사.
// 난수가 끼는 수치는 공식의 범위로 본다(물리 피해 ×0.875..1.125, 주문 ×0.9..1.1). 키트 GF_ClassicJRPG(nogpu).

namespace
{
    constexpr const utf8* kJRPGXML = R"(<JRPGCatalog>
  <ExperienceCurve base="100" exponent="0" maxLevel="30"/>
  <Class id="hero" name="Hero" hp="100" mp="10" str="40" agi="50" vit="20" intellect="10" luck="5" growHP="10" growMP="2" growStr="2" growAgi="1" growVit="2" growIntellect="2" growLuck="0" attackType="Sword">
    <Learn level="1" spell="flare"/><Learn level="1" spell="blaze"/><Learn level="1" spell="mend"/><Learn level="3" spell="raise"/>
  </Class>
  <Class id="mage" name="Mage" hp="60" mp="40" str="4" agi="1" vit="4" intellect="100" luck="5" attackType="Blunt"><Learn level="1" spell="flare"/></Class>
  <Class id="sage" name="Sage" hp="50" requires="sagebook"/>
  <Spell id="flare" name="Flare" mp="5" power="100" type="Fire"/>
  <Spell id="blaze" name="Blaze" mp="5" power="100" type="Fire" target="All"/>
  <Spell id="mend" name="Mend" kind="Heal" mp="3" power="20"/>
  <Spell id="raise" name="Raise" kind="Revive" mp="8"/>
  <Spell id="pine_cut" name="PineCut" manual="pine" inner="20" power="20" type="Sword" proficiency="10"/>
  <Spell id="pine_mend" name="PineMend" kind="Heal" manual="pine" power="5" proficiency="30"/>
  <Spell id="pine_storm" name="PineStorm" manual="pine" power="50"/>
  <Combo id="eclipse" name="Eclipse" members="hero,mage" points="3" power="40" types="Sun,Moon"/>
  <Manual id="pine" name="Pine"><Stage proficiency="0" technique="pine_cut"/><Stage proficiency="0" technique="pine_mend"/><Stage proficiency="30" technique="pine_storm"/></Manual>
  <Enemy id="slime" name="Slime" hp="1000" str="0" agi="1" vit="0" intellect="0"/>
  <Enemy id="brittle" name="Brittle" hp="1000" str="0" agi="1" vit="0" weak="Sword"/>
  <Enemy id="ogre" name="Ogre" hp="1000" str="80" agi="1" vit="0"/>
  <Enemy id="giant" name="Giant" hp="1000" str="400" agi="1" vit="0"/>
  <Enemy id="frail" name="Frail" hp="1" str="0" agi="1" vit="0" exp="31" gold="7"/>
  <Enemy id="boss" name="Boss" hp="1000" str="0" agi="1" vit="0" boss="true"/>
  <Enemy id="wyrd" name="Wyrd" hp="1000" str="0" agi="1" vit="0" intellect="0" cast="flare" castTurns="2" castEvery="1" locks="Sword,Fire"/>
  <Area id="field" rate="1" grace="2"><Group enemies="slime,frail" weight="1"/></Area>
  <Area id="quiet" rate="0"><Group enemies="slime"/></Area>
  <Area id="weightless" rate="1"><Group enemies="slime" weight="0"/></Area>
  <Area id="empty" rate="1"/>
</JRPGCatalog>)";

    constexpr const utf8* kItemXML = R"(<ItemCatalog>
  <Item id="sagebook" category="Material" maxStack="1" weight="1"/>
  <Item id="blade" category="Weapon" slot="Weapon" maxStack="1" weight="1"><Stats attack="12"/></Item>
  <Item id="mail" category="Armor" slot="Armor" maxStack="1" weight="1"><Stats defense="4"/></Item>
</ItemCatalog>)";

    /** @brief 타이밍 입력을 정해 둡니다. 0 이면 판정 창 안, 5 면 창 밖입니다. */
    struct JRPGTestTiming : sw::IJRPGTimingInput
    {
        float32 _attackOffset{ 5.0f };
        float32 _blockOffset{ 5.0f };

        bool findPressOffset( sw::JRPGTimingKind kind, int32, float32& outOffset ) const override
        {
            outOffset = kind == sw::JRPGTimingKind::Attack ? _attackOffset : _blockOffset;
            return true;
        }
    };

    /** @brief 상태 바이트의 비교입니다. */
    struct JRPGTestUtil
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

    /** @brief 전투 상태 바이트를 손으로 씁니다. 초기값은 맞는 상태(적 하나, 멤버 하나)이고 케이스가 칸 하나를 망칩니다. */
    struct JRPGFixtureBattle
    {
        sw::hashed_string _enemyID{ "slime" };
        sw::hashed_string _commandID{};
        int32             _lockCount{ 0 };
        int32             _hp{ 500 };
        int32             _castTurnsLeft{ 0 };
        int32             _lockTotal{ 0 };
        int32             _turnsTaken{ 0 };
        int32             _commandCount{ 1 };
        int32             _comboPoints{ 0 };
        int32             _round{ 0 };
        int32             _fleeAttempts{ 0 };
        int64             _rewardExp{ 0 };
        int64             _rewardGold{ 0 };
        uint8             _commandKind{ 0 };
        uint8             _defending{ 0 };
        uint8             _result{ 0 };

        void write( sw::Archive& written ) const
        {
            sw::TurnOrder  order;
            sw::GameRandom random;
            random.setSeed( 3 );
            written << static_cast<uint32>( 1 );
            sw::StateIO::writeName( written, _enemyID );
            written << static_cast<uint32>( _lockCount );
            for ( int32 lock = 0; lock < _lockCount; ++lock )
            {
                sw::StateIO::writeName( written, "Sword" );
            }
            written << _hp;
            written << _castTurnsLeft;
            written << _lockTotal;
            written << _turnsTaken;
            written << static_cast<uint32>( _commandCount );
            for ( int32 command = 0; command < _commandCount; ++command )
            {
                sw::StateIO::writeName( written, _commandID );
                written << static_cast<int32>( 0 );
                written << _commandKind;
                written << _defending;
            }
            order.writeState( written );
            sw::StateIO::writeRandom( written, random );
            written << _rewardExp;
            written << _rewardGold;
            written << _comboPoints;
            written << _round;
            written << _fleeAttempts;
            written << _result;
        }
    };

    /** @brief 카탈로그, 파티, 전투 하나입니다. 멤버 0 은 hero(민첩 50 이라 먼저 움직인다)입니다. */
    struct JRPGRulesTest
    {
        sw::JRPGCatalog                 _catalog{};
        sw::ItemCatalog                 _itemCatalog{};
        sw::Wallet                      _wallet{};
        sw::Inventory                   _inventory{};
        sw::JRPGParty                   _party{};
        sw::JRPGBattle                  _battle{};
        sw::TimingJudge                 _judge{};
        JRPGTestTiming                  _timing{};
        sw::JRPGBattleSettings          _settings{};
        sw::vector<sw::JRPGBattleEvent> _listBattleEvent{};
        sw::vector<sw::JRPGPartyEvent>  _listPartyEvent{};

        JRPGRulesTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kJRPGXML, "JRPGTest" ) );
            SW_REQUIRE_TRUE( _itemCatalog.loadFromXMLText( kItemXML, "JRPGTest" ) );
            SW_REQUIRE_TRUE( _inventory.initialize( &_itemCatalog, 5 ) );
            initializeParty( _party );
            sw::TimingWindow window;
            window._grade      = "Good";
            window._earlyWidth = 0.1f;
            window._lateWidth  = 0.1f;
            sw::vector<sw::TimingWindow> listWindow;
            listWindow.push_back( window );
            _judge.setWindows( listWindow );
        }

        void initializeParty( sw::JRPGParty& outParty )
        {
            sw::SessionStateRefs refs;
            refs._pWallet    = &_wallet;
            refs._pInventory = &_inventory;
            outParty.initialize( &_catalog, &_itemCatalog, refs );
        }

        /** @brief 지금 설정으로 전투를 처음부터 엽니다(멤버는 앞서 넣어 둡니다). */
        bool startBattle( const sw::hashed_string& enemyID )
        {
            sw::vector<sw::hashed_string> listEnemy;
            listEnemy.push_back( enemyID );
            _battle.initialize( &_catalog, &_judge, _settings, 7 );
            _battle.setTimingInput( &_timing );
            return _battle.start( &_party, listEnemy );
        }

        int32 countBattleEvents( sw::JRPGBattleEvent::Kind kind )
        {
            _battle.pollEvents( _listBattleEvent );
            return sw::VectorUtil::countBy<&sw::JRPGBattleEvent::_kind>( _listBattleEvent, kind );
        }

        int32 countPartyEvents( sw::JRPGPartyEvent::Kind kind )
        {
            _party.pollEvents( _listPartyEvent );
            return sw::VectorUtil::countBy<&sw::JRPGPartyEvent::_kind>( _listPartyEvent, kind );
        }

        /** @brief 라운드 하나를 돌리고 멤버 0 이 잃은 HP 를 돌려줍니다. */
        int32 resolveAndMeasureHeroLoss()
        {
            const int32 before = _party.getMember( 0 )._hp;
            _battle.resolveRound();
            return before - _party.getMember( 0 )._hp;
        }

        /** @brief 멤버들이 적에게 준 피해 값을 차례로 돌려줍니다. */
        sw::vector<int32> collectMemberDamage()
        {
            _battle.pollEvents( _listBattleEvent );
            sw::vector<int32> listDamage;
            for ( const sw::JRPGBattleEvent& event : _listBattleEvent )
            {
                if ( event._kind == sw::JRPGBattleEvent::Kind::Damage && event._bEnemyActor == false )
                    listDamage.push_back( event._value );
            }
            return listDamage;
        }

        bool readForged( const JRPGFixtureBattle& forged )
        {
            sw::Archive written;
            forged.write( written );
            sw::Archive reading( written.getData(), written.getSize() );
            return _battle.readState( reading );
        }
    };
} // namespace

/**
 * @brief [JRPGRulesTest] DQ 식 물리 피해: (공격력 / 2 - 방어력 / 4) × 0.875..1.125, 1 미만이면 0 또는 1
 */
SW_TEST_CASE_F( JRPGRulesTest, PhysicalDamageFormula )
{
    sw::GameRandom random;
    random.setSeed( 11 );
    for ( int32 roll = 0; roll < 64; ++roll )
    {
        const int32 normal = sw::JRPGBattle::computePhysicalDamage( 40, 0, random );
        SW_EXPECT_TRUE( 17 <= normal && normal <= 22 );
        const int32 weak = sw::JRPGBattle::computePhysicalDamage( 1, 100, random );
        SW_EXPECT_TRUE( 0 <= weak && weak <= 1 );
    }
}

/**
 * @brief [JRPGRulesTest] 기본 공격: 범위 밖 대상은 살아 있는 적으로 옮기고, 약점 유형은 ×1.5, 한 타마다 콤보 포인트
 */
SW_TEST_CASE_F( JRPGRulesTest, AttackRetargetsAndUsesWeakness )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( startBattle( "slime" ) );
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 9 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, _battle.getRound() );
    const int32 dealt = 1000 - _battle.getEnemies()[0]._hp;
    SW_EXPECT_TRUE( 17 <= dealt && dealt <= 22 );
    SW_EXPECT_EQUAL( 1, _battle.getComboPoints() );

    SW_REQUIRE_TRUE( startBattle( "brittle" ) );
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    _battle.resolveRound();
    const int32 weakDealt = 1000 - _battle.getEnemies()[0]._hp;
    SW_EXPECT_TRUE( 25 <= weakDealt && weakDealt <= 33 );
}

/**
 * @brief [JRPGRulesTest] 받는 피해: 방어 명령과 타이밍 방어가 각각 절반, 함께면 사분의 일
 */
SW_TEST_CASE_F( JRPGRulesTest, DefendAndTimedBlockReduceDamage )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( startBattle( "ogre" ) ); // 힘 80, 방어력 10: 38 × 0.875..1.125 = 33..42

    const int32 plain = resolveAndMeasureHeroLoss();
    SW_EXPECT_TRUE( 33 <= plain && plain <= 42 );

    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeDefend() ) );
    const int32 defended = resolveAndMeasureHeroLoss();
    SW_EXPECT_TRUE( 16 <= defended && defended <= 21 );
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::Defending ) );

    _timing._blockOffset = 0.0f;
    const int32 blocked  = resolveAndMeasureHeroLoss();
    SW_EXPECT_TRUE( 16 <= blocked && blocked <= 21 );
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::TimedBlock ) );

    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeDefend() ) );
    const int32 both = resolveAndMeasureHeroLoss();
    SW_EXPECT_TRUE( 8 <= both && both <= 10 );
}

/**
 * @brief [JRPGRulesTest] 타이밍 공격: 맞으면 절반 위력의 추가 타격과 콤보 포인트가 하나 더, 창 밖이면 없다
 */
SW_TEST_CASE_F( JRPGRulesTest, TimedHitAddsAStrike )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( startBattle( "slime" ) );

    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1u, collectMemberDamage().size() );
    SW_EXPECT_EQUAL( 0, countBattleEvents( sw::JRPGBattleEvent::Kind::TimedHit ) );
    _listBattleEvent.clear();

    _timing._attackOffset = 0.0f;
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    _battle.resolveRound();
    const sw::vector<int32> listDamage = collectMemberDamage();
    SW_REQUIRE_EQUAL( 2u, listDamage.size() );
    SW_EXPECT_EQUAL( listDamage[0] / 2, listDamage[1] );
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::TimedHit ) );
    SW_EXPECT_EQUAL( 3, _battle.getComboPoints() );
}

/**
 * @brief [JRPGRulesTest] 주문: MP 소모, 위력 × (100 + 지능) / 100 × 0.9..1.1, 전체 대상, 실행 때 MP 가 모자라거나 잊었으면 쓰지 않는다
 */
SW_TEST_CASE_F( JRPGRulesTest, DamageSpells )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    sw::vector<sw::hashed_string> listEnemy;
    listEnemy.push_back( "slime" );
    listEnemy.push_back( "slime" );
    _battle.initialize( &_catalog, &_judge, _settings, 7 );
    SW_REQUIRE_TRUE( _battle.start( &_party, listEnemy ) );

    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "flare", 1 ) ) );
    _battle.resolveRound();
    const int32 dealt = 1000 - _battle.getEnemies()[1]._hp; // 위력 100 × 110% = 110
    SW_EXPECT_TRUE( 99 <= dealt && dealt <= 121 );
    SW_EXPECT_EQUAL( 1000, _battle.getEnemies()[0]._hp );
    SW_EXPECT_EQUAL( 5, _party.getMember( 0 )._mp );

    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "blaze", 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_TRUE( _battle.getEnemies()[0]._hp < 1000 );
    SW_EXPECT_TRUE( _battle.getEnemies()[1]._hp < 1000 - dealt );
    SW_EXPECT_EQUAL( 0, _party.getMember( 0 )._mp );

    SW_EXPECT_FALSE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "flare", 0 ) ) ); // 고를 때 MP 가 모자란다

    _party.getMember( 0 )._mp = 5;
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "flare", 0 ) ) );
    _party.getMember( 0 )._mp = 0; // 고른 뒤에 잃었다
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::NotEnoughMP ) );

    _party.getMember( 0 )._mp = 5;
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "flare", 0 ) ) );
    _party.getMember( 0 )._listSpell.clear(); // 고른 뒤에 잊었다
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::CannotUse ) );
    SW_EXPECT_EQUAL( 5, _party.getMember( 0 )._mp );
}

/**
 * @brief [JRPGRulesTest] 회복과 부활: 최대에서 멈추고, 쓰러진 멤버는 회복되지 않으며, 부활은 절반으로 일어난다.
 *        결함: 없는 멤버를 고른 회복이 받아들여져 MP 만 쓰고 아무 일도 없었다 - 명령을 거절한다.
 */
SW_TEST_CASE_F( JRPGRulesTest, HealAndRevive )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero", 3 ) == 0 ); // 레벨 3: HP 120, MP 14, 지능 14 이고 raise 를 배웠다
    SW_REQUIRE_TRUE( _party.addMember( "mage", "Mage", "mage" ) == 1 );
    SW_REQUIRE_TRUE( startBattle( "slime" ) );

    SW_EXPECT_FALSE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "mend", 9 ) ) );
    SW_EXPECT_FALSE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "mend", -1 ) ) );
    SW_EXPECT_FALSE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "raise", 2 ) ) );

    _party.getMember( 1 )._hp = 10;
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "mend", 1 ) ) ); // 위력 20 + 지능 14 / 2 = 27
    _battle.resolveRound();
    SW_EXPECT_TRUE( 35 <= _party.getMember( 1 )._hp && _party.getMember( 1 )._hp <= 37 ); // 슬라임의 공격이 0..1 깎는다
    SW_EXPECT_EQUAL( 11, _party.getMember( 0 )._mp );

    _party.getMember( 1 )._hp = 55;
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "mend", 1 ) ) );
    _battle.resolveRound();
    SW_EXPECT_TRUE( _party.getMember( 1 )._hp <= 60 );

    _party.getMember( 1 )._hp = 0;
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "mend", 1 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 0, _party.getMember( 1 )._hp );

    _party.getMember( 0 )._mp = 8;
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "raise", 1 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::Revived ) );
    SW_EXPECT_TRUE( _party.getMember( 1 ).isAlive() );
}

/**
 * @brief [JRPGRulesTest] 명령 거절: 없는 멤버, 쓰러진 멤버, 모르는 주문 · 합동기, 배우지 않은 주문
 */
SW_TEST_CASE_F( JRPGRulesTest, CommandRefusals )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( _party.addMember( "mage", "Mage", "mage" ) == 1 );
    SW_REQUIRE_TRUE( startBattle( "slime" ) );

    SW_EXPECT_FALSE( _battle.setCommand( -1, sw::JRPGCommand::makeDefend() ) );
    SW_EXPECT_FALSE( _battle.setCommand( 2, sw::JRPGCommand::makeDefend() ) );
    SW_EXPECT_FALSE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "nope", 0 ) ) );
    SW_EXPECT_FALSE( _battle.setCommand( 0, sw::JRPGCommand::makeCombo( "nope", 0 ) ) );
    SW_EXPECT_FALSE( _battle.setCommand( 1, sw::JRPGCommand::makeSpell( "mend", 0 ) ) ); // 마법사는 mend 를 배우지 않았다

    _party.getMember( 1 )._hp = 0;
    SW_EXPECT_FALSE( _battle.setCommand( 1, sw::JRPGCommand::makeDefend() ) );
}

/**
 * @brief [JRPGRulesTest] 결함: 실패한 `start` 뒤에도 전투가 진행 중이라, 적이 없는 전투의 `resolveRound` 가 곧바로 승리(보상 0)였다. 시작 뒤에 든 멤버는 명령 표를 넘어 썼다.
 *        모르는 적도 말없이 빼고 시작했다 - 이제 시작을 거절한다.
 */
SW_TEST_CASE_F( JRPGRulesTest, FailedStartDoesNotProgress )
{
    SW_EXPECT_FALSE( startBattle( "slime" ) ); // 멤버가 없다

    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_EXPECT_LOG( "JRPGBattle.UnknownEnemy" );
    SW_EXPECT_FALSE( startBattle( "nope" ) );
    SW_EXPECT_FALSE( _battle.setCommand( 0, sw::JRPGCommand::makeDefend() ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::JRPGBattleResult::Ongoing, _battle.getResult() );
    SW_EXPECT_EQUAL( 0, _battle.getRound() );
    SW_EXPECT_FALSE( _battle.tryFlee() );

    sw::vector<sw::hashed_string> listNone;
    SW_EXPECT_FALSE( _battle.start( &_party, listNone ) );
    SW_EXPECT_FALSE( _battle.start( nullptr, listNone ) );

    _party.getMember( 0 )._hp = 0;
    SW_EXPECT_FALSE( startBattle( "slime" ) );
    _party.getMember( 0 )._hp = 1;

    // 시작 뒤에 멤버가 늘면 명령 표와 맞지 않는다.
    SW_REQUIRE_TRUE( startBattle( "slime" ) );
    SW_REQUIRE_TRUE( _party.addMember( "mage", "Mage", "mage" ) == 1 );
    SW_EXPECT_FALSE( _battle.setCommand( 1, sw::JRPGCommand::makeDefend() ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 0, _battle.getRound() );
}

/**
 * @brief [JRPGRulesTest] 합동기: 포인트를 쓰고 참여 멤버의 행동을 함께 쓴다. 위력 + 공격력 합 / 2, 포인트는 상한에서 멈춘다
 */
SW_TEST_CASE_F( JRPGRulesTest, ComboUsesPointsAndParticipants )
{
    _settings._comboPerHit = 3;
    _settings._comboMax    = 4;
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( _party.addMember( "mage", "Mage", "mage" ) == 1 );
    SW_REQUIRE_TRUE( startBattle( "slime" ) );

    SW_EXPECT_FALSE( _battle.setCommand( 1, sw::JRPGCommand::makeCombo( "eclipse", 0 ) ) ); // 포인트가 없다
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 3, _battle.getComboPoints() );
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 4, _battle.getComboPoints() ); // 상한

    _party.getMember( 1 )._hp = 0;
    SW_EXPECT_FALSE( _battle.setCommand( 0, sw::JRPGCommand::makeCombo( "eclipse", 0 ) ) ); // 참여 멤버가 쓰러졌다
    _party.getMember( 1 )._hp = 60;

    _listBattleEvent.clear();
    _battle.pollEvents( _listBattleEvent );
    _listBattleEvent.clear();
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    SW_EXPECT_TRUE( _battle.setCommand( 1, sw::JRPGCommand::makeCombo( "eclipse", 0 ) ) );
    _battle.resolveRound();
    const sw::vector<int32> listDamage = collectMemberDamage();
    SW_REQUIRE_EQUAL( 1u, listDamage.size() );                    // hero 의 공격은 합동기에 쓰였다
    SW_EXPECT_TRUE( 55 <= listDamage[0] && listDamage[0] <= 68 ); // 40 + (40 + 4) / 2 = 62 × 0.9..1.1
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::ComboUsed ) );
    SW_EXPECT_EQUAL( 1, _battle.getComboPoints() );
}

/**
 * @brief [JRPGRulesTest] 시전: 잠금을 깨면 위력이 (남은 수 + 1) / (전체 + 1) 로 줄어 터진다
 */
SW_TEST_CASE_F( JRPGRulesTest, CastReleaseScalesWithRemainingLocks )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( startBattle( "wyrd" ) );

    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::CastStarted ) );
    SW_EXPECT_TRUE( _battle.getEnemies()[0].isCasting() );
    SW_EXPECT_EQUAL( 2, _battle.getEnemies()[0]._lockTotal );

    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) ); // 기본 공격은 Sword 잠금을 깬다
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::LockBroken ) );
    SW_EXPECT_EQUAL( 1u, _battle.getEnemies()[0]._listLock.size() );

    const int32 loss = resolveAndMeasureHeroLoss(); // 위력 100 × 2 / 3 = 66, ×0.9..1.1
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::CastReleased ) );
    SW_EXPECT_TRUE( 59 <= loss && loss <= 72 );
    SW_EXPECT_FALSE( _battle.getEnemies()[0].isCasting() );
    SW_EXPECT_TRUE( _battle.getEnemies()[0]._listLock.empty() );
}

/**
 * @brief [JRPGRulesTest] 시전: 잠금을 모두 깨면 취소되어 터지지 않는다
 */
SW_TEST_CASE_F( JRPGRulesTest, BreakingEveryLockCancelsTheCast )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( startBattle( "wyrd" ) );

    _battle.resolveRound();
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "flare", 0 ) ) ); // Fire 잠금
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::CastCancelled ) );
    SW_EXPECT_EQUAL( 0, countBattleEvents( sw::JRPGBattleEvent::Kind::CastReleased ) );
}

/**
 * @brief [JRPGRulesTest] 도망: 민첩 차이 · 시도 횟수의 확률, 상한 0.95, 보스는 0 이고 실패하면 적만 움직인다
 */
SW_TEST_CASE_F( JRPGRulesTest, Flee )
{
    SW_REQUIRE_TRUE( _party.addMember( "mage", "Mage", "mage" ) == 0 );
    SW_REQUIRE_TRUE( startBattle( "slime" ) ); // 평균 민첩이 같다
    SW_EXPECT_NEAR( 0.5, _battle.computeFleeChance(), 1.0e-5 );

    SW_REQUIRE_TRUE( startBattle( "boss" ) );
    SW_EXPECT_NEAR( 0.0, _battle.computeFleeChance(), 1.0e-5 );
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    SW_EXPECT_FALSE( _battle.tryFlee() );
    SW_EXPECT_EQUAL( 1, _battle.getRound() );
    SW_EXPECT_EQUAL( 1000, _battle.getEnemies()[0]._hp ); // 공격 명령은 버렸다
    SW_EXPECT_EQUAL( sw::JRPGBattleResult::Ongoing, _battle.getResult() );

    _settings._fleeBase = 100.0f;
    SW_REQUIRE_TRUE( startBattle( "slime" ) );
    SW_EXPECT_NEAR( 0.95, _battle.computeFleeChance(), 1.0e-5 );
    int32 tries = 0;
    while ( tries < 32 && _battle.tryFlee() == false )
    {
        ++tries;
    }
    SW_EXPECT_EQUAL( sw::JRPGBattleResult::Fled, _battle.getResult() );
    SW_EXPECT_FALSE( _battle.tryFlee() ); // 끝난 전투
}

/**
 * @brief [JRPGRulesTest] 승리: 경험치를 살아 있는 멤버가 나눠 받고(나머지 버림), 골드는 지갑으로. 끝난 전투는 명령을 받지 않는다
 */
SW_TEST_CASE_F( JRPGRulesTest, VictoryDistributesRewards )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( _party.addMember( "mage", "Mage", "mage" ) == 1 );
    SW_REQUIRE_TRUE( startBattle( "frail" ) );
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    _battle.resolveRound();

    SW_EXPECT_EQUAL( sw::JRPGBattleResult::Victory, _battle.getResult() );
    SW_EXPECT_EQUAL( 31, _battle.getRewardExp() );
    SW_EXPECT_EQUAL( 7, _battle.getRewardGold() );
    SW_EXPECT_EQUAL( 7, _wallet.getBalance( sw::Wallet::getDefaultCurrency() ) );
    SW_EXPECT_EQUAL( 15, _party.getMember( 0 )._level.getXP() );
    SW_EXPECT_EQUAL( 15, _party.getMember( 1 )._level.getXP() );
    SW_EXPECT_EQUAL( 2, countPartyEvents( sw::JRPGPartyEvent::Kind::ExpGained ) );

    SW_EXPECT_FALSE( _battle.setCommand( 0, sw::JRPGCommand::makeDefend() ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 1, _battle.getRound() );
}

/**
 * @brief [JRPGRulesTest] 패배: 살아 있는 멤버가 없으면 끝난다
 */
SW_TEST_CASE_F( JRPGRulesTest, DefeatWhenEveryMemberFalls )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( startBattle( "giant" ) );
    _party.getMember( 0 )._hp = 1;
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::JRPGBattleResult::Defeat, _battle.getResult() );
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::Defeat ) );
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::Defeated ) );
}

/**
 * @brief [JRPGRulesTest] 무협 옵션의 내공: 때리면 +10, 맞으면 +5, 상한 100. 옵션이 꺼져 있으면 차지 않는다.
 *        결함: 쓰러지는 타격에도 내공이 찼다 - 부활하면 쓰러진 값이 남아 있었다.
 */
SW_TEST_CASE_F( JRPGRulesTest, WuxiaInnerGauge )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( startBattle( "slime" ) );
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 0, _party.getMember( 0 )._inner );

    _settings._bWuxia = true;
    SW_REQUIRE_TRUE( startBattle( "slime" ) );
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 15, _party.getMember( 0 )._inner );

    _party.getMember( 0 )._inner = 98;
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( sw::JRPGParty::kInnerMax, _party.getMember( 0 )._inner );

    _party.getMember( 0 )._inner = 0;
    _party.getMember( 0 )._hp    = 1;
    SW_REQUIRE_TRUE( startBattle( "giant" ) );
    _battle.resolveRound();
    SW_EXPECT_FALSE( _party.getMember( 0 ).isAlive() );
    SW_EXPECT_EQUAL( 0, _party.getMember( 0 )._inner );
}

/**
 * @brief [JRPGRulesTest] 초식: 숙련 단계로 열리고, 내공을 쓰며, 쓰면 숙련이 오른다.
 *        결함: 숙련은 피해 초식에만 올랐다(회복 초식은 `_proficiencyGain` 을 적어도 그대로였다). 숙련 0 을 더하는 호출은 이미 열린 초식을 다시 알렸다.
 */
SW_TEST_CASE_F( JRPGRulesTest, ManualTechniques )
{
    _settings._bWuxia = true;
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_EXPECT_FALSE( _party.getMember( 0 ).findProficiency( "pine" ).has_value() );
    SW_EXPECT_FALSE( _party.canUseSpell( 0, "pine_cut" ) );

    _party.learnManual( 0, "nope" );
    _party.addProficiency( 0, "pine", 10 ); // 익히지 않았다
    SW_EXPECT_FALSE( _party.getMember( 0 ).findProficiency( "pine" ).has_value() );

    _party.learnManual( 0, "pine" );
    SW_EXPECT_TRUE( _party.getMember( 0 ).findProficiency( "pine" ) == 0 );
    SW_EXPECT_EQUAL( 2, countPartyEvents( sw::JRPGPartyEvent::Kind::TechniqueUnlocked ) ); // 0 단계 둘
    SW_EXPECT_TRUE( _party.canUseSpell( 0, "pine_cut" ) );
    SW_EXPECT_FALSE( _party.canUseSpell( 0, "pine_storm" ) );

    _party.addProficiency( 0, "pine", 0 );
    _party.learnManual( 0, "pine" );
    SW_EXPECT_EQUAL( 2, countPartyEvents( sw::JRPGPartyEvent::Kind::TechniqueUnlocked ) ); // 다시 알리지 않는다

    SW_REQUIRE_TRUE( startBattle( "slime" ) );
    SW_EXPECT_FALSE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "pine_cut", 0 ) ) ); // 내공이 없다
    _party.getMember( 0 )._inner = 20;
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "pine_cut", 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_EQUAL( 5, _party.getMember( 0 )._inner ); // 20 을 쓰고 맞아서 5
    SW_EXPECT_TRUE( _party.getMember( 0 ).findProficiency( "pine" ) == 10 );
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::ProficiencyGained ) );

    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeSpell( "pine_mend", 0 ) ) );
    _battle.resolveRound();
    SW_EXPECT_TRUE( _party.getMember( 0 ).findProficiency( "pine" ) == 40 );
    SW_EXPECT_TRUE( _party.canUseSpell( 0, "pine_storm" ) );
    SW_EXPECT_EQUAL( 3, countPartyEvents( sw::JRPGPartyEvent::Kind::TechniqueUnlocked ) );
}

/**
 * @brief [JRPGRulesTest] 멤버 추가: 레벨까지 성장, 정원 · 겹친 id · 빈 id · 모르는 직업 거절, 범위 밖 레벨은 곡선 안으로
 */
SW_TEST_CASE_F( JRPGRulesTest, AddMember )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero", 3 ) == 0 );
    const sw::JRPGMember& hero = _party.getMember( 0 );
    SW_EXPECT_EQUAL( 120, hero.getStat( sw::JRPGStat::MaxHP ) );
    SW_EXPECT_EQUAL( 14, hero.getStat( sw::JRPGStat::MaxMP ) );
    SW_EXPECT_EQUAL( 44, hero.getStat( sw::JRPGStat::Strength ) );
    SW_EXPECT_EQUAL( 120, hero._hp );
    SW_EXPECT_EQUAL( 14, hero._mp );
    SW_EXPECT_TRUE( hero.hasLearnedSpell( "raise" ) );
    SW_EXPECT_EQUAL( 0, countPartyEvents( sw::JRPGPartyEvent::Kind::LevelUp ) ); // 처음 만든 멤버의 성장은 알리지 않는다

    SW_EXPECT_FALSE( _party.addMember( "hero", "Again", "mage" ).has_value() );
    SW_EXPECT_FALSE( _party.addMember( "", "Blank", "mage" ).has_value() );
    SW_EXPECT_FALSE( _party.addMember( "ghost", "Ghost", "nope" ).has_value() );

    SW_EXPECT_TRUE( _party.addMember( "a", "A", "mage", 99 ) == 1 );
    SW_EXPECT_EQUAL( 30, _party.getMember( 1 )._level.getLevel() );
    SW_EXPECT_TRUE( _party.addMember( "b", "B", "mage", -5 ) == 2 );
    SW_EXPECT_EQUAL( 1, _party.getMember( 2 )._level.getLevel() );
    SW_EXPECT_TRUE( _party.addMember( "c", "C", "mage" ) == 3 );
    SW_EXPECT_FALSE( _party.addMember( "d", "D", "mage" ).has_value() ); // 정원 4
    SW_EXPECT_EQUAL( 3, _party.findMemberIndex( "c" ).value() );
    SW_EXPECT_FALSE( _party.findMemberIndex( "d" ).has_value() );
}

/**
 * @brief [JRPGRulesTest] 경험치: 레벨마다 성장치와 주문, 쓰러진 멤버의 HP 는 그대로, 최고 레벨에서 멈춘다, 0 이하 · 없는 멤버는 무시
 */
SW_TEST_CASE_F( JRPGRulesTest, AddExpGrowsPerLevel )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_EXPECT_EQUAL( 0, _party.addExp( 0, 0 ) );
    SW_EXPECT_EQUAL( 0, _party.addExp( 0, -5 ) );
    SW_EXPECT_EQUAL( 0, _party.addExp( 7, 100 ) );

    SW_EXPECT_EQUAL( 2, _party.addExp( 0, 250 ) );
    const sw::JRPGMember& hero = _party.getMember( 0 );
    SW_EXPECT_EQUAL( 3, hero._level.getLevel() );
    SW_EXPECT_EQUAL( 50, hero._level.getXP() );
    SW_EXPECT_EQUAL( 120, hero._hp );
    SW_EXPECT_EQUAL( 2, countPartyEvents( sw::JRPGPartyEvent::Kind::LevelUp ) );
    SW_EXPECT_EQUAL( 1, countPartyEvents( sw::JRPGPartyEvent::Kind::LearnedSpell ) );

    _party.getMember( 0 )._hp = 0;
    SW_EXPECT_EQUAL( 1, _party.addExp( 0, 100 ) );
    SW_EXPECT_EQUAL( 0, hero._hp );
    SW_EXPECT_EQUAL( 130, hero.getStat( sw::JRPGStat::MaxHP ) );

    SW_EXPECT_EQUAL( 26, _party.addExp( 0, 1000000 ) );
    SW_EXPECT_EQUAL( 30, hero._level.getLevel() );
    SW_EXPECT_EQUAL( 0, _party.addExp( 0, 100 ) );
}

/**
 * @brief [JRPGRulesTest] 전직: 결과 일곱, 레벨 1 · 능력치 절반 · 주문 유지 · 최대 HP 는 1 이상
 */
SW_TEST_CASE_F( JRPGRulesTest, ClassChange )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero", 20 ) == 0 );
    SW_REQUIRE_TRUE( _party.addMember( "rookie", "Rookie", "mage" ) == 1 );

    SW_EXPECT_EQUAL( sw::JRPGClassChangeResult::UnknownMember, _party.changeClass( 9, "mage" ) );
    SW_EXPECT_EQUAL( sw::JRPGClassChangeResult::UnknownClass, _party.changeClass( 0, "nope" ) );
    SW_EXPECT_EQUAL( sw::JRPGClassChangeResult::SameClass, _party.changeClass( 0, "hero" ) );
    SW_EXPECT_EQUAL( sw::JRPGClassChangeResult::LevelTooLow, _party.changeClass( 1, "hero" ) );
    SW_EXPECT_EQUAL( sw::JRPGClassChangeResult::MissingItem, _party.changeClass( 0, "sage" ) );
    _party.getMember( 0 )._hp = 0;
    SW_EXPECT_EQUAL( sw::JRPGClassChangeResult::Dead, _party.changeClass( 0, "mage" ) );
    _party.getMember( 0 )._hp = 290;

    SW_EXPECT_EQUAL( 1, _inventory.addItem( "sagebook", 1 ) );
    SW_EXPECT_EQUAL( sw::JRPGClassChangeResult::Ok, _party.changeClass( 0, "sage" ) );
    const sw::JRPGMember& hero = _party.getMember( 0 );
    SW_EXPECT_EQUAL( 1, hero._level.getLevel() );
    SW_EXPECT_EQUAL( 145, hero.getStat( sw::JRPGStat::MaxHP ) ); // 100 + 19 × 10 = 290 의 절반
    SW_EXPECT_EQUAL( 145, hero._hp );
    SW_EXPECT_EQUAL( 24, hero._mp ); // 10 + 19 × 2 = 48 의 절반
    SW_EXPECT_EQUAL( 39, hero.getStat( sw::JRPGStat::Strength ) );
    SW_EXPECT_TRUE( hero.hasLearnedSpell( "raise" ) );

    _party.getMember( 1 )._arrStat[static_cast<size_t>( sw::JRPGStat::MaxHP )] = 1;
    _party.getMember( 1 )._hp                                                  = 1;
    SW_EXPECT_EQUAL( sw::JRPGClassChangeResult::Ok, _party.changeClass( 1, "hero", 1 ) );
    SW_EXPECT_EQUAL( 1, _party.getMember( 1 ).getStat( sw::JRPGStat::MaxHP ) );
    SW_EXPECT_EQUAL( 1, _party.getMember( 1 )._hp );
}

/**
 * @brief [JRPGRulesTest] 여관 · 교회: 값, 돈이 모자라면 그대로, 음수 값은 공짜, 지갑이 없으면 값이 있는 곳을 못 쓴다
 */
SW_TEST_CASE_F( JRPGRulesTest, InnAndChurch )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( _party.addMember( "mage", "Mage", "mage", 2 ) == 1 );
    _party.getMember( 0 )._hp        = 10;
    _party.getMember( 0 )._mp        = 1;
    _party.getMember( 1 )._hp        = 0;
    const sw::hashed_string currency = sw::Wallet::getDefaultCurrency();

    SW_EXPECT_FALSE( _party.restAtInn( 10 ) );
    SW_EXPECT_EQUAL( 10, _party.getMember( 0 )._hp );

    _wallet.add( currency, 15 );
    SW_EXPECT_TRUE( _party.restAtInn( 10 ) ); // 살아 있는 멤버 하나분
    SW_EXPECT_EQUAL( 5, _wallet.getBalance( currency ) );
    SW_EXPECT_EQUAL( 100, _party.getMember( 0 )._hp );
    SW_EXPECT_EQUAL( 10, _party.getMember( 0 )._mp );
    SW_EXPECT_EQUAL( 0, _party.getMember( 1 )._hp );
    SW_EXPECT_TRUE( _party.restAtInn( -3 ) );
    SW_EXPECT_EQUAL( 5, _wallet.getBalance( currency ) );

    SW_EXPECT_FALSE( _party.reviveAtChurch( 9, 1 ) );
    SW_EXPECT_FALSE( _party.reviveAtChurch( 0, 1 ) ); // 살아 있다
    SW_EXPECT_FALSE( _party.reviveAtChurch( 1, 3 ) ); // 레벨 2 × 3 = 6
    SW_EXPECT_FALSE( _party.getMember( 1 ).isAlive() );
    SW_EXPECT_TRUE( _party.reviveAtChurch( 1, 2 ) );
    SW_EXPECT_EQUAL( 1, _wallet.getBalance( currency ) );
    SW_EXPECT_EQUAL( _party.getMember( 1 ).getStat( sw::JRPGStat::MaxHP ), _party.getMember( 1 )._hp );

    sw::JRPGParty unfunded;
    unfunded.initialize( &_catalog, &_itemCatalog, sw::SessionStateRefs{} );
    SW_REQUIRE_TRUE( unfunded.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_EXPECT_FALSE( unfunded.restAtInn( 1 ) );
    SW_EXPECT_TRUE( unfunded.restAtInn( 0 ) );
    SW_EXPECT_EQUAL( 100, unfunded.distributeRewards( 100, 5 ) ); // 지갑이 없어도 경험치는 받는다
}

/**
 * @brief [JRPGRulesTest] 장비: 공격력 = 힘 + attack, 방어력 = 체력 / 2 + defense, 상태 왕복에 남는다. 없는 멤버는 0
 */
SW_TEST_CASE_F( JRPGRulesTest, EquipmentStatsAndRoundTrip )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_EXPECT_EQUAL( 40, _party.computeAttack( 0 ) );
    SW_EXPECT_EQUAL( 10, _party.computeDefense( 0 ) );
    SW_EXPECT_EQUAL( 0, _party.computeAttack( 9 ) );
    SW_EXPECT_EQUAL( 0, _party.computeDefense( -1 ) );

    sw::InventorySlot blade;
    blade._itemID = "blade";
    blade._count  = 1;
    sw::InventorySlot mail;
    mail._itemID = "mail";
    mail._count  = 1;
    sw::vector<sw::InventorySlot> listRemoved;
    SW_EXPECT_EQUAL( sw::EquipResult::Ok, _party.getMember( 0 )._equipment.equip( "Weapon", blade, listRemoved ) );
    SW_EXPECT_EQUAL( sw::EquipResult::Ok, _party.getMember( 0 )._equipment.equip( "Armor", mail, listRemoved ) );
    SW_EXPECT_EQUAL( 52, _party.computeAttack( 0 ) );
    SW_EXPECT_EQUAL( 14, _party.computeDefense( 0 ) );

    sw::Archive written;
    _party.writeState( written );
    sw::Archive   reading( written.getData(), written.getSize() );
    sw::JRPGParty restored;
    initializeParty( restored );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 52, restored.computeAttack( 0 ) );
    SW_EXPECT_EQUAL( 14, restored.computeDefense( 0 ) );
    SW_EXPECT_TRUE( JRPGTestUtil::isSameState( _party, restored ) );
}

/**
 * @brief [JRPGRulesTest] 파티 상태 왕복: 레벨 · 경험치 · 주문 · 비급 · HP · MP · 내공이 그대로이고, 이벤트는 비운다
 */
SW_TEST_CASE_F( JRPGRulesTest, PartyStateRoundTrip )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero", 3 ) == 0 );
    SW_REQUIRE_TRUE( _party.addMember( "mage", "Mage", "mage" ) == 1 );
    _party.learnManual( 0, "pine" );
    _party.addProficiency( 0, "pine", 35 );
    SW_EXPECT_EQUAL( 0, _party.addExp( 0, 50 ) );
    _party.getMember( 0 )._hp    = 33;
    _party.getMember( 0 )._mp    = 3;
    _party.getMember( 0 )._inner = 44;

    sw::Archive written;
    _party.writeState( written );
    sw::Archive   reading( written.getData(), written.getSize() );
    sw::JRPGParty restored;
    initializeParty( restored );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( JRPGTestUtil::isSameState( _party, restored ) );
    SW_EXPECT_TRUE( restored.getMember( 0 ).findProficiency( "pine" ) == 35 );
    SW_EXPECT_EQUAL( 33, restored.getMember( 0 )._hp );
    SW_EXPECT_EQUAL( 50, restored.getMember( 0 )._level.getXP() );

    restored.pollEvents( _listPartyEvent );
    SW_EXPECT_TRUE( _listPartyEvent.empty() );
}

/**
 * @brief [JRPGRulesTest] 결함: 파티 상태가 값을 보지 않았다. 음수 · 최대를 넘는 HP / MP / 내공, 음수 능력치, 최대 HP 0, 모르는 주문 · 비급, 겹친 주문 · 비급 · 멤버 id, 곡선을 넘는 레벨을 그대로 받았다. 거절하고 상태를 두는지 본다
 */
SW_TEST_CASE_F( JRPGRulesTest, PartyStateRefusals )
{
    const auto refuses = [this]( auto mutate )
    {
        sw::JRPGParty source;
        initializeParty( source );
        (void)source.addMember( "hero", "Hero", "hero" );
        (void)source.addMember( "mage", "Mage", "mage" );
        source.learnManual( 0, "pine" );
        mutate( source.getMember( 0 ), source.getMember( 1 ) );
        sw::Archive written;
        source.writeState( written );
        sw::Archive   reading( written.getData(), written.getSize() );
        sw::JRPGParty target;
        initializeParty( target );
        return target.readState( reading ) == false && target.getMemberCount() == 0;
    };
    const auto keep = []( sw::JRPGMember&, sw::JRPGMember& ) {};
    SW_EXPECT_FALSE( refuses( keep ) );

    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._hp = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._hp = 101; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._mp = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._mp = 11; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._inner = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._inner = sw::JRPGParty::kInnerMax + 1; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._arrStat[static_cast<size_t>( sw::JRPGStat::Luck )] = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    {
        hero._arrStat[static_cast<size_t>( sw::JRPGStat::MaxHP )] = 0;
        hero._hp                                                  = 0;
    } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._classID = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._listSpell.push_back( "nope" ); } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._listSpell.push_back( "flare" ); } ) ); // 이미 배웠다
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._listManual[0]._proficiency = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._listManual[0]._manualID = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    {
        const sw::JRPGManualProgress again = hero._listManual[0];
        hero._listManual.push_back( again );
    } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& mage )
    { mage._id = hero._id; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    { hero._id = sw::hashed_string{}; } ) );
    SW_EXPECT_TRUE( refuses( []( sw::JRPGMember& hero, sw::JRPGMember& )
    {
        sw::ExperienceCurve longer;
        (void)longer.setFormula( 100.0f, 0.0f, 0.0f, 99 );
        hero._level.setLevel( longer, 50 ); // 이 카탈로그의 최고는 30
    } ) );

    // 정원을 넘는 멤버 수, 장비 슬롯 구성이 다른 상태.
    sw::Archive oversized;
    oversized << static_cast<uint32>( sw::JRPGParty::kMaxMembers + 1 );
    for ( int32 pad = 0; pad < 128; ++pad )
    {
        oversized << static_cast<uint32>( 0 );
    }
    sw::Archive   readingOversized( oversized.getData(), oversized.getSize() );
    sw::JRPGParty target;
    initializeParty( target );
    SW_EXPECT_FALSE( target.readState( readingOversized ) );

    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    sw::Archive written;
    _party.writeState( written );
    sw::Archive   readingLayout( written.getData(), written.getSize() );
    sw::JRPGParty otherLayout;
    otherLayout.initialize( &_catalog, &_itemCatalog, sw::SessionStateRefs{}, "Weapon" );
    SW_EXPECT_FALSE( otherLayout.readState( readingLayout ) );
}

/**
 * @brief [JRPGRulesTest] 전투 상태 왕복: 읽은 전투가 같은 명령에 같은 수열을 이어 간다
 */
SW_TEST_CASE_F( JRPGRulesTest, BattleStateRoundTrip )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( startBattle( "wyrd" ) );
    _battle.resolveRound();
    SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeDefend() ) ); // 정한 명령도 싣는다

    sw::Archive writtenParty;
    sw::Archive writtenBattle;
    _party.writeState( writtenParty );
    _battle.writeState( writtenBattle );

    sw::Archive   readingParty( writtenParty.getData(), writtenParty.getSize() );
    sw::JRPGParty restoredParty;
    initializeParty( restoredParty );
    SW_REQUIRE_TRUE( restoredParty.readState( readingParty ) );
    sw::Archive    readingBattle( writtenBattle.getData(), writtenBattle.getSize() );
    sw::JRPGBattle restored;
    restored.initialize( &_catalog, &_judge, _settings, 99 );
    restored.setTimingInput( &_timing );
    restored.bindParty( &restoredParty );
    SW_REQUIRE_TRUE( restored.readState( readingBattle ) );
    SW_EXPECT_TRUE( JRPGTestUtil::isSameState( _battle, restored ) );

    for ( int32 round = 0; round < 4; ++round )
    {
        SW_EXPECT_TRUE( _battle.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
        SW_EXPECT_TRUE( restored.setCommand( 0, sw::JRPGCommand::makeAttack( 0 ) ) );
        _battle.resolveRound();
        restored.resolveRound();
    }
    SW_EXPECT_TRUE( JRPGTestUtil::isSameState( _battle, restored ) );
    SW_EXPECT_TRUE( JRPGTestUtil::isSameState( _party, restoredParty ) );

    // 시작 전 전투는 파티 없이 왕복한다.
    sw::JRPGBattle fresh;
    fresh.initialize( &_catalog, &_judge, _settings, 5 );
    sw::Archive writtenFresh;
    fresh.writeState( writtenFresh );
    sw::Archive    readingFresh( writtenFresh.getData(), writtenFresh.getSize() );
    sw::JRPGBattle freshTarget;
    freshTarget.initialize( &_catalog, &_judge, _settings, 6 );
    SW_EXPECT_TRUE( freshTarget.readState( readingFresh ) );
}

/**
 * @brief [JRPGRulesTest] 결함: 전투 상태가 적과 진행 값을 보지 않았다. 잠금 전체 -1 은 시전을 터뜨릴 때 0 으로 나눴고, 음수 · 최대를 넘는 HP, 음수 도망 횟수(확률을 깎는다), 음수 보상, 상한을 넘는 콤보, 모르는 주문 명령을 받았다
 */
SW_TEST_CASE_F( JRPGRulesTest, BattleStateRefusals )
{
    SW_REQUIRE_TRUE( _party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_REQUIRE_TRUE( startBattle( "slime" ) );

    const auto refuses = [this]( auto mutate )
    {
        JRPGFixtureBattle forged;
        mutate( forged );
        return readForged( forged ) == false && _battle.getEnemies()[0]._hp == 1000;
    };
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._hp = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._hp = 1001; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._enemyID = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._castTurnsLeft = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._turnsTaken = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._lockTotal = -1; } ) ); // 0 으로 나누는 값
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._lockTotal = 1; } ) ); // 슬라임은 잠금이 없다
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    {
        forged._enemyID       = "wyrd";
        forged._castTurnsLeft = 3; // 이 적의 시전은 2 차례다
        forged._lockTotal     = 2;
    } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    {
        forged._enemyID   = "wyrd";
        forged._lockCount = 2; // 시전 중이 아닌데 잠금이 남았다
        forged._lockTotal = 2;
    } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._commandCount = 2; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._commandKind = 9; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._defending = 2; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    {
        forged._commandKind = static_cast<uint8>( sw::JRPGCommandKind::Spell );
        forged._commandID   = "nope";
    } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    {
        forged._commandKind = static_cast<uint8>( sw::JRPGCommandKind::Combo );
        forged._commandID   = "nope";
    } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._comboPoints = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._comboPoints = 10; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._round = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._fleeAttempts = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._rewardExp = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._rewardGold = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( JRPGFixtureBattle& forged )
    { forged._result = 9; } ) );

    // 맞는 시전 상태는 읽히고, 다음 라운드에 터진다.
    JRPGFixtureBattle casting;
    casting._enemyID       = "wyrd";
    casting._lockCount     = 1;
    casting._lockTotal     = 2;
    casting._castTurnsLeft = 1;
    SW_REQUIRE_TRUE( readForged( casting ) );
    SW_EXPECT_EQUAL( 500, _battle.getEnemies()[0]._hp );
    _battle.resolveRound(); // 읽은 턴 순서가 비어 있어 syncActors 가 배우를 다시 넣는다
    SW_EXPECT_EQUAL( 1, countBattleEvents( sw::JRPGBattleEvent::Kind::CastReleased ) );
}

/**
 * @brief [JRPGRulesTest] 걸음 조우: 유예 걸음 동안 굴리지 않고, 끄는 값 · 모르는 지역 · 무리 없음 · 가중치 0 은 조우가 없고, 걸음은 센다
 */
SW_TEST_CASE_F( JRPGRulesTest, StepEncounter )
{
    sw::JRPGEncounterWalker walker;
    walker.initialize( &_catalog, 5 );
    SW_EXPECT_EQUAL( sw::JRPGEncounterWalker::kNoEncounterYet, walker.getStepsSinceEncounter() );

    const sw::JRPGEncounterGroup* pGroup = walker.step( "field" );
    SW_REQUIRE_TRUE( pGroup != nullptr );
    SW_EXPECT_EQUAL( 2u, pGroup->_listEnemyID.size() );
    SW_EXPECT_EQUAL( 0, walker.getStepsSinceEncounter() );
    SW_EXPECT_TRUE( walker.step( "field" ) == nullptr ); // 유예 1
    SW_EXPECT_TRUE( walker.step( "field" ) == nullptr ); // 유예 2
    SW_EXPECT_TRUE( walker.step( "field" ) != nullptr );

    walker.restartGrace();
    SW_EXPECT_TRUE( walker.step( "field" ) == nullptr );

    const int32 stepsBefore = walker.getTotalSteps();
    SW_EXPECT_TRUE( walker.step( "quiet" ) == nullptr );
    SW_EXPECT_TRUE( walker.step( "nope" ) == nullptr );
    SW_EXPECT_TRUE( walker.step( "empty" ) == nullptr );
    SW_EXPECT_TRUE( walker.step( "weightless" ) == nullptr );
    SW_EXPECT_EQUAL( stepsBefore + 4, walker.getTotalSteps() );

    sw::JRPGEncounterWalker noCatalog;
    noCatalog.initialize( nullptr, 5 );
    SW_EXPECT_TRUE( noCatalog.step( "field" ) == nullptr );
}

/**
 * @brief [JRPGRulesTest] 걸음 조우 상태: 왕복하면 같은 걸음에 같은 무리가 나오고, 음수 걸음은 거절한다
 */
SW_TEST_CASE_F( JRPGRulesTest, StepEncounterState )
{
    sw::JRPGEncounterWalker walker;
    walker.initialize( &_catalog, 5 );
    (void)walker.step( "field" );

    sw::Archive written;
    walker.writeState( written );
    sw::Archive             reading( written.getData(), written.getSize() );
    sw::JRPGEncounterWalker restored;
    restored.initialize( &_catalog, 77 );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( JRPGTestUtil::isSameState( walker, restored ) );
    for ( int32 step = 0; step < 8; ++step )
    {
        SW_EXPECT_EQUAL( walker.step( "field" ) == nullptr, restored.step( "field" ) == nullptr );
    }
    SW_EXPECT_TRUE( JRPGTestUtil::isSameState( walker, restored ) );

    sw::GameRandom random;
    random.setSeed( 3 );
    const auto refuses = [&]( int32 stepsSince, int32 total )
    {
        sw::Archive forged;
        sw::StateIO::writeRandom( forged, random );
        forged << stepsSince;
        forged << total;
        sw::Archive readingFixture( forged.getData(), forged.getSize() );
        return restored.readState( readingFixture ) == false;
    };
    SW_EXPECT_TRUE( refuses( -1, 0 ) );
    SW_EXPECT_TRUE( refuses( 0, -1 ) );
    SW_EXPECT_TRUE( refuses( sw::JRPGEncounterWalker::kNoEncounterYet + 1, 0 ) );
    SW_EXPECT_FALSE( refuses( 3, 9 ) );
}

/**
 * @brief [JRPGTest] 같은 곡선과 정의: 능력치 일곱, 비급 단계, 지역, 예시와 같은 형식으로 읽는다
 */
SW_TEST_CASE( JRPGTest, CatalogReadsDefinitions )
{
    sw::JRPGCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kJRPGXML, "JRPGTest" ) );
    SW_EXPECT_EQUAL( 30, catalog.getCurve().getMaxLevel() );
    SW_EXPECT_EQUAL( 100, catalog.getCurve().getXPToNext( 1 ) );

    const sw::JRPGClassDef* pHero = catalog.findClass( "hero" );
    SW_REQUIRE_TRUE( pHero != nullptr );
    SW_EXPECT_EQUAL( 100, pHero->_arrBase[static_cast<size_t>( sw::JRPGStat::MaxHP )] );
    SW_EXPECT_EQUAL( 10, pHero->_arrGrowth[static_cast<size_t>( sw::JRPGStat::MaxHP )] );
    SW_EXPECT_EQUAL( 4u, pHero->_listLearn.size() );

    const sw::JRPGEnemyDef* pWyrd = catalog.findEnemy( "wyrd" );
    SW_REQUIRE_TRUE( pWyrd != nullptr );
    SW_EXPECT_EQUAL( 2u, pWyrd->_listLock.size() );
    SW_EXPECT_EQUAL( 2, pWyrd->_castTurns );
    SW_EXPECT_TRUE( catalog.findEnemy( "boss" )->_bBoss );
    SW_EXPECT_EQUAL( sw::JRPGSpellKind::Heal, catalog.findSpell( "mend" )->_kind );
    SW_EXPECT_EQUAL( sw::JRPGTargetKind::All, catalog.findSpell( "blaze" )->_target );
    SW_EXPECT_EQUAL( 3u, catalog.findManual( "pine" )->_listStage.size() );
    SW_EXPECT_NEAR( 1.0, catalog.findArea( "field" )->_rate, 1.0e-5 );

    catalog.clear();
    SW_EXPECT_TRUE( catalog.findClass( "hero" ) == nullptr );
}

/**
 * @brief [JRPGTest] 결함: 손으로 읽는 원소(직업 · 합동기 · 비급 · 적 · 지역)는 겹친 id 가 앞 정의를 말없이 덮어썼다(주석은 "알리고 버린다"). 원소 하나를 넣지 않고 오류다
 */
SW_TEST_CASE( JRPGTest, DuplicateIDKeepsTheFirstDefinition )
{
    sw::JRPGCatalog catalog;
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<JRPGCatalog>
  <Enemy id="slime" name="First" hp="10"/><Enemy id="slime" name="Second" hp="20"/>
  <Class id="hero" name="First" hp="10"/><Class id="hero" name="Second" hp="20"/>
  <Combo id="c" name="First" members="a,b" power="1"/><Combo id="c" name="Second" members="a,b" power="2"/>
  <Manual id="m" name="First"/><Manual id="m" name="Second"/>
  <Area id="a" rate="0.5"/><Area id="a" rate="0.25"/>
</JRPGCatalog>)",
                                              "JRPGTest" ) );
    SW_EXPECT_EQUAL( 10, catalog.findEnemy( "slime" )->_arrStat[static_cast<size_t>( sw::JRPGStat::MaxHP )] );
    SW_EXPECT_EQUAL( 10, catalog.findClass( "hero" )->_arrBase[static_cast<size_t>( sw::JRPGStat::MaxHP )] );
    SW_EXPECT_EQUAL( 1, catalog.findCombo( "c" )->_power );
    SW_EXPECT_NEAR( 0.5, catalog.findArea( "a" )->_rate, 1.0e-5 );
}

/**
 * @brief [JRPGTest] 깨진 원소: 음수 능력치 · HP 0 · 멤버 하나인 합동기 · 빈 무리 · 모르는 원소는 그 원소를 넣지 않고 오류다
 */
SW_TEST_CASE( JRPGTest, CatalogRefusesBrokenElements )
{
    sw::JRPGCatalog catalog;
    SW_EXPECT_LOG( "JRPGCatalog.NeedsIntegerMore" );
    SW_EXPECT_LOG( "JRPGCatalog.IdNeedsHpMore" );
    SW_EXPECT_LOG( "JRPGCatalog.ComboNeedsTwoMore" );
    SW_EXPECT_LOG( "JRPGCatalog.AreaGroupWithoutEnemies" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<JRPGCatalog>
  <Enemy id="good" name="Good"/>
  <Enemy id="negative" name="Negative" str="-1"/>
  <Enemy id="deadborn" name="Deadborn" hp="0"/>
  <Class id="deadclass" name="DeadClass" hp="0"/>
  <Combo id="solo" name="Solo" members="a"/>
  <Area id="hollow"><Group weight="1"/></Area>
  <Dragon id="x"/>
  <Spell id="unnamed"/>
  <Spell id="typo" name="Typo" mpp="1"/>
  <Spell id="cheap" name="Cheap" mp="-1"/>
</JRPGCatalog>)",
                                              "JRPGTest" ) );
    SW_EXPECT_TRUE( catalog.findEnemy( "good" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findEnemy( "negative" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findEnemy( "deadborn" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findClass( "deadclass" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findCombo( "solo" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findArea( "hollow" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findSpell( "unnamed" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findSpell( "typo" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findSpell( "cheap" ) == nullptr );
}

/**
 * @brief [JRPGTest] 서로 가리키는 id: 모르는 주문 · 초식 · 적을 읽은 뒤 알린다.
 *        결함: 적의 시전 주문과 주문의 비급은 보지 않았다 - 오타가 위력 0 으로 터지는 시전, 영원히 열리지 않는 초식이 되었다.
 */
SW_TEST_CASE( JRPGTest, CatalogReportsUnknownReferences )
{
    sw::JRPGCatalog catalog;
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<JRPGCatalog>
  <Class id="hero" name="Hero"><Learn spell="nope"/></Class>
  <Manual id="m" name="M"><Stage technique="nope"/></Manual>
  <Area id="a"><Group enemies="nope"/></Area>
  <Enemy id="e" name="E" cast="nope"/>
  <Spell id="s" name="S" manual="nope"/>
</JRPGCatalog>)",
                                              "JRPGTest" ) );

    // 모르는 주문은 배우지 않는다 - 배우면 그 멤버의 세이브가 읽히지 않는다.
    sw::JRPGParty party;
    party.initialize( &catalog, nullptr, sw::SessionStateRefs{} );
    SW_REQUIRE_TRUE( party.addMember( "hero", "Hero", "hero" ) == 0 );
    SW_EXPECT_TRUE( party.getMember( 0 )._listSpell.empty() );
}
