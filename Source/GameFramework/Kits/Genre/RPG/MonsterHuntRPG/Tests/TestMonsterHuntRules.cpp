#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Object/GameObject/Tests/SceneWalkTestAccess.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Catalog/MonsterHuntCatalog.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntAlchemy.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntBestiary.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntCombat.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntContract.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntMutagens.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/SkillTree.h"
#include "GameFramework/Runtime/Character/Combat/Damage/ElementChart.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

#include <limits>

// 위쳐 키트의 규칙: 연금술(독성 상한 · 묶인 독성 · 오일 · 명상), 도감(지식 단계), 전투(스태미나 · 표식 · 아드레날린), 계약 조사(순서 · 단서 없는 단계), 흥정(분노 · 한도 · 큰 요구),
// 변이 슬롯, 스킬 트리(층 되짚기), 카탈로그 오류, 상태 바이트의 범위 검사. 상태 바이트는 형식을 맞춰 손으로 쓴 뒤(정상 값이 읽히는 것을 함께 본다) 범위 밖 값만 바꿔 넣는다. 키트 GF_MonsterHuntRPG(nogpu).

namespace
{
    constexpr const utf8* kMonsterHuntXML = R"(<MonsterHuntCatalog>
  <Alchemy maxToxicity="100" toxicityDecay="2" alcohol="spirit,gull"/>
  <Combat stamina="100" regen="10" delay="1" fast="0" strong="10" dodge="5" roll="10" adrenalineMax="3" adrenalinePerHit="0.1" adrenalineLoss="1" adrenalineBonus="0.1" intensityStat="signIntensity"/>
  <Monster id="ghoul" category="Necrophage" maxKnowledge="3" readLevel="3" killsPerLevel="2" killCap="2" investigateLevel="1">
    <Weakness kind="Oil" id="necro_oil" knowledge="1"/>
    <Weakness kind="Sign" id="igni" knowledge="2"/>
    <Weakness kind="Bomb" id="grapeshot" knowledge="3"/>
  </Monster>
  <Item id="swallow" kind="Potion" toxicity="20" duration="10" charges="2"/>
  <Item id="thunder" kind="Potion" toxicity="80" duration="10" charges="1"/>
  <Item id="decoct" kind="Decoction" toxicity="70" duration="30" charges="2"/>
  <Item id="necro_oil" kind="Oil" element="Silver" hits="2" charges="1"/>
  <Item id="beast_oil" kind="Oil" element="Beast" hits="3" charges="1"/>
  <Item id="grapeshot" kind="Bomb" element="Fire" charges="2"/>
  <Sign id="igni" element="Fire" cost="30" altCost="80" power="20" altPowerScale="1.5" altSkill="firestream"/>
  <Sign id="quen" cost="0" altCost="0" power="5"/>
  <SkillColor skill="red_a" color="Red"/>
  <SkillColor skill="red_b" color="Red"/>
  <SkillColor skill="blue_a" color="Blue"/>
  <Mutagen id="red_m" color="Red" stat="attackPower" value="5" matchValue="2"/>
  <SlotGroup level="1" slots="2"/>
  <SlotGroup level="5" slots="1"/>
  <Contract id="griffin" reward="100" limit="1.5" angerMax="1" angerScale="1" angerPerRound="0.1">
    <Step id="tracks">
      <Clue id="blood" position="0,0,0" radius="2" order="0"/>
      <Clue id="feather" position="10,0,0" radius="2" order="1"/>
    </Step>
    <Step id="nest"><Clue id="eggs" position="20,0,0" radius="1" order="0"/></Step>
  </Contract>
  <Contract id="interview" reward="100">
    <Step id="talk"/>
    <Step id="search"><Clue id="note" position="0,0,0" radius="1"/></Step>
    <Step id="report"/>
  </Contract>
  <Contract id="no_steps" reward="100"/>
  <Contract id="calm" reward="100" limit="1.5" angerMax="1" angerScale="0" angerPerRound="0"/>
  <Contract id="rounds" reward="100" limit="1" angerMax="1" angerScale="0" angerPerRound="0.25"/>
  <Contract id="free" reward="0" limit="1" angerMax="1" angerScale="1" angerPerRound="0.1"/>
</MonsterHuntCatalog>)";

    constexpr const utf8* kMonsterHuntItemXML = R"(<ItemCatalog>
  <Item id="herb" maxStack="50"/>
  <Item id="spirit" maxStack="50"/>
  <Item id="gull" maxStack="50"/>
  <Item id="swallow" maxStack="10"/>
  <Item id="thunder" maxStack="10"/>
  <Item id="decoct" maxStack="10"/>
  <Item id="necro_oil" maxStack="10"/>
  <Item id="beast_oil" maxStack="10"/>
  <Item id="grapeshot" maxStack="10"/>
</ItemCatalog>)";

    constexpr const utf8* kMonsterHuntRecipeXML = R"(<RecipeCatalog>
  <Recipe id="make_swallow"><In item="herb"/><Out item="swallow"/></Recipe>
  <Recipe id="make_thunder"><In item="herb"/><Out item="thunder"/></Recipe>
  <Recipe id="make_decoct"><In item="herb"/><Out item="decoct"/></Recipe>
  <Recipe id="make_necro_oil"><In item="herb"/><Out item="necro_oil"/></Recipe>
  <Recipe id="make_beast_oil"><In item="herb"/><Out item="beast_oil"/></Recipe>
  <Recipe id="make_grapeshot"><In item="herb"/><Out item="grapeshot"/></Recipe>
</RecipeCatalog>)";

    constexpr const utf8* kSkillTreeXML = R"(<SkillTreeCatalog><Tree id="combat">
  <Skill id="base" maxRank="2" cost="1"><Stats attack="5"/></Skill>
  <Skill id="power" cost="2" level="3" requires="base:2" ability="PowerStrike"/>
  <Skill id="stance_a" cost="1" spent="2" group="stance"/>
  <Skill id="stance_b" cost="1" spent="2" group="stance"/>
</Tree><Tree id="tiers">
  <Skill id="floor" cost="1"/>
  <Skill id="upper_a" cost="1" spent="1"/>
  <Skill id="upper_b" cost="1" spent="1"/>
</Tree></SkillTreeCatalog>)";

    constexpr float32 kMonsterHuntTolerance = 1.0e-3f;

    /** @brief 카탈로그 · 인벤토리 · 연금술을 갖춘 판입니다. 재료는 넉넉합니다. */
    struct MonsterHuntAlchemyTest
    {
        sw::MonsterHuntCatalog _catalog{};
        sw::ItemCatalog        _itemCatalog{};
        sw::RecipeCatalog      _recipeCatalog{};
        sw::Inventory          _inventory{};
        sw::MonsterHuntAlchemy _alchemy{};

        MonsterHuntAlchemyTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kMonsterHuntXML, "MonsterHuntTest" ) );
            SW_REQUIRE_TRUE( _itemCatalog.loadFromXMLText( kMonsterHuntItemXML, "MonsterHuntTest" ) );
            SW_REQUIRE_TRUE( _recipeCatalog.loadFromXMLText( kMonsterHuntRecipeXML, "MonsterHuntTest" ) );
            SW_REQUIRE_TRUE( _inventory.initialize( &_itemCatalog, 16 ) );
            SW_EXPECT_EQUAL( 40, _inventory.addItem( "herb", 40 ) );
            _alchemy.initialize( &_catalog, &_recipeCatalog );
        }

        void brew( const sw::hashed_string& recipeID ) { SW_EXPECT_EQUAL( sw::CraftResult::Ok, _alchemy.brew( recipeID, _inventory, sw::hashed_string{}, 1 ) ); }

        /** @brief 묶음 하나씩(충전 · 효과)을 손으로 쓴 상태를 읽습니다. 형식이 맞으면 결과는 값의 범위만 가른다. */
        bool readForged( int32 charges, float32 lockedToxicity, const sw::hashed_string& oilID, int32 oilHits )
        {
            sw::Archive written;
            _alchemy.getCrafter().writeState( written );
            written << static_cast<uint32>( 1 );
            sw::StateIO::writeName( written, "swallow" );
            written << charges;
            written << static_cast<uint32>( 1 );
            sw::StateIO::writeName( written, "decoct" );
            sw::StateIO::writeCountdown( written, sw::Countdown( 5.0f ) );
            written << lockedToxicity;
            sw::StateIO::writeName( written, oilID );
            written << 0.0f;
            written << oilHits;
            sw::Archive reading( written.getData(), written.getSize() );
            return _alchemy.readState( reading );
        }
    };

    /** @brief 전투 하나와 표식에 쓰는 상성 테이블 · 능력치입니다. */
    struct MonsterHuntCombatTest
    {
        sw::MonsterHuntCatalog _catalog{};
        sw::ElementChart       _chart{};
        sw::StatBlock          _stats{};
        sw::MonsterHuntCombat  _combat{};

        MonsterHuntCombatTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kMonsterHuntXML, "MonsterHuntTest" ) );
            _chart.addElement( "Fire" );
            sw::ElementStatusChance burn;
            burn._element = "Fire";
            burn._status  = "Burn";
            burn._chance  = 0.5f;
            _chart.addStatusChance( burn );
            SW_REQUIRE_TRUE( _combat.initialize( &_catalog, 7 ) );
        }

        bool readForged( float32 adrenaline )
        {
            sw::GameRandom random;
            random.setSeed( 7 );
            sw::Archive written;
            _combat.getStamina().writeState( written );
            sw::StateIO::writeRandom( written, random );
            written << adrenaline;
            sw::Archive reading( written.getData(), written.getSize() );
            return _combat.readState( reading );
        }
    };

    /** @brief 계약 조사의 판입니다. */
    struct MonsterHuntContractTest
    {
        sw::MonsterHuntCatalog _catalog{};

        MonsterHuntContractTest() { SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kMonsterHuntXML, "MonsterHuntTest" ) ); }

        static sw::vector<sw::MonsterHuntInvestigationEvent> pollAll( sw::MonsterHuntInvestigation& investigation )
        {
            sw::vector<sw::MonsterHuntInvestigationEvent> listEvent;
            investigation.pollEvents( listEvent );
            return listEvent;
        }

        static bool readForgedInvestigation( sw::MonsterHuntInvestigation& investigation, const sw::hashed_string& contractID, int32 stepIndex, const sw::vector<sw::hashed_string>& listFound )
        {
            sw::Archive written;
            sw::StateIO::writeName( written, contractID );
            written << stepIndex;
            written << static_cast<uint32>( listFound.size() );
            for ( const sw::hashed_string& clueID : listFound )
            {
                sw::StateIO::writeName( written, clueID );
            }
            sw::Archive reading( written.getData(), written.getSize() );
            return investigation.readState( reading );
        }

        static bool readForgedHaggle( sw::MonsterHuntHaggle& haggle, const sw::hashed_string& contractID, float32 anger, int32 offer, int32 finalReward, uint8 bClosed )
        {
            sw::Archive written;
            sw::StateIO::writeName( written, contractID );
            written << anger;
            written << offer;
            written << finalReward;
            written << bClosed;
            sw::Archive reading( written.getData(), written.getSize() );
            return haggle.readState( reading );
        }
    };

    /** @brief 변이 슬롯의 판입니다(1 그룹은 레벨 1 · 슬롯 둘, 2 그룹은 레벨 5 · 슬롯 하나). */
    struct MonsterHuntMutagensTest
    {
        sw::MonsterHuntCatalog  _catalog{};
        sw::MonsterHuntMutagens _mutagens{};

        MonsterHuntMutagensTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kMonsterHuntXML, "MonsterHuntTest" ) );
            _mutagens.initialize( &_catalog, 3 );
        }

        bool readForged( int32 level, const sw::hashed_string& slot00, const sw::hashed_string& slot01, const sw::hashed_string& slot10, const sw::hashed_string& mutagen0 )
        {
            sw::Archive written;
            written << level;
            written << static_cast<uint32>( 2 );
            written << static_cast<uint32>( 2 );
            sw::StateIO::writeName( written, slot00 );
            sw::StateIO::writeName( written, slot01 );
            sw::StateIO::writeName( written, mutagen0 );
            written << static_cast<uint32>( 1 );
            sw::StateIO::writeName( written, slot10 );
            sw::StateIO::writeName( written, sw::hashed_string{} );
            sw::Archive reading( written.getData(), written.getSize() );
            return _mutagens.readState( reading );
        }
    };

    /** @brief 스킬 트리 카탈로그를 읽어 둔 판입니다. */
    struct SkillTreeStateTest
    {
        sw::SkillTreeCatalog _catalog{};
        sw::SkillTreeState   _state{};

        SkillTreeStateTest() { SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kSkillTreeXML, "MonsterHuntTest" ) ); }
    };
} // namespace

/**
 * @brief [MonsterHuntAlchemyTest] 독성은 상한과 정확히 같아질 때까지 마실 수 있고, 넘으면 거절하며 충전을 쓰지 않는다
 */
SW_TEST_CASE_F( MonsterHuntAlchemyTest, ToxicityCapIsInclusiveAndRefusalSpendsNothing )
{
    brew( "make_swallow" );
    brew( "make_thunder" );
    SW_EXPECT_EQUAL( 2, _alchemy.getCharges( "swallow" ) );

    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "thunder", _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "swallow", _inventory ) );
    SW_EXPECT_NEAR( 100.0f, _alchemy.getToxicity(), kMonsterHuntTolerance );

    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::TooToxic, _alchemy.drink( "swallow", _inventory ) );
    SW_EXPECT_EQUAL( 1, _alchemy.getCharges( "swallow" ) );
    SW_EXPECT_NEAR( 100.0f, _alchemy.getToxicity(), kMonsterHuntTolerance );
}

/**
 * @brief [MonsterHuntAlchemyTest] 변이 혼합물의 독성은 줄지 않다가 효과가 끝날 때 풀리고, 돌고 있는 동안은 다시 마시지 못한다. 묶이지 않은 독성만 줄어든다
 */
SW_TEST_CASE_F( MonsterHuntAlchemyTest, DecoctionLocksToxicityUntilItEnds )
{
    brew( "make_decoct" );
    brew( "make_swallow" );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "decoct", _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::AlreadyActive, _alchemy.drink( "decoct", _inventory ) );
    SW_EXPECT_EQUAL( 1, _alchemy.getCharges( "decoct" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "swallow", _inventory ) );
    SW_EXPECT_NEAR( 90.0f, _alchemy.getToxicity(), kMonsterHuntTolerance );

    _alchemy.update( 5.0f ); // 묶이지 않은 20 이 10 으로 준다
    SW_EXPECT_NEAR( 80.0f, _alchemy.getToxicity(), kMonsterHuntTolerance );

    _alchemy.update( -100.0f ); // 되돌리지 않는다
    _alchemy.update( 0.0f );
    SW_EXPECT_NEAR( 80.0f, _alchemy.getToxicity(), kMonsterHuntTolerance );
    SW_EXPECT_TRUE( _alchemy.isEffectActive( "decoct" ) );

    _alchemy.update( 25.0f );
    SW_EXPECT_NEAR( 0.0f, _alchemy.getToxicity(), kMonsterHuntTolerance ); // 바닥 아래로 내려가지 않는다
    SW_EXPECT_TRUE( _alchemy.getActiveEffects().empty() );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "decoct", _inventory ) );
}

/**
 * @brief [MonsterHuntAlchemyTest] 같은 물약을 다시 마시면 효과는 겹치지 않고 시간만 새로 시작하며 독성은 다시 오른다
 */
SW_TEST_CASE_F( MonsterHuntAlchemyTest, RedrinkingPotionRestartsItsEffect )
{
    brew( "make_swallow" );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "swallow", _inventory ) );
    _alchemy.update( 4.0f );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "swallow", _inventory ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), _alchemy.getActiveEffects().size() );
    SW_EXPECT_NEAR( 10.0f, _alchemy.getActiveEffects()[0]._remaining.getRemaining(), kMonsterHuntTolerance );
    SW_EXPECT_NEAR( 32.0f, _alchemy.getToxicity(), kMonsterHuntTolerance );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::NoCharges, _alchemy.drink( "swallow", _inventory ) );
}

/**
 * @brief [MonsterHuntAlchemyTest] 쓰는 길마다 종류를 가리고, 모르는 것 · 만들지 않은 것을 거절한다. 실패한 폭탄은 속성을 남기지 않는다
 */
SW_TEST_CASE_F( MonsterHuntAlchemyTest, UsePathsRefuseWrongKindsAndMissingItems )
{
    sw::hashed_string element( "stale" );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::UnknownItem, _alchemy.drink( "nothing", _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::UnknownItem, _alchemy.applyOil( "nothing", _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::UnknownItem, _alchemy.throwBomb( "nothing", _inventory, element ) );
    SW_EXPECT_TRUE( element.empty() );

    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::NotInInventory, _alchemy.drink( "swallow", _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::WrongKind, _alchemy.drink( "necro_oil", _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::WrongKind, _alchemy.applyOil( "swallow", _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::WrongKind, _alchemy.throwBomb( "swallow", _inventory, element ) );

    brew( "make_grapeshot" );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.throwBomb( "grapeshot", _inventory, element ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "Fire" ), element );
    SW_EXPECT_EQUAL( 1, _alchemy.getCharges( "grapeshot" ) );
}

/**
 * @brief [MonsterHuntAlchemyTest] 오일은 적중 횟수만큼 속성을 돌려주고 다하면 지워진다. 새 오일은 앞의 것을 지운다
 */
SW_TEST_CASE_F( MonsterHuntAlchemyTest, OilRunsOutAfterItsHits )
{
    brew( "make_necro_oil" );
    brew( "make_beast_oil" );
    SW_EXPECT_TRUE( _alchemy.consumeOilHit().empty() ); // 바르기 전

    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.applyOil( "necro_oil", _inventory ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "Silver" ), _alchemy.consumeOilHit() );
    SW_EXPECT_EQUAL( sw::hashed_string( "Silver" ), _alchemy.consumeOilHit() );
    SW_EXPECT_TRUE( _alchemy.consumeOilHit().empty() );
    SW_EXPECT_EQUAL( 0, _alchemy.getOilHits() );
    SW_EXPECT_TRUE( _alchemy.getOilElement().empty() );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::NoCharges, _alchemy.applyOil( "necro_oil", _inventory ) );

    SW_EXPECT_TRUE( _alchemy.meditate( _inventory ) == false ); // 술이 없다
    SW_EXPECT_EQUAL( 3, _inventory.addItem( "gull", 3 ) );
    SW_REQUIRE_TRUE( _alchemy.meditate( _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.applyOil( "necro_oil", _inventory ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "Silver" ), _alchemy.consumeOilHit() );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.applyOil( "beast_oil", _inventory ) );
    SW_EXPECT_EQUAL( 3, _alchemy.getOilHits() );
    SW_EXPECT_EQUAL( sw::hashed_string( "Beast" ), _alchemy.getOilElement() );
}

/**
 * @brief [MonsterHuntAlchemyTest] 명상은 앞에 적힌 술부터 하나만 쓰고, 인벤토리에 있는 물건만 채운다
 */
SW_TEST_CASE_F( MonsterHuntAlchemyTest, MeditationSpendsFirstAlcoholAndRefillsOwnedItems )
{
    brew( "make_swallow" );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "swallow", _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "swallow", _inventory ) );
    SW_EXPECT_EQUAL( 0, _alchemy.getCharges( "swallow" ) );
    SW_EXPECT_FALSE( _alchemy.meditate( _inventory ) );

    SW_EXPECT_EQUAL( 1, _inventory.addItem( "spirit", 1 ) );
    SW_EXPECT_EQUAL( 1, _inventory.addItem( "gull", 1 ) );
    SW_REQUIRE_TRUE( _alchemy.meditate( _inventory ) );
    SW_EXPECT_EQUAL( 0, _inventory.getItemCount( "spirit" ) );
    SW_EXPECT_EQUAL( 1, _inventory.getItemCount( "gull" ) );
    SW_EXPECT_EQUAL( 2, _alchemy.getCharges( "swallow" ) );
    SW_EXPECT_EQUAL( 0, _alchemy.getCharges( "thunder" ) ); // 만든 적이 없다
}

/**
 * @brief [MonsterHuntAlchemyTest] 상태를 쓰고 읽으면 충전 · 효과 · 오일 · 독성이 같다
 */
SW_TEST_CASE_F( MonsterHuntAlchemyTest, StateRoundTrips )
{
    brew( "make_swallow" );
    brew( "make_decoct" );
    brew( "make_necro_oil" );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "decoct", _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.drink( "swallow", _inventory ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntUseResult::Ok, _alchemy.applyOil( "necro_oil", _inventory ) );
    _alchemy.update( 1.0f );

    sw::Archive written;
    _alchemy.writeState( written );
    sw::MonsterHuntAlchemy restored;
    restored.initialize( &_catalog, &_recipeCatalog );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );

    SW_EXPECT_NEAR( _alchemy.getToxicity(), restored.getToxicity(), kMonsterHuntTolerance );
    SW_EXPECT_EQUAL( _alchemy.getCharges( "swallow" ), restored.getCharges( "swallow" ) );
    SW_EXPECT_EQUAL( _alchemy.getOilHits(), restored.getOilHits() );
    SW_EXPECT_EQUAL( _alchemy.getOilElement(), restored.getOilElement() );
    SW_EXPECT_EQUAL( _alchemy.getActiveEffects().size(), restored.getActiveEffects().size() );
}

/**
 * @brief [MonsterHuntAlchemyTest] 결함: 음수 · NaN 인 묶인 독성과 음수 충전, 이름과 어긋난 오일 횟수를 그대로 받았다. 음수 독성은 상한 검사를 뚫는다 — 거절하고 상태를 두다
 */
SW_TEST_CASE_F( MonsterHuntAlchemyTest, ReadStateRefusesOutOfRangeValues )
{
    SW_REQUIRE_TRUE( readForged( 1, 5.0f, "necro_oil", 2 ) ); // 형식은 맞다
    SW_EXPECT_NEAR( 5.0f, _alchemy.getToxicity(), kMonsterHuntTolerance );

    SW_EXPECT_FALSE( readForged( -1, 7.0f, "necro_oil", 2 ) );
    SW_EXPECT_FALSE( readForged( 1, -50.0f, "necro_oil", 2 ) );
    SW_EXPECT_FALSE( readForged( 1, std::numeric_limits<float32>::quiet_NaN(), "necro_oil", 2 ) );
    SW_EXPECT_FALSE( readForged( 1, 7.0f, sw::hashed_string{}, 2 ) );
    SW_EXPECT_FALSE( readForged( 1, 7.0f, "necro_oil", 0 ) );
    SW_EXPECT_FALSE( readForged( 1, 7.0f, "ghost_oil", 2 ) );

    SW_EXPECT_NEAR( 5.0f, _alchemy.getToxicity(), kMonsterHuntTolerance );
    SW_EXPECT_EQUAL( 2, _alchemy.getOilHits() );
}

/**
 * @brief [MonsterHuntBestiaryTest] 처치는 정한 수마다 한 단계씩 상한까지만 올리고 그때 약점을 알린다. 책은 그 위로 올리고, 지식은 내려가지 않는다
 */
SW_TEST_CASE( MonsterHuntBestiaryTest, KnowledgeRisesByKillsBookAndNeverFalls )
{
    sw::MonsterHuntCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kMonsterHuntXML, "MonsterHuntTest" ) );
    sw::MonsterHuntBestiary bestiary;
    bestiary.initialize( &catalog, nullptr );

    SW_EXPECT_FALSE( bestiary.recordKill( "ghoul" ) );
    SW_EXPECT_EQUAL( 0, bestiary.getKnowledge( "ghoul" ) );
    SW_EXPECT_TRUE( bestiary.recordKill( "ghoul" ) );
    SW_EXPECT_EQUAL( 1, bestiary.getKnowledge( "ghoul" ) );

    sw::vector<sw::MonsterHuntBestiaryEvent> listEvent;
    bestiary.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t( 2 ), listEvent.size() );
    SW_EXPECT_EQUAL( sw::MonsterHuntBestiaryEvent::Kind::KnowledgeRaised, listEvent[0]._kind );
    SW_EXPECT_EQUAL( 1, listEvent[0]._value );
    SW_EXPECT_EQUAL( sw::MonsterHuntBestiaryEvent::Kind::WeaknessRevealed, listEvent[1]._kind );
    SW_EXPECT_EQUAL( sw::hashed_string( "necro_oil" ), listEvent[1]._weaknessID );

    for ( int32 kill = 0; kill < 8; ++kill )
    {
        (void)bestiary.recordKill( "ghoul" );
    }
    SW_EXPECT_EQUAL( 10, bestiary.getKillCount( "ghoul" ) );
    SW_EXPECT_EQUAL( 2, bestiary.getKnowledge( "ghoul" ) ); // 처치 상한

    SW_EXPECT_TRUE( bestiary.readBook( "ghoul" ) );
    SW_EXPECT_EQUAL( 3, bestiary.getKnowledge( "ghoul" ) );
    SW_EXPECT_FALSE( bestiary.readBook( "ghoul" ) );
    SW_EXPECT_FALSE( bestiary.investigate( "ghoul" ) ); // 조사 단계(1)는 더 낮다
    SW_EXPECT_EQUAL( 3, bestiary.getKnowledge( "ghoul" ) );

    sw::vector<const sw::MonsterHuntWeakness*> listWeakness;
    bestiary.collectKnownWeaknesses( "ghoul", listWeakness );
    SW_EXPECT_EQUAL( size_t( 3 ), listWeakness.size() );

    SW_EXPECT_FALSE( bestiary.recordKill( "nobody" ) );
    SW_EXPECT_FALSE( bestiary.readBook( "nobody" ) );
    SW_EXPECT_EQUAL( 0, bestiary.getKillCount( "nobody" ) );
}

/**
 * @brief [MonsterHuntBestiaryTest] 한 번에 여러 단계를 건너뛰면 사이의 약점을 모두 알리고, 조사는 조사 단계까지만 보여 준다
 */
SW_TEST_CASE( MonsterHuntBestiaryTest, JumpRevealsEveryWeaknessInBetween )
{
    sw::MonsterHuntCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kMonsterHuntXML, "MonsterHuntTest" ) );
    sw::MonsterHuntBestiary bestiary;
    bestiary.initialize( &catalog, nullptr );

    sw::vector<const sw::MonsterHuntWeakness*> listWeakness;
    bestiary.collectKnownWeaknesses( "ghoul", listWeakness );
    SW_EXPECT_TRUE( listWeakness.empty() );

    SW_EXPECT_TRUE( bestiary.investigate( "ghoul" ) );
    bestiary.collectKnownWeaknesses( "ghoul", listWeakness );
    SW_REQUIRE_EQUAL( size_t( 1 ), listWeakness.size() );
    SW_EXPECT_EQUAL( sw::hashed_string( "necro_oil" ), listWeakness[0]->_id );

    sw::vector<sw::MonsterHuntBestiaryEvent> listEvent;
    bestiary.pollEvents( listEvent );
    listEvent.clear();
    SW_EXPECT_TRUE( bestiary.readBook( "ghoul" ) );
    bestiary.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t( 3 ), listEvent.size() ); // 단계 하나 + 새 약점 둘(앞서 본 것은 다시 알리지 않는다)
    SW_EXPECT_EQUAL( sw::hashed_string( "igni" ), listEvent[1]._weaknessID );
    SW_EXPECT_EQUAL( sw::hashed_string( "grapeshot" ), listEvent[2]._weaknessID );
}

/**
 * @brief [MonsterHuntBestiaryTest] 배율은 지식과 상관없이 적용되며, 방어 속성을 적지 않으면 분류를 쓴다. 모르는 괴물 · 빈 속성 · 테이블 없음은 1 이다
 */
SW_TEST_CASE( MonsterHuntBestiaryTest, MultiplierIgnoresKnowledge )
{
    sw::MonsterHuntCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kMonsterHuntXML, "MonsterHuntTest" ) );
    sw::ElementChart chart;
    chart.addElement( "Silver" );
    chart.addElement( "Necrophage" );
    SW_REQUIRE_TRUE( chart.setMultiplier( "Silver", "Necrophage", 2.0f ) );
    sw::MonsterHuntBestiary bestiary;
    bestiary.initialize( &catalog, &chart );

    SW_EXPECT_EQUAL( 0, bestiary.getKnowledge( "ghoul" ) );
    SW_EXPECT_NEAR( 2.0f, bestiary.computeMultiplier( "ghoul", "Silver" ), kMonsterHuntTolerance );
    SW_EXPECT_NEAR( 1.0f, bestiary.computeMultiplier( "ghoul", sw::hashed_string{} ), kMonsterHuntTolerance );
    SW_EXPECT_NEAR( 1.0f, bestiary.computeMultiplier( "nobody", "Silver" ), kMonsterHuntTolerance );

    sw::MonsterHuntBestiary noChart;
    noChart.initialize( &catalog, nullptr );
    SW_EXPECT_NEAR( 1.0f, noChart.computeMultiplier( "ghoul", "Silver" ), kMonsterHuntTolerance );
}

/**
 * @brief [MonsterHuntBestiaryTest] 상태 왕복이 지식과 처치 수를 지키고, 모르는 괴물 · 음수 값은 거절하며 상태를 둔다
 */
SW_TEST_CASE( MonsterHuntBestiaryTest, StateRoundTripsAndRefusesBadEntries )
{
    sw::MonsterHuntCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kMonsterHuntXML, "MonsterHuntTest" ) );
    sw::MonsterHuntBestiary bestiary;
    bestiary.initialize( &catalog, nullptr );
    SW_EXPECT_TRUE( bestiary.investigate( "ghoul" ) );
    (void)bestiary.recordKill( "ghoul" );

    sw::Archive written;
    bestiary.writeState( written );
    sw::MonsterHuntBestiary restored;
    restored.initialize( &catalog, nullptr );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 1, restored.getKnowledge( "ghoul" ) );
    SW_EXPECT_EQUAL( 1, restored.getKillCount( "ghoul" ) );

    const auto readForged = [&restored]( const sw::hashed_string& monsterID, int32 knowledge, int32 kills )
    {
        sw::Archive forged;
        forged << static_cast<uint32>( 1 );
        sw::StateIO::writeName( forged, monsterID );
        forged << knowledge;
        forged << kills;
        sw::Archive forgedReading( forged.getData(), forged.getSize() );
        return restored.readState( forgedReading );
    };
    SW_EXPECT_FALSE( readForged( "nobody", 1, 1 ) );
    SW_EXPECT_FALSE( readForged( "ghoul", -1, 1 ) );
    SW_EXPECT_FALSE( readForged( "ghoul", 1, -1 ) );
    SW_EXPECT_EQUAL( 1, restored.getKnowledge( "ghoul" ) );
}

/**
 * @brief [MonsterHuntCombatSetupTest] 카탈로그 없는 시작은 대신 쓸 수치 없이 오류로 멈춘다
 */
SW_TEST_CASE( MonsterHuntCombatSetupTest, InitializeRefusesNoCatalog )
{
    SW_EXPECT_LOG( "MonsterHuntCombat.MonsterHuntCombatRefusedNoCatalog" );
    sw::MonsterHuntCombat combat;
    SW_EXPECT_FALSE( combat.initialize( nullptr, 1 ) );
}

/**
 * @brief [MonsterHuntCombatTest] 공짜 동작은 스태미나가 바닥이어도 되고, 값이 있는 동작은 모자라면 값을 깎지 않는다
 */
SW_TEST_CASE_F( MonsterHuntCombatTest, ActionsSpendStaminaOnlyWhenAffordable )
{
    SW_EXPECT_NEAR( 0.0f, _combat.getActionCost( sw::MonsterHuntAction::FastAttack ), kMonsterHuntTolerance );
    for ( int32 roll = 0; roll < 9; ++roll )
    {
        SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::Ok, _combat.performAction( sw::MonsterHuntAction::Roll ) );
    }
    SW_EXPECT_NEAR( 10.0f, _combat.getStamina().getValue(), kMonsterHuntTolerance );
    SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::Ok, _combat.performAction( sw::MonsterHuntAction::Dodge ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::NotEnoughStamina, _combat.performAction( sw::MonsterHuntAction::StrongAttack ) );
    SW_EXPECT_NEAR( 5.0f, _combat.getStamina().getValue(), kMonsterHuntTolerance );
    SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::Ok, _combat.performAction( sw::MonsterHuntAction::FastAttack ) );
}

/**
 * @brief [MonsterHuntCombatTest] 표식 위력은 능력치로 오르고 음수 능력치에도 0 아래로 가지 않는다. 대체 시전은 스킬이 있어야 하고 거절은 기력을 쓰지 않는다
 */
SW_TEST_CASE_F( MonsterHuntCombatTest, SignPowerAndAlternateCast )
{
    SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::UnknownSign, _combat.castSign( "nothing", false, nullptr, _stats, nullptr )._result );

    _stats.setValue( "signIntensity", 50.0f );
    const sw::MonsterHuntSignCast plain = _combat.castSign( "igni", false, nullptr, _stats, nullptr );
    SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::Ok, plain._result );
    SW_EXPECT_NEAR( 30.0f, plain._power, kMonsterHuntTolerance );
    SW_EXPECT_EQUAL( sw::hashed_string( "Fire" ), plain._element );
    SW_EXPECT_TRUE( plain._status.empty() ); // 테이블이 없으면 굴리지 않는다
    SW_EXPECT_NEAR( 70.0f, _combat.getStamina().getValue(), kMonsterHuntTolerance );

    sw::SkillTreeDef tree;
    sw::SkillDef     firestream;
    firestream._id = "firestream";
    tree._listSkill.push_back( firestream );
    sw::SkillTreeState skills;
    skills.initialize( &tree );
    skills.addPoints( 1 );

    SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::AlternateLocked, _combat.castSign( "igni", true, nullptr, _stats, nullptr )._result );
    SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::AlternateLocked, _combat.castSign( "igni", true, &skills, _stats, nullptr )._result ); // 랭크 0
    SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::AlternateLocked, _combat.castSign( "quen", true, &skills, _stats, nullptr )._result ); // 대체 시전이 없는 표식
    SW_EXPECT_NEAR( 70.0f, _combat.getStamina().getValue(), kMonsterHuntTolerance );

    SW_EXPECT_EQUAL( sw::SkillResult::Ok, skills.rankUp( "firestream", 1 ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::NotEnoughStamina, _combat.castSign( "igni", true, &skills, _stats, nullptr )._result );
    _combat.getStamina().refill();
    const sw::MonsterHuntSignCast alternate = _combat.castSign( "igni", true, &skills, _stats, nullptr );
    SW_EXPECT_EQUAL( sw::MonsterHuntCombatResult::Ok, alternate._result );
    SW_EXPECT_NEAR( 45.0f, alternate._power, kMonsterHuntTolerance );
    SW_EXPECT_NEAR( 20.0f, _combat.getStamina().getValue(), kMonsterHuntTolerance );

    _stats.setValue( "signIntensity", -500.0f );
    SW_EXPECT_NEAR( 0.0f, _combat.castSign( "quen", false, nullptr, _stats, nullptr )._power, kMonsterHuntTolerance );
}

/**
 * @brief [MonsterHuntCombatTest] 0.1 씩 열 번이면 포인트 하나다(부동소수 오차로 놓치지 않는다). 상한에 멈추고, 쓰면 소수 부분이 남고, 피격은 0 아래로 가지 않는다
 */
SW_TEST_CASE_F( MonsterHuntCombatTest, AdrenalinePointsAccumulateAndClamp )
{
    for ( int32 hit = 0; hit < 10; ++hit )
    {
        _combat.registerHitLanded();
    }
    SW_EXPECT_EQUAL( 1, _combat.getAdrenalinePoints() );
    SW_EXPECT_NEAR( 1.1f, _combat.computeDamageScale(), kMonsterHuntTolerance );

    for ( int32 hit = 0; hit < 5; ++hit )
    {
        _combat.registerHitLanded();
    }
    SW_EXPECT_EQUAL( 1, _combat.consumeAdrenalinePoints() );
    SW_EXPECT_NEAR( 0.5f, _combat.getAdrenaline(), kMonsterHuntTolerance );
    SW_EXPECT_EQUAL( 0, _combat.consumeAdrenalinePoints() );

    _combat.registerHitTaken();
    SW_EXPECT_NEAR( 0.0f, _combat.getAdrenaline(), kMonsterHuntTolerance );

    for ( int32 hit = 0; hit < 50; ++hit )
    {
        _combat.registerHitLanded();
    }
    SW_EXPECT_NEAR( 3.0f, _combat.getAdrenaline(), kMonsterHuntTolerance );
    SW_EXPECT_EQUAL( 3, _combat.getAdrenalinePoints() );
}

/**
 * @brief [MonsterHuntCombatTest] 시드가 같으면 같은 상태이상이 걸리고, 상태를 쓰고 읽은 쪽도 같은 수열을 잇는다
 */
SW_TEST_CASE_F( MonsterHuntCombatTest, StatusRollsAreDeterministicAcrossState )
{
    sw::MonsterHuntCombat twin;
    SW_REQUIRE_TRUE( twin.initialize( &_catalog, 7 ) );
    const auto rollStatus = [this]( sw::MonsterHuntCombat& combat )
    {
        combat.getStamina().refill();
        return combat.castSign( "igni", false, nullptr, _stats, &_chart )._status;
    };
    for ( int32 cast = 0; cast < 5; ++cast )
    {
        SW_EXPECT_EQUAL( rollStatus( _combat ), rollStatus( twin ) );
    }

    sw::Archive written;
    _combat.writeState( written );
    sw::MonsterHuntCombat restored;
    SW_REQUIRE_TRUE( restored.initialize( &_catalog, 99 ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    for ( int32 cast = 0; cast < 16; ++cast )
    {
        SW_EXPECT_EQUAL( rollStatus( _combat ), rollStatus( restored ) );
    }
}

/**
 * @brief [MonsterHuntCombatTest] 결함: NaN 아드레날린은 `< 0` 검사를 지나 포인트 변환을 깨뜨렸고, 상한 위 값도 그대로 받아 피해 배율이 끝없이 올랐다 — 거절하고 상태를 둔다
 */
SW_TEST_CASE_F( MonsterHuntCombatTest, ReadStateRefusesOutOfRangeAdrenaline )
{
    SW_REQUIRE_TRUE( readForged( 2.0f ) ); // 형식은 맞다
    SW_EXPECT_EQUAL( 2, _combat.getAdrenalinePoints() );

    SW_EXPECT_FALSE( readForged( std::numeric_limits<float32>::quiet_NaN() ) );
    SW_EXPECT_FALSE( readForged( -1.0f ) );
    SW_EXPECT_FALSE( readForged( 3.5f ) );
    SW_EXPECT_FALSE( readForged( std::numeric_limits<float32>::infinity() ) );
    SW_EXPECT_EQUAL( 2, _combat.getAdrenalinePoints() );
}

/**
 * @brief [MonsterHuntContractTest] 조사 한 판: 순서 · 거리(경계 포함) · 다른 단계 단서를 가리고, 단계 끝과 조사 끝을 차례로 알린다
 */
SW_TEST_CASE_F( MonsterHuntContractTest, InvestigationWalksOrderDistanceAndSteps )
{
    sw::MonsterHuntInvestigation investigation;
    SW_REQUIRE_TRUE( investigation.initialize( &_catalog, "griffin", nullptr ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "tracks" ), investigation.getStepID() );

    const sw::float3 atOrigin{ 0.0f, 0.0f, 0.0f };
    const sw::float3 atFeather{ 10.0f, 0.0f, 0.0f };
    const sw::float3 atEggs{ 20.0f, 0.0f, 0.0f };

    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::UnknownClue, investigation.investigate( "eggs", atEggs ) ); // 다음 단계의 단서
    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::UnknownClue, investigation.investigate( "nothing", atOrigin ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::OutOfOrder, investigation.investigate( "feather", atFeather ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::TooFar, investigation.investigate( "blood", sw::float3{ 2.5f, 0.0f, 0.0f } ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::Found, investigation.investigate( "blood", sw::float3{ 2.0f, 0.0f, 0.0f } ) ); // 반지름과 같은 거리
    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::AlreadyFound, investigation.investigate( "blood", atOrigin ) );
    SW_EXPECT_TRUE( investigation.isClueFound( "blood" ) );
    SW_EXPECT_FALSE( investigation.isClueFound( "feather" ) );
    SW_EXPECT_EQUAL( size_t( 1 ), pollAll( investigation ).size() );

    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::Found, investigation.investigate( "feather", atFeather ) );
    {
        const sw::vector<sw::MonsterHuntInvestigationEvent> listEvent = pollAll( investigation );
        SW_REQUIRE_EQUAL( size_t( 2 ), listEvent.size() );
        SW_EXPECT_EQUAL( sw::MonsterHuntInvestigationEvent::Kind::ClueFound, listEvent[0]._kind );
        SW_EXPECT_EQUAL( sw::MonsterHuntInvestigationEvent::Kind::StepCompleted, listEvent[1]._kind );
        SW_EXPECT_EQUAL( sw::hashed_string( "tracks" ), listEvent[1]._stepID );
    }
    SW_EXPECT_EQUAL( 1, investigation.getStepIndex() );
    SW_EXPECT_TRUE( investigation.isClueFound( "blood" ) ); // 지난 단계의 단서는 찾은 것이다
    SW_EXPECT_FALSE( investigation.isSolved() );

    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::Found, investigation.investigate( "eggs", atEggs ) );
    {
        const sw::vector<sw::MonsterHuntInvestigationEvent> listEvent = pollAll( investigation );
        SW_REQUIRE_EQUAL( size_t( 3 ), listEvent.size() );
        SW_EXPECT_EQUAL( sw::MonsterHuntInvestigationEvent::Kind::Solved, listEvent[2]._kind );
    }
    SW_EXPECT_TRUE( investigation.isSolved() );
    SW_EXPECT_TRUE( investigation.getStepID().empty() );
    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::Solved, investigation.investigate( "eggs", atEggs ) );
}

/**
 * @brief [MonsterHuntContractTest] 결함: 첫 단계에 단서가 없으면 찾을 것이 없어 조사가 영영 끝나지 않았다(가운데의 빈 단계만 넘겼다) — 시작할 때 넘긴다. 단계가 없는 계약은 바로 끝난다
 */
SW_TEST_CASE_F( MonsterHuntContractTest, EmptyLeadingStepsAreSkipped )
{
    sw::MonsterHuntInvestigation investigation;
    SW_REQUIRE_TRUE( investigation.initialize( &_catalog, "interview", nullptr ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "search" ), investigation.getStepID() );
    {
        const sw::vector<sw::MonsterHuntInvestigationEvent> listEvent = pollAll( investigation );
        SW_REQUIRE_EQUAL( size_t( 1 ), listEvent.size() );
        SW_EXPECT_EQUAL( sw::hashed_string( "talk" ), listEvent[0]._stepID );
    }

    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::Found, investigation.investigate( "note", sw::float3{ 0.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( investigation.isSolved() ); // 뒤의 빈 단계도 넘긴다
    {
        const sw::vector<sw::MonsterHuntInvestigationEvent> listEvent = pollAll( investigation );
        SW_REQUIRE_EQUAL( size_t( 4 ), listEvent.size() ); // 단서, search, report, 끝
        SW_EXPECT_EQUAL( sw::hashed_string( "report" ), listEvent[2]._stepID );
        SW_EXPECT_EQUAL( sw::MonsterHuntInvestigationEvent::Kind::Solved, listEvent[3]._kind );
    }

    sw::MonsterHuntInvestigation noSteps;
    SW_REQUIRE_TRUE( noSteps.initialize( &_catalog, "no_steps", nullptr ) );
    SW_EXPECT_TRUE( noSteps.isSolved() );
    {
        const sw::vector<sw::MonsterHuntInvestigationEvent> listEvent = pollAll( noSteps );
        SW_REQUIRE_EQUAL( size_t( 1 ), listEvent.size() );
        SW_EXPECT_EQUAL( sw::MonsterHuntInvestigationEvent::Kind::Solved, listEvent[0]._kind );
    }

    sw::MonsterHuntInvestigation unknown;
    SW_EXPECT_FALSE( unknown.initialize( &_catalog, "nothing", nullptr ) );
    SW_EXPECT_TRUE( pollAll( unknown ).empty() );
    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::Solved, unknown.investigate( "blood", sw::float3{} ) );
}

/**
 * @brief [MonsterHuntContractTest] 위쳐 감각은 열린 순서 · 못 찾은 단서만 고른다. 결함: 음수 반지름이 제곱되어 그 거리까지 보였다
 */
SW_TEST_CASE_F( MonsterHuntContractTest, SenseOnlyOpenCluesWithinRadius )
{
    sw::MonsterHuntInvestigation investigation;
    SW_REQUIRE_TRUE( investigation.initialize( &_catalog, "griffin", nullptr ) );

    sw::vector<const sw::MonsterHuntClueDef*> listClue;
    investigation.senseClues( sw::float3{}, 100.0f, listClue );
    SW_REQUIRE_EQUAL( size_t( 1 ), listClue.size() );
    SW_EXPECT_EQUAL( sw::hashed_string( "blood" ), listClue[0]->_id );

    investigation.senseClues( sw::float3{}, 0.0f, listClue ); // 그 자리에서는 반지름 0 으로도 보인다
    SW_EXPECT_EQUAL( size_t( 1 ), listClue.size() );
    investigation.senseClues( sw::float3{}, -100.0f, listClue );
    SW_EXPECT_TRUE( listClue.empty() );

    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::Found, investigation.investigate( "blood", sw::float3{} ) );
    investigation.senseClues( sw::float3{}, 100.0f, listClue );
    SW_REQUIRE_EQUAL( size_t( 1 ), listClue.size() );
    SW_EXPECT_EQUAL( sw::hashed_string( "feather" ), listClue[0]->_id );
    investigation.senseClues( sw::float3{}, 5.0f, listClue );
    SW_EXPECT_TRUE( listClue.empty() );
}

/**
 * @brief [MonsterHuntContractTest] 단서와 단계 끝을 일지에 알려 계약 퀘스트가 진행된다
 */
SW_TEST_CASE_F( MonsterHuntContractTest, InvestigationDrivesQuestLog )
{
    sw::QuestCatalog      questCatalog;
    constexpr const utf8* kQuestXML = R"(<QuestCatalog><Quest id="griffin_quest">
  <Stage id="find" next="track"><Objective kind="Clue" target="blood"/></Stage>
  <Stage id="track" next="done"><Objective kind="Investigate" target="tracks"/></Stage>
  <Stage id="done" complete="true"/>
</Quest></QuestCatalog>)";
    SW_REQUIRE_TRUE( questCatalog.loadFromXMLText( kQuestXML, "MonsterHuntTest" ) );
    sw::QuestLog questLog;
    questLog.initialize( &questCatalog );
    SW_REQUIRE_EQUAL( sw::QuestStartResult::Ok, questLog.start( "griffin_quest", 1 ) );

    sw::MonsterHuntInvestigation investigation;
    SW_REQUIRE_TRUE( investigation.initialize( &_catalog, "griffin", &questLog ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::Found, investigation.investigate( "blood", sw::float3{} ) );
    SW_EXPECT_EQUAL( sw::QuestStatus::Active, questLog.getStatus( "griffin_quest" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntClueResult::Found, investigation.investigate( "feather", sw::float3{ 10.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_EQUAL( sw::QuestStatus::Completed, questLog.getStatus( "griffin_quest" ) );
}

/**
 * @brief [MonsterHuntContractTest] 결함: 찾은 단서를 지금 단계와 맞춰 보지 않아, 단계의 단서를 다 찾은 채 멈춘 상태를 받으면 넘어갈 길이 없어 조사가 영영 끝나지 않았다
 */
SW_TEST_CASE_F( MonsterHuntContractTest, InvestigationReadStateChecksFoundClues )
{
    sw::MonsterHuntInvestigation investigation;
    SW_REQUIRE_TRUE( investigation.initialize( &_catalog, "griffin", nullptr ) );
    SW_REQUIRE_TRUE( readForgedInvestigation( investigation, "griffin", 0, { "blood" } ) ); // 형식은 맞다
    SW_EXPECT_TRUE( investigation.isClueFound( "blood" ) );

    SW_EXPECT_FALSE( readForgedInvestigation( investigation, "griffin", 0, { "blood", "feather" } ) ); // 단계를 다 찾았다
    SW_EXPECT_FALSE( readForgedInvestigation( investigation, "griffin", 0, { "eggs" } ) );             // 다른 단계의 단서
    SW_EXPECT_FALSE( readForgedInvestigation( investigation, "griffin", 2, { "eggs" } ) );             // 끝난 조사에 남은 단서
    SW_EXPECT_FALSE( readForgedInvestigation( investigation, "griffin", 3, {} ) );                     // 단계 밖
    SW_EXPECT_FALSE( readForgedInvestigation( investigation, "griffin", -1, {} ) );
    SW_EXPECT_FALSE( readForgedInvestigation( investigation, "nothing", 0, {} ) );
    SW_EXPECT_EQUAL( 0, investigation.getStepIndex() );
    SW_EXPECT_TRUE( investigation.isClueFound( "blood" ) ); // 거절은 상태를 바꾸지 않는다

    SW_REQUIRE_TRUE( readForgedInvestigation( investigation, "griffin", 2, {} ) );
    SW_EXPECT_TRUE( investigation.isSolved() );
}

/**
 * @brief [MonsterHuntContractTest] 한도 이하의 요구는 받아들이고, 넘으면 분노가 오르며 제안이 반만큼 다가온다. 끝나면 더 받지 않는다
 */
SW_TEST_CASE_F( MonsterHuntContractTest, HaggleCountersUpToLimit )
{
    sw::MonsterHuntHaggle haggle;
    SW_REQUIRE_TRUE( haggle.initialize( &_catalog, "griffin" ) );
    SW_EXPECT_EQUAL( 100, haggle.getOffer() );

    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Countered, haggle.propose( 151 ) ); // 한도(150)를 1 넘는다
    SW_EXPECT_EQUAL( 125, haggle.getOffer() );
    SW_EXPECT_NEAR( 0.11f, haggle.getAnger(), kMonsterHuntTolerance );
    SW_EXPECT_EQUAL( 0, haggle.getFinalReward() );
    SW_EXPECT_FALSE( haggle.isClosed() );

    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Accepted, haggle.propose( 150 ) ); // 한도와 같은 요구
    SW_EXPECT_EQUAL( 150, haggle.getFinalReward() );
    SW_EXPECT_TRUE( haggle.isClosed() );
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Closed, haggle.propose( 120 ) );
    SW_EXPECT_EQUAL( 150, haggle.acceptOffer() );
}

/**
 * @brief [MonsterHuntContractTest] 분노가 최대에 닿으면(같아도) 결렬되어 처음 보상만 준다. 지금 제안을 받으면 그 제안으로 끝난다
 */
SW_TEST_CASE_F( MonsterHuntContractTest, HaggleBreaksOffAtMaxAnger )
{
    sw::MonsterHuntHaggle rounds;
    SW_REQUIRE_TRUE( rounds.initialize( &_catalog, "rounds" ) );
    for ( int32 round = 0; round < 3; ++round )
    {
        SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Countered, rounds.propose( 200 ) );
    }
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::BrokenOff, rounds.propose( 200 ) ); // 0.25 × 4 = 1
    SW_EXPECT_EQUAL( 100, rounds.getFinalReward() );

    sw::MonsterHuntHaggle greedy;
    SW_REQUIRE_TRUE( greedy.initialize( &_catalog, "griffin" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::BrokenOff, greedy.propose( 100000 ) );
    SW_EXPECT_EQUAL( 100, greedy.getFinalReward() );

    sw::MonsterHuntHaggle settled;
    SW_REQUIRE_TRUE( settled.initialize( &_catalog, "griffin" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Countered, settled.propose( 160 ) );
    const int32 offer = settled.getOffer();
    SW_EXPECT_EQUAL( offer, settled.acceptOffer() );
    SW_EXPECT_EQUAL( offer, settled.getFinalReward() );
}

/**
 * @brief [MonsterHuntContractTest] 결함: `( 제안 + 요구 ) / 2` 가 큰 요구에서 넘쳐 제안이 오르지 않았다. 제안은 한도까지 오른다. 음수 요구 · 0 보상에도 보상은 음수가 되지 않는다
 */
SW_TEST_CASE_F( MonsterHuntContractTest, HaggleSurvivesExtremeAsks )
{
    sw::MonsterHuntHaggle calm;
    SW_REQUIRE_TRUE( calm.initialize( &_catalog, "calm" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Countered, calm.propose( std::numeric_limits<int32>::max() ) );
    SW_EXPECT_EQUAL( 150, calm.getOffer() );
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Countered, calm.propose( std::numeric_limits<int32>::max() ) );
    SW_EXPECT_EQUAL( 150, calm.getOffer() );

    sw::MonsterHuntHaggle negative;
    SW_REQUIRE_TRUE( negative.initialize( &_catalog, "griffin" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Accepted, negative.propose( -50 ) );
    SW_EXPECT_EQUAL( 0, negative.getFinalReward() );

    sw::MonsterHuntHaggle unpaid;
    SW_REQUIRE_TRUE( unpaid.initialize( &_catalog, "free" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::BrokenOff, unpaid.propose( 1 ) ); // 처음 보상이 0 이어도 0 으로 나누지 않는다
    SW_EXPECT_EQUAL( 0, unpaid.getFinalReward() );
}

/**
 * @brief [MonsterHuntContractTest] 결함: 기본 생성한 흥정은 열려 있어 `propose` 가 빈 계약을 읽었다 — 끝난 상태로 시작한다. 시작이 실패해도 같다
 */
SW_TEST_CASE_F( MonsterHuntContractTest, HaggleWithoutContractIsClosed )
{
    sw::MonsterHuntHaggle neverStarted;
    SW_EXPECT_TRUE( neverStarted.isClosed() );
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Closed, neverStarted.propose( 10 ) );
    SW_EXPECT_EQUAL( 0, neverStarted.acceptOffer() );

    sw::MonsterHuntHaggle unknown;
    SW_EXPECT_FALSE( unknown.initialize( &_catalog, "nothing" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Closed, unknown.propose( 10 ) );
}

/**
 * @brief [MonsterHuntContractTest] 결함: 음수 제안 · 보상을 그대로 받았다 — 거절하고 상태를 둔다. 왕복은 값을 지킨다
 */
SW_TEST_CASE_F( MonsterHuntContractTest, HaggleReadStateChecksRange )
{
    sw::MonsterHuntHaggle haggle;
    SW_REQUIRE_TRUE( haggle.initialize( &_catalog, "griffin" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntHaggleResult::Countered, haggle.propose( 151 ) );

    sw::Archive written;
    haggle.writeState( written );
    sw::MonsterHuntHaggle restored;
    SW_REQUIRE_TRUE( restored.initialize( &_catalog, "rounds" ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 125, restored.getOffer() );
    SW_EXPECT_NEAR( haggle.getAnger(), restored.getAnger(), kMonsterHuntTolerance );

    SW_EXPECT_FALSE( readForgedHaggle( restored, "griffin", 0.0f, -1, 0, 0 ) );
    SW_EXPECT_FALSE( readForgedHaggle( restored, "griffin", 0.0f, 100, -1, 1 ) );
    SW_EXPECT_FALSE( readForgedHaggle( restored, "griffin", -1.0f, 100, 0, 0 ) );
    SW_EXPECT_FALSE( readForgedHaggle( restored, "griffin", std::numeric_limits<float32>::quiet_NaN(), 100, 0, 0 ) );
    SW_EXPECT_FALSE( readForgedHaggle( restored, "griffin", 0.0f, 100, 0, 2 ) );
    SW_EXPECT_FALSE( readForgedHaggle( restored, "nothing", 0.0f, 100, 0, 0 ) );
    SW_EXPECT_EQUAL( 125, restored.getOffer() );
}

/**
 * @brief [MonsterHuntMutagensTest] 슬롯 번호 · 잠긴 그룹 · 겹치는 스킬 · 모르는 변이원을 가린다. 같은 칸에 다시 끼우거나 빈 이름을 여럿 두는 것은 된다
 */
SW_TEST_CASE_F( MonsterHuntMutagensTest, EquipChecksSlotLockDuplicateAndMutagen )
{
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::InvalidSlot, _mutagens.equipSkill( -1, 0, "red_a" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::InvalidSlot, _mutagens.equipSkill( 2, 0, "red_a" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::InvalidSlot, _mutagens.equipSkill( 0, 2, "red_a" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::InvalidSlot, _mutagens.equipSkill( 0, -1, "red_a" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::Locked, _mutagens.equipSkill( 1, 0, "red_a" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::Locked, _mutagens.equipMutagen( 1, "red_m" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::InvalidSlot, _mutagens.equipMutagen( 2, "red_m" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::UnknownMutagen, _mutagens.equipMutagen( 0, "nothing" ) );

    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::Ok, _mutagens.equipSkill( 0, 0, "red_a" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::AlreadyEquipped, _mutagens.equipSkill( 0, 1, "red_a" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::Ok, _mutagens.equipSkill( 0, 0, "red_a" ) );

    _mutagens.setCharacterLevel( 5 );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::AlreadyEquipped, _mutagens.equipSkill( 1, 0, "red_a" ) ); // 다른 그룹에 있어도 겹친다

    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::Ok, _mutagens.equipSkill( 0, 0, sw::hashed_string{} ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::Ok, _mutagens.equipSkill( 0, 1, sw::hashed_string{} ) );
    _mutagens.clearSlot( 9, 9 ); // 범위 밖은 아무 일도 없다
    SW_EXPECT_TRUE( _mutagens.getSkill( 9, 9 ).empty() );
    SW_EXPECT_TRUE( _mutagens.getMutagen( -1 ).empty() );
}

/**
 * @brief [MonsterHuntMutagensTest] 능력치는 기본 값 + 맞는 색 수 × 맞춤 값이다. 레벨이 내려 닫힌 그룹은 끼운 채 남지만 능력치 · 능동 목록에 들지 않는다
 */
SW_TEST_CASE_F( MonsterHuntMutagensTest, StatsCountMatchingColorsInOpenGroups )
{
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::Ok, _mutagens.equipMutagen( 0, "red_m" ) );
    SW_EXPECT_EQUAL( 0, _mutagens.countMatches( 0 ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::Ok, _mutagens.equipSkill( 0, 0, "red_a" ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::Ok, _mutagens.equipSkill( 0, 1, "blue_a" ) );
    SW_EXPECT_EQUAL( 1, _mutagens.countMatches( 0 ) );
    SW_EXPECT_EQUAL( sw::MonsterHuntSlotResult::Ok, _mutagens.equipSkill( 0, 1, "red_b" ) );
    SW_EXPECT_EQUAL( 2, _mutagens.countMatches( 0 ) );
    SW_EXPECT_EQUAL( 0, _mutagens.countMatches( -1 ) );
    SW_EXPECT_EQUAL( 0, _mutagens.countMatches( 2 ) );

    sw::StatBlock stats;
    _mutagens.computeStats( stats );
    SW_EXPECT_NEAR( 9.0f, stats.getValue( "attackPower" ), kMonsterHuntTolerance );

    _mutagens.clearSlot( 0, 1 );
    stats.clear();
    _mutagens.computeStats( stats );
    SW_EXPECT_NEAR( 7.0f, stats.getValue( "attackPower" ), kMonsterHuntTolerance );

    _mutagens.setCharacterLevel( 0 );
    stats.clear();
    _mutagens.computeStats( stats );
    SW_EXPECT_NEAR( 0.0f, stats.getValue( "attackPower" ), kMonsterHuntTolerance );
    sw::vector<sw::hashed_string> listSkill;
    _mutagens.collectEquippedSkills( listSkill );
    SW_EXPECT_TRUE( listSkill.empty() );
    SW_EXPECT_EQUAL( sw::hashed_string( "red_a" ), _mutagens.getSkill( 0, 0 ) );
}

/**
 * @brief [MonsterHuntMutagensTest] 결함: 상태 바이트는 같은 스킬이 두 슬롯에 있어도 받아 색 맞춤을 두 번 셌다 — `equipSkill` 과 같은 약속으로 거절한다. 빈 슬롯은 겹침이 아니다
 */
SW_TEST_CASE_F( MonsterHuntMutagensTest, ReadStateRefusesDuplicateSkills )
{
    const sw::hashed_string kNone{};
    SW_REQUIRE_TRUE( readForged( 5, "red_a", kNone, "blue_a", "red_m" ) ); // 형식은 맞다
    SW_EXPECT_EQUAL( 1, _mutagens.countMatches( 0 ) );
    SW_EXPECT_TRUE( _mutagens.isGroupOpen( 1 ) );

    SW_EXPECT_FALSE( readForged( 5, "red_a", "red_a", kNone, "red_m" ) );
    SW_EXPECT_FALSE( readForged( 5, "red_a", kNone, "red_a", "red_m" ) );
    SW_EXPECT_FALSE( readForged( 5, "red_a", kNone, kNone, "nothing" ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "blue_a" ), _mutagens.getSkill( 1, 0 ) );

    SW_EXPECT_TRUE( readForged( 5, kNone, kNone, kNone, kNone ) );
}

/**
 * @brief [SkillTreeStateTest] 랭크 올리기의 조건: 모르는 스킬 · 레벨 · 선행 랭크 · 층 · 배타 그룹 · 최대 랭크 · 점수. 거절은 점수를 쓰지 않는다
 */
SW_TEST_CASE_F( SkillTreeStateTest, RankUpChecksEveryCondition )
{
    _state.initialize( _catalog.find( "combat" ) );
    SW_REQUIRE_TRUE( _state.getTree() != nullptr );

    SW_EXPECT_EQUAL( sw::SkillResult::UnknownSkill, _state.rankUp( "nothing", 99 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::NotEnoughPoints, _state.rankUp( "base", 1 ) );
    _state.addPoints( 10 );

    SW_EXPECT_EQUAL( sw::SkillResult::LevelTooLow, _state.rankUp( "power", 2 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::RequirementMissing, _state.rankUp( "power", 3 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::TierLocked, _state.rankUp( "stance_a", 1 ) );
    SW_EXPECT_EQUAL( 10, _state.getPoints() );

    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "base", 1 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::RequirementMissing, _state.rankUp( "power", 3 ) ); // 랭크 2 를 요구한다
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "base", 1 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::MaxRank, _state.rankUp( "base", 1 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "power", 3 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "stance_a", 1 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::GroupTaken, _state.rankUp( "stance_b", 1 ) );

    SW_EXPECT_EQUAL( 5, _state.getSpentPoints() );
    SW_EXPECT_EQUAL( 5, _state.getPoints() );

    sw::StatBlock stats;
    stats.setValue( "stale", 1.0f ); // 비우고 다시 센다
    _state.computeStats( stats );
    SW_EXPECT_NEAR( 10.0f, stats.getValue( "attack" ), kMonsterHuntTolerance );
    SW_EXPECT_FALSE( stats.hasValue( "stale" ) );

    sw::vector<sw::hashed_string> listAbility;
    _state.collectAbilities( listAbility );
    SW_REQUIRE_EQUAL( size_t( 1 ), listAbility.size() );
    SW_EXPECT_EQUAL( sw::hashed_string( "PowerStrike" ), listAbility[0] );

    SW_EXPECT_EQUAL( 5, _state.refundAll() );
    SW_EXPECT_EQUAL( 10, _state.getPoints() );
    SW_EXPECT_EQUAL( 0, _state.getSpentPoints() );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "base", 1 ) );
}

/**
 * @brief [SkillTreeStateTest] 스킬 디버거 줄은 스킬마다 랭크와 다음 랭크의 판정을 그 까닭의 값(필요 레벨 · 빠진 선행 · 층 · 점수)과 함께 낸다 — 왜 해금되지 않는지를 판정 함수와 같은 순서로 보인다
 */
SW_TEST_CASE_F( SkillTreeStateTest, DebugLinesExplainWhyTheNextRankIsLocked )
{
    const auto findLine = []( const sw::DebugHUDSectionWriter& writer, sw::string_view label ) -> sw::string
    {
        for ( uint32 index = 0; index < writer.getLineCount(); ++index )
        {
            if ( writer.getLineAt( index )._label == label )
                return writer.getLineAt( index )._value;
        }
        return "(missing)";
    };
    _state.initialize( _catalog.find( "combat" ) );
    _state.addPoints( 1 );
    SW_REQUIRE_EQUAL( sw::SkillResult::Ok, _state.rankUp( "base", 1 ) );

    sw::DebugHUDSectionWriter writer{ sw::SceneWalkTestAccess::getKey() };
    _state.writeDebugLines( writer, 2, 16 );
    SW_EXPECT_EQUAL( sw::string( "points 0, spent 1, level 2" ), findLine( writer, "tree combat" ) );
    SW_EXPECT_EQUAL( sw::string( "1/2 next: NotEnoughPoints (points 0/1)" ), findLine( writer, "base" ) );
    SW_EXPECT_EQUAL( sw::string( "0/1 next: LevelTooLow (needs level 3)" ), findLine( writer, "power" ) );
    SW_EXPECT_EQUAL( sw::string( "0/1 next: TierLocked (spent 1/2)" ), findLine( writer, "stance_a" ) );

    sw::DebugHUDSectionWriter atLevel3{ sw::SceneWalkTestAccess::getKey() };
    _state.writeDebugLines( atLevel3, 3, 16 );
    SW_EXPECT_EQUAL( sw::string( "0/1 next: RequirementMissing (needs base:2)" ), findLine( atLevel3, "power" ) );

    sw::DebugHUDSectionWriter capped{ sw::SceneWalkTestAccess::getKey() };
    _state.writeDebugLines( capped, 3, 2 );
    SW_EXPECT_EQUAL( sw::string( "3 more" ), findLine( capped, "..." ) );
}

/**
 * @brief [SkillTreeStateTest] 내리기는 선행 랭크 · 층을 깨면 거절하고, 깨지 않으면 점수를 돌려준다. 안 배운 스킬은 내릴 수 없다
 */
SW_TEST_CASE_F( SkillTreeStateTest, RankDownGuardsRequirementsAndTiers )
{
    _state.initialize( _catalog.find( "combat" ) );
    _state.addPoints( 10 );
    SW_EXPECT_EQUAL( sw::SkillResult::UnknownSkill, _state.rankDown( "base" ) );
    SW_EXPECT_EQUAL( sw::SkillResult::UnknownSkill, _state.rankDown( "nothing" ) );

    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "base", 3 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "base", 3 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "stance_a", 3 ) ); // 층 2 를 base 두 랭크로 연다

    SW_EXPECT_EQUAL( sw::SkillResult::RequiredByOther, _state.rankDown( "base" ) );
    SW_EXPECT_EQUAL( 2, _state.getRank( "base" ) );
    SW_EXPECT_EQUAL( 7, _state.getPoints() );

    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankDown( "stance_a" ) );
    SW_EXPECT_EQUAL( 8, _state.getPoints() );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "power", 3 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::RequiredByOther, _state.rankDown( "base" ) ); // 선행 랭크 2
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankDown( "power" ) );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankDown( "base" ) );
    SW_EXPECT_EQUAL( 1, _state.getRank( "base" ) );
    SW_EXPECT_EQUAL( 9, _state.getPoints() );
}

/**
 * @brief [SkillTreeStateTest] 결함: 위층 둘이 서로의 점수로 열린 것으로 쳐서 아래층을 환불할 수 있었다(닿을 수 없는 배치). 배우는 순서를 되짚어 거절한다
 */
SW_TEST_CASE_F( SkillTreeStateTest, RankDownCannotDropTheFloorUnderTwoUpperSkills )
{
    _state.initialize( _catalog.find( "tiers" ) );
    _state.addPoints( 3 );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "floor", 1 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "upper_a", 1 ) );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "upper_b", 1 ) );

    SW_EXPECT_EQUAL( sw::SkillResult::RequiredByOther, _state.rankDown( "floor" ) );
    SW_EXPECT_EQUAL( 1, _state.getRank( "floor" ) );
    SW_EXPECT_EQUAL( 0, _state.getPoints() );

    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankDown( "upper_a" ) ); // 위층은 순서 없이 내릴 수 있다
    SW_EXPECT_EQUAL( sw::SkillResult::RequiredByOther, _state.rankDown( "floor" ) );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankDown( "upper_b" ) );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankDown( "floor" ) );
    SW_EXPECT_EQUAL( 3, _state.getPoints() );
}

/**
 * @brief [SkillTreeStateTest] 결함: 다시 시작해도 남은 점수가 그대로라 앞 판의 점수를 들고 갔다. 트리가 없어도 조회는 멀쩡하다
 */
SW_TEST_CASE_F( SkillTreeStateTest, InitializeStartsOver )
{
    _state.initialize( _catalog.find( "combat" ) );
    _state.addPoints( 4 );
    SW_EXPECT_EQUAL( sw::SkillResult::Ok, _state.rankUp( "base", 1 ) );

    _state.initialize( _catalog.find( "tiers" ) );
    SW_EXPECT_EQUAL( 0, _state.getPoints() );
    SW_EXPECT_EQUAL( 0, _state.getSpentPoints() );

    _state.initialize( nullptr );
    SW_EXPECT_EQUAL( 0, _state.getRank( "base" ) );
    SW_EXPECT_EQUAL( sw::SkillResult::UnknownSkill, _state.evaluateRankUp( "base", 1 ) );
    SW_EXPECT_EQUAL( 0, _state.refundAll() );
}

/**
 * @brief [SkillTreeCatalogTest] 깨진 선행 랭크와 같은 id 의 스킬은 넣지 않고 알린다. 없는 선행 스킬은 경고하고 스킬은 남는다(영원히 잠긴다). `requires` 의 랭크 생략은 1 이다
 */
SW_TEST_CASE( SkillTreeCatalogTest, ReportsBrokenSkills )
{
    SW_EXPECT_LOG( "SkillTree.SkillInvalidRequirement" );
    SW_EXPECT_LOG( "SkillTree.SkillInvalidFieldsSkipped" );
    SW_EXPECT_LOG( "SkillTree.SkillDuplicateSkipped" );
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_LOG( "SkillTree.SkillNeverLearnable" );

    sw::SkillTreeCatalog  catalog;
    constexpr const utf8* kBrokenXML = R"(<SkillTreeCatalog><Tree id="t">
  <Skill id="base" maxRank="3"/>
  <Skill id="plain" requires="base"/>
  <Skill id="zero" requires="base:0"/>
  <Skill id="base" maxRank="9"/>
  <Skill id="orphan" requires="missing:1"/>
</Tree></SkillTreeCatalog>)";
    SW_EXPECT_FALSE( catalog.loadFromXMLText( kBrokenXML, "MonsterHuntTest" ) ); // 없는 선행 스킬(missing)로 로드는 실패다 — 읽은 트리의 판정은 그대로 본다
    const sw::SkillTreeDef* pTree = catalog.find( "t" );
    SW_REQUIRE_TRUE( pTree != nullptr );
    SW_REQUIRE_EQUAL( size_t( 3 ), pTree->_listSkill.size() ); // base · plain · orphan
    SW_EXPECT_EQUAL( 3, pTree->findSkill( "base" )->_maxRank );
    SW_REQUIRE_EQUAL( size_t( 1 ), pTree->findSkill( "plain" )->_listRequirement.size() );
    SW_EXPECT_EQUAL( 1, pTree->findSkill( "plain" )->_listRequirement[0]._rank );
    SW_EXPECT_TRUE( pTree->findSkill( "zero" ) == nullptr );

    sw::SkillTreeState state;
    state.initialize( pTree );
    state.addPoints( 1 );
    SW_EXPECT_EQUAL( sw::SkillResult::RequirementMissing, state.rankUp( "orphan", 1 ) );
}

/**
 * @brief [SkillTreeCatalogTest] 순환 · 자기 참조 · 도달 못 할 랭크 · 그것에 기대는 스킬은 영원히 못 배우는 것으로 찾아 알린다. 멀쩡한 사슬은 순서와 상관없이 지난다
 */
SW_TEST_CASE( SkillTreeCatalogTest, FindsUnlearnableSkills )
{
    SW_EXPECT_LOG( "SkillTree.SkillNeverLearnable" );
    sw::SkillTreeCatalog  catalog;
    constexpr const utf8* kXML = R"(<SkillTreeCatalog><Tree id="t">
  <Skill id="late" requires="early:2"/>
  <Skill id="early" maxRank="2"/>
  <Skill id="ringA" requires="ringB"/>
  <Skill id="ringB" requires="ringA"/>
  <Skill id="self" requires="self"/>
  <Skill id="tooHigh" requires="early:3"/>
  <Skill id="afterRing" requires="ringA,early"/>
</Tree></SkillTreeCatalog>)";
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kXML, "MonsterHuntTest" ) );
    const sw::SkillTreeDef* pTree = catalog.find( "t" );
    SW_REQUIRE_TRUE( pTree != nullptr );
    const sw::vector<sw::hashed_string> listUnlearnable = pTree->collectUnlearnableSkills();
    SW_EXPECT_EQUAL( size_t( 5 ), listUnlearnable.size() );
    for ( const utf8* pLearnable : { "late", "early" } )
    {
        SW_EXPECT_FALSE( sw::VectorUtil::contains( listUnlearnable, sw::hashed_string( pLearnable ) ) );
    }
}

/**
 * @brief [MonsterHuntCatalogTest] 약점은 다 읽은 뒤에 맞춰 보므로 뒤에 적은 물건을 가리켜도 된다
 */
SW_TEST_CASE( MonsterHuntCatalogTest, WeaknessMayNameLaterItem )
{
    sw::MonsterHuntCatalog catalog;
    constexpr const utf8*  kLaterXML = R"(<MonsterHuntCatalog>
  <Monster id="early" readLevel="1"><Weakness kind="Oil" id="later_oil"/><Weakness kind="Sign" id="later_sign"/></Monster>
  <Item id="later_oil" kind="Oil" hits="1"/>
  <Sign id="later_sign"/>
</MonsterHuntCatalog>)";
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kLaterXML, "MonsterHuntTest" ) );
    SW_REQUIRE_TRUE( catalog.findMonster( "early" ) != nullptr );
    SW_EXPECT_EQUAL( size_t( 2 ), catalog.findMonster( "early" )->_listWeakness.size() );
}

/**
 * @brief [MonsterHuntCatalogTest] 깨진 원소는 넣지 않고 알리며 다른 원소는 남는다: 적중 횟수 없는 오일, 상한 위 약점, 범위 밖 값, 깨진 단서가 든 계약, 모르는 원소. 없는 약점 이름은 알리고 괴물은 남는다
 */
SW_TEST_CASE( MonsterHuntCatalogTest, ReportsBrokenElements )
{
    SW_EXPECT_LOG( "MonsterHuntCatalog.OilNeedsHitsMore" );
    SW_EXPECT_LOG( "MonsterHuntCatalog.MonsterWeaknessNeedsKnowledge" );
    SW_EXPECT_LOG( "MonsterHuntCatalog.MonsterNamesUnknownWeakness" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );

    sw::MonsterHuntCatalog catalog;
    constexpr const utf8*  kBrokenXML = R"(<MonsterHuntCatalog>
  <Item id="dry_oil" kind="Oil"/>
  <Item id="good_bomb" kind="Bomb"/>
  <Monster id="too_deep" readLevel="1" maxKnowledge="2"><Weakness id="good_bomb" kind="Bomb" knowledge="3"/></Monster>
  <Monster id="no_kills" readLevel="1" killsPerLevel="0"/>
  <Monster id="lost" readLevel="1"><Weakness id="ghost_item"/></Monster>
  <Contract id="broken"><Step id="s"><Clue id="c" radius="-1"/></Step></Contract>
  <Mystery/>
</MonsterHuntCatalog>)";
    SW_EXPECT_FALSE( catalog.loadFromXMLText( kBrokenXML, "MonsterHuntTest" ) ); // 없는 약점(ghost_item)으로 로드는 실패다 — 읽은 항목의 판정은 그대로 본다
    SW_EXPECT_TRUE( catalog.findAlchemy( "dry_oil" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findAlchemy( "good_bomb" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findMonster( "too_deep" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findMonster( "no_kills" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findMonster( "lost" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findContract( "broken" ) == nullptr );
}
