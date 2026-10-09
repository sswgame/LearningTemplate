#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Combat/ElementChart.h"
#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Gameplay/Progression/SkillTree.h"
#include "GameFramework/Base/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Base/Gameplay/Quest/QuestLog.h"
#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherAlchemy.h"
#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherBestiary.h"
#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherCatalog.h"
#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherCombat.h"
#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherContract.h"
#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherMutagens.h"

#include "TestFramework/TestFramework.h"

// 위쳐 RPG 키트 — 카탈로그, 도감 지식(읽기 · 처치 · 조사)과 해금된 약점만 보이기 · 속성 배율, 연금술 제작 · 독성 한도 · 변이 혼합물 · 명상 보충 · 오일,
// 표식 기력 · 위력 · 대체 시전 해금 · 상태이상 결정성, 동작 스태미나 · 아드레날린, 변이 슬롯 열림 · 색 맞춤, 계약 단서 순서 · 일지 진행 · 보상 흥정.

using namespace sw;

namespace
{
    constexpr const utf8* kWitcherTestXml = R"(
<WitcherCatalog>
  <Alchemy maxToxicity="100" toxicityDecay="2" alcohol="dwarven_spirit,white_gull"/>
  <Combat stamina="100" regen="10" delay="1" fast="0" strong="15" dodge="5" roll="10" adrenalineMax="3" adrenalinePerHit="0.25" adrenalineLoss="1" adrenalineBonus="0.1"/>
  <Monster id="drowner" category="Necrophage" maxKnowledge="3" readLevel="3" killsPerLevel="2" killCap="2" investigateLevel="1">
    <Weakness kind="Oil" id="necrophage_oil" knowledge="1"/>
    <Weakness kind="Sign" id="igni" knowledge="2"/>
    <Weakness kind="Bomb" id="dimeritium_bomb" knowledge="3"/>
  </Monster>
  <Monster id="wraith" category="Specter" elements="Specter,Cursed" readLevel="2"/>
  <Item id="swallow" kind="Potion" toxicity="30" duration="20" charges="3"/>
  <Item id="thunderbolt" kind="Potion" toxicity="40" duration="30" charges="2"/>
  <Item id="ekimmara" kind="Decoction" toxicity="60" duration="100" charges="1"/>
  <Item id="necrophage_oil" kind="Oil" element="NecrophageOil" hits="3"/>
  <Item id="dimeritium_bomb" kind="Bomb" element="Dimeritium" charges="2"/>
  <Sign id="igni" element="Fire" cost="30" altCost="80" power="20" altPowerScale="1.5" altSkill="firestream"/>
  <Sign id="quen" element="Shield" cost="25" power="10"/>
  <SkillColor skill="muscle_memory" color="Red"/>
  <SkillColor skill="strength_training" color="Red"/>
  <SkillColor skill="firestream" color="Blue"/>
  <Mutagen id="red_greater" color="Red" stat="attackPower" value="10" matchValue="5"/>
  <SlotGroup level="1" slots="3"/>
  <SlotGroup level="8" slots="3"/>
  <Contract id="griffin" quest="contract_griffin" reward="200" limit="1.4" angerMax="1" angerScale="2" angerPerRound="0.1">
    <Step id="tracks">
      <Clue id="blood" x="0" z="0" radius="2" order="0"/>
      <Clue id="feathers" x="10" z="0" radius="2" order="1"/>
      <Clue id="carcass" x="10" z="10" radius="2" order="1"/>
    </Step>
    <Step id="lair">
      <Clue id="nest" x="50" z="50" radius="3"/>
    </Step>
  </Contract>
</WitcherCatalog>
)";

    constexpr const utf8* kElementXml = R"(
<ElementChart>
  <Element id="Fire"/><Element id="Shield"/><Element id="NecrophageOil"/><Element id="Dimeritium"/>
  <Element id="Necrophage"/><Element id="Specter"/><Element id="Cursed"/>
  <Rule attack="NecrophageOil" defend="Necrophage" multiplier="1.5"/>
  <Rule attack="Fire" defend="Necrophage" multiplier="1.25"/>
  <Rule attack="Fire" defend="Specter" multiplier="0.5"/>
  <Rule attack="Fire" defend="Cursed" multiplier="0.5"/>
  <Status element="Fire" status="Burn" chance="0.4"/>
</ElementChart>
)";

    constexpr const utf8* kWitcherRpgItemXml = R"(
<ItemCatalog>
  <Item id="swallow" maxStack="1"/><Item id="thunderbolt" maxStack="1"/><Item id="ekimmara" maxStack="1"/>
  <Item id="necrophage_oil" maxStack="1"/><Item id="dimeritium_bomb" maxStack="1"/>
  <Item id="celandine" maxStack="99"/><Item id="dwarven_spirit" maxStack="99"/>
</ItemCatalog>
)";

    constexpr const utf8* kRecipeXml = R"(
<RecipeCatalog>
  <Recipe id="brew_swallow"><In item="celandine" count="2"/><In item="dwarven_spirit" count="1"/><Out item="swallow" count="1"/></Recipe>
  <Recipe id="brew_thunderbolt"><In item="celandine" count="1"/><Out item="thunderbolt" count="1"/></Recipe>
  <Recipe id="brew_ekimmara"><In item="celandine" count="1"/><Out item="ekimmara" count="1"/></Recipe>
  <Recipe id="brew_oil"><In item="celandine" count="1"/><Out item="necrophage_oil" count="1"/></Recipe>
</RecipeCatalog>
)";

    struct WitcherTestData
    {
        WitcherCatalog _catalog;
        ElementChart   _chart;
        ItemCatalog    _itemCatalog;
        RecipeCatalog  _recipeCatalog;

        bool initialize()
        {
            return _catalog.loadFromXmlText( kWitcherTestXml, "WitcherRpgTest" ) && _chart.loadFromXmlText( kElementXml, "WitcherRpgTest" ) &&
                   _itemCatalog.loadFromXmlText( kWitcherRpgItemXml, "WitcherRpgTest" ) && _recipeCatalog.loadFromXmlText( kRecipeXml, "WitcherRpgTest" );
        }
    };

    bool hasWeakness( const vector<const WitcherWeakness*>& listWeakness, const utf8* pId )
    {
        for ( const WitcherWeakness* pWeakness : listWeakness )
        {
            if ( pWeakness->_id == hashed_string( pId ) )
                return true;
        }
        return false;
    }

    /** @brief 상태 하나의 바이트입니다. */
    template <typename TState>
    vector<uint8> captureWitcherBytes( const TState& state )
    {
        Archive archive;
        state.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }
} // namespace

SW_TEST_CASE( WitcherRpgTest, CatalogReadsMonstersAlchemySignsMutagensAndContracts )
{
    WitcherCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWitcherTestXml, "WitcherRpgTest" ) );
    const WitcherMonsterDef* pDrowner = catalog.findMonster( hashed_string( "drowner" ) );
    SW_ASSERT_NOT_NULL( pDrowner );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( pDrowner->_listWeakness.size() ) );
    SW_EXPECT_TRUE( pDrowner->_listWeakness[1]._kind == WitcherWeaknessKind::Sign );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( pDrowner->_listElement.size() ) ); // 속성이 없으면 분류 하나
    SW_EXPECT_TRUE( pDrowner->_listElement[0] == hashed_string( "Necrophage" ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( catalog.findMonster( hashed_string( "wraith" ) )->_listElement.size() ) );
    SW_EXPECT_TRUE( catalog.findAlchemy( hashed_string( "ekimmara" ) )->_kind == WitcherAlchemyKind::Decoction );
    SW_EXPECT_EQUAL( 3, catalog.findAlchemy( hashed_string( "necrophage_oil" ) )->_hits );
    SW_EXPECT_TRUE( catalog.findSign( hashed_string( "igni" ) )->_altSkill == hashed_string( "firestream" ) );
    SW_EXPECT_TRUE( catalog.getSkillColor( hashed_string( "muscle_memory" ) ) == hashed_string( "Red" ) );
    SW_EXPECT_TRUE( catalog.getSkillColor( hashed_string( "unknown" ) ).empty() );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( catalog.getSlotGroups().size() ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( catalog.getAlchemy()._listAlcohol.size() ) );
    SW_EXPECT_NEAR_EQUAL( 15.0f, catalog.getCombat()._strongCost, 1.0e-5f );
    const WitcherContractDef* pContract = catalog.findContract( hashed_string( "griffin" ) );
    SW_ASSERT_NOT_NULL( pContract );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( pContract->_listStep.size() ) );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pContract->_listStep[0]._listClue[2]._position._z, 1.0e-5f );
}

SW_TEST_CASE( WitcherRpgTest, BestiaryShowsOnlyUnlockedWeaknessesWhileMultipliersAlwaysApply )
{
    WitcherTestData data;
    SW_ASSERT_TRUE( data.initialize() );
    WitcherBestiary bestiary;
    bestiary.initialize( &data._catalog, &data._chart );
    const hashed_string            drowner( "drowner" );
    vector<const WitcherWeakness*> listWeakness;

    // 처음에는 아무 약점도 보이지 않지만 배율은 그대로 들어간다(지식은 보여 주기만 정한다).
    bestiary.collectKnownWeaknesses( drowner, listWeakness );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( listWeakness.size() ) );
    SW_EXPECT_NEAR_EQUAL( 1.5f, bestiary.computeMultiplier( drowner, hashed_string( "NecrophageOil" ) ), 1.0e-5f );

    SW_EXPECT_TRUE( bestiary.investigate( drowner ) );
    SW_EXPECT_FALSE( bestiary.investigate( drowner ) ); // 조사로는 1 까지
    bestiary.collectKnownWeaknesses( drowner, listWeakness );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listWeakness.size() ) );
    SW_EXPECT_TRUE( hasWeakness( listWeakness, "necrophage_oil" ) );

    // 처치는 둘마다 한 단계, 처치 상한 2 에서 멈춘다.
    SW_EXPECT_FALSE( bestiary.recordKill( drowner ) );
    SW_EXPECT_TRUE( bestiary.recordKill( drowner ) == false ); // 2 / 2 = 1 — 이미 1
    SW_EXPECT_FALSE( bestiary.recordKill( drowner ) );
    SW_EXPECT_TRUE( bestiary.recordKill( drowner ) ); // 4 / 2 = 2
    for ( int32 index = 0; index < 10; ++index )
    {
        (void)bestiary.recordKill( drowner );
    }
    SW_EXPECT_EQUAL( 2, bestiary.getKnowledge( drowner ) );
    SW_EXPECT_EQUAL( 14, bestiary.getKillCount( drowner ) );
    bestiary.collectKnownWeaknesses( drowner, listWeakness );
    SW_EXPECT_TRUE( hasWeakness( listWeakness, "igni" ) && hasWeakness( listWeakness, "dimeritium_bomb" ) == false );

    // 책을 읽어야 마지막 약점까지.
    SW_EXPECT_TRUE( bestiary.readBook( drowner ) );
    bestiary.collectKnownWeaknesses( drowner, listWeakness );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listWeakness.size() ) );
    vector<WitcherBestiaryEvent> listEvent;
    bestiary.drainEvents( listEvent );
    int32 revealed = 0;
    for ( const WitcherBestiaryEvent& event : listEvent )
    {
        revealed += event._kind == WitcherBestiaryEvent::Kind::WeaknessRevealed ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 3, revealed );

    // 복합 속성은 곱 — 불은 망령(0.5 × 0.5)에게 약하다.
    SW_EXPECT_NEAR_EQUAL( 0.25f, bestiary.computeMultiplier( hashed_string( "wraith" ), hashed_string( "Fire" ) ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, bestiary.computeMultiplier( hashed_string( "griffin" ), hashed_string( "Fire" ) ), 1.0e-5f );
    SW_EXPECT_FALSE( bestiary.readBook( hashed_string( "griffin" ) ) );
}

SW_TEST_CASE( WitcherRpgTest, AlchemyLimitsToxicityLocksDecoctionsAndMeditationRefillsWithAlcohol )
{
    WitcherTestData data;
    SW_ASSERT_TRUE( data.initialize() );
    Inventory inventory;
    inventory.initialize( &data._itemCatalog, 20 );
    SW_EXPECT_EQUAL( 10, inventory.addItem( hashed_string( "celandine" ), 10 ) );
    SW_EXPECT_EQUAL( 2, inventory.addItem( hashed_string( "dwarven_spirit" ), 2 ) );
    WitcherAlchemy alchemy;
    alchemy.initialize( &data._catalog, &data._recipeCatalog );

    const hashed_string swallow( "swallow" );
    SW_EXPECT_TRUE( alchemy.drink( swallow, inventory ) == WitcherUseResult::NotInInventory );
    SW_EXPECT_TRUE( alchemy.brew( hashed_string( "brew_swallow" ), inventory, hashed_string{}, 1 ) == CraftResult::Ok );
    SW_EXPECT_TRUE( alchemy.brew( hashed_string( "brew_thunderbolt" ), inventory, hashed_string{}, 1 ) == CraftResult::Ok );
    SW_EXPECT_TRUE( alchemy.brew( hashed_string( "brew_ekimmara" ), inventory, hashed_string{}, 1 ) == CraftResult::Ok );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( hashed_string( "dwarven_spirit" ) ) ); // 재료로 하나 썼다
    SW_EXPECT_EQUAL( 3, alchemy.getCharges( swallow ) );
    SW_EXPECT_TRUE( alchemy.applyOil( swallow, inventory ) == WitcherUseResult::WrongKind );

    // 독성: 30 + 40 = 70, 하나 더(30)는 100 까지 — 딱 맞으면 된다, 넘으면 안 된다.
    SW_EXPECT_TRUE( alchemy.drink( swallow, inventory ) == WitcherUseResult::Ok );
    SW_EXPECT_TRUE( alchemy.drink( hashed_string( "thunderbolt" ), inventory ) == WitcherUseResult::Ok );
    SW_EXPECT_TRUE( alchemy.drink( swallow, inventory ) == WitcherUseResult::Ok );
    SW_EXPECT_NEAR_EQUAL( 100.0f, alchemy.getToxicity(), 1.0e-4f );
    SW_EXPECT_TRUE( alchemy.drink( swallow, inventory ) == WitcherUseResult::TooToxic );
    SW_EXPECT_EQUAL( 1, alchemy.getCharges( swallow ) ); // 실패한 시도는 쓰지 않는다

    // 서서히 준다(초당 2).
    alchemy.update( 25.0f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, alchemy.getToxicity(), 1.0e-3f );
    SW_EXPECT_FALSE( alchemy.isEffectActive( swallow ) ); // 20 초 효과는 끝났다
    SW_EXPECT_TRUE( alchemy.isEffectActive( hashed_string( "thunderbolt" ) ) );
    alchemy.update( 25.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, alchemy.getToxicity(), 1.0e-3f );

    // 변이 혼합물의 독성은 효과 동안 줄지 않는다.
    const hashed_string ekimmara( "ekimmara" );
    SW_EXPECT_TRUE( alchemy.drink( ekimmara, inventory ) == WitcherUseResult::Ok );
    alchemy.update( 50.0f );
    SW_EXPECT_NEAR_EQUAL( 60.0f, alchemy.getToxicity(), 1.0e-3f );
    SW_EXPECT_TRUE( alchemy.drink( swallow, inventory ) == WitcherUseResult::Ok ); // 60 + 30 = 90
    SW_EXPECT_TRUE( alchemy.drink( swallow, inventory ) == WitcherUseResult::NoCharges );
    alchemy.update( 51.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, alchemy.getToxicity(), 1.0e-3f );
    SW_EXPECT_TRUE( alchemy.drink( ekimmara, inventory ) == WitcherUseResult::NoCharges );

    // 명상 — 술 하나로 모두 가득. 술이 없으면 못 한다.
    SW_ASSERT_TRUE( alchemy.meditate( inventory ) );
    SW_EXPECT_EQUAL( 0, inventory.getItemCount( hashed_string( "dwarven_spirit" ) ) );
    SW_EXPECT_EQUAL( 3, alchemy.getCharges( swallow ) );
    SW_EXPECT_EQUAL( 1, alchemy.getCharges( ekimmara ) );
    SW_EXPECT_FALSE( alchemy.meditate( inventory ) );

    // 오일 — 적중마다 하나씩, 다 쓰면 칼이 빈다.
    SW_EXPECT_TRUE( alchemy.brew( hashed_string( "brew_oil" ), inventory, hashed_string{}, 1 ) == CraftResult::Ok );
    SW_EXPECT_TRUE( alchemy.applyOil( hashed_string( "necrophage_oil" ), inventory ) == WitcherUseResult::Ok );
    SW_EXPECT_TRUE( alchemy.consumeOilHit() == hashed_string( "NecrophageOil" ) );
    SW_EXPECT_TRUE( alchemy.consumeOilHit() == hashed_string( "NecrophageOil" ) );
    SW_EXPECT_TRUE( alchemy.consumeOilHit() == hashed_string( "NecrophageOil" ) );
    SW_EXPECT_TRUE( alchemy.consumeOilHit().empty() );
    SW_EXPECT_TRUE( alchemy.applyOil( hashed_string( "necrophage_oil" ), inventory ) == WitcherUseResult::NoCharges );
}

SW_TEST_CASE( WitcherRpgTest, SignsSpendStaminaScaleWithIntensityAndAlternateCastNeedsSkill )
{
    WitcherTestData data;
    SW_ASSERT_TRUE( data.initialize() );
    SkillTreeCatalog skillCatalog;
    SW_ASSERT_TRUE( skillCatalog.loadFromXmlText( R"(<SkillTreeCatalog><Tree id="signs"><Skill id="firestream" maxRank="1" cost="1"/></Tree></SkillTreeCatalog>)",
                                                  "WitcherRpgTest" ) );
    SkillTreeState skills;
    skills.initialize( skillCatalog.findTree( hashed_string( "signs" ) ) );
    StatBlock stats;
    stats.setValue( hashed_string( "signIntensity" ), 50.0f );

    WitcherCombat combat;
    combat.initialize( &data._catalog, 11u );
    WitcherSignCast cast = combat.castSign( hashed_string( "igni" ), true, &skills, stats, &data._chart );
    SW_EXPECT_TRUE( cast._result == WitcherCombatResult::AlternateLocked );
    SW_EXPECT_NEAR_EQUAL( 100.0f, combat.getStamina().getValue(), 1.0e-4f ); // 잠겨 있으면 쓰지 않는다
    cast = combat.castSign( hashed_string( "igni" ), false, &skills, stats, &data._chart );
    SW_EXPECT_TRUE( cast._result == WitcherCombatResult::Ok );
    SW_EXPECT_NEAR_EQUAL( 30.0f, cast._power, 1.0e-4f ); // 20 × 1.5
    SW_EXPECT_NEAR_EQUAL( 70.0f, combat.getStamina().getValue(), 1.0e-4f );
    cast = combat.castSign( hashed_string( "igni" ), true, nullptr, stats, &data._chart );
    SW_EXPECT_TRUE( cast._result == WitcherCombatResult::AlternateLocked );

    skills.addPoints( 1 );
    SW_EXPECT_TRUE( skills.rankUp( hashed_string( "firestream" ), 1 ) == SkillResult::Ok );
    cast = combat.castSign( hashed_string( "igni" ), true, &skills, stats, &data._chart );
    SW_EXPECT_TRUE( cast._result == WitcherCombatResult::NotEnoughStamina ); // 대체 시전은 80
    combat.update( 10.0f );
    cast = combat.castSign( hashed_string( "igni" ), true, &skills, stats, &data._chart );
    SW_EXPECT_TRUE( cast._result == WitcherCombatResult::Ok );
    SW_EXPECT_NEAR_EQUAL( 45.0f, cast._power, 1.0e-4f ); // 30 × 1.5
    SW_EXPECT_TRUE( combat.castSign( hashed_string( "axii" ), false, &skills, stats, nullptr )._result == WitcherCombatResult::UnknownSign );

    // 상태이상은 씨앗이 같으면 같고, 확률이 없는 표식은 걸지 않는다.
    vector<hashed_string> arrStatus[2];
    for ( int32 trial = 0; trial < 2; ++trial )
    {
        WitcherCombat caster;
        caster.initialize( &data._catalog, 1234u );
        for ( int32 index = 0; index < 20; ++index )
        {
            caster.getStamina().refill();
            arrStatus[trial].push_back( caster.castSign( hashed_string( "igni" ), false, nullptr, stats, &data._chart )._status );
        }
        caster.getStamina().refill();
        SW_EXPECT_TRUE( caster.castSign( hashed_string( "quen" ), false, nullptr, stats, &data._chart )._status.empty() );
    }
    int32 burned = 0;
    for ( size_t index = 0; index < arrStatus[0].size(); ++index )
    {
        SW_EXPECT_TRUE( arrStatus[0][index] == arrStatus[1][index] );
        burned += arrStatus[0][index] == hashed_string( "Burn" ) ? 1 : 0;
    }
    SW_EXPECT_TRUE( burned > 0 && burned < 20 );
}

SW_TEST_CASE( WitcherRpgTest, ActionsCostStaminaAndAdrenalineBuildsOnHitsAndDropsWhenHit )
{
    WitcherTestData data;
    SW_ASSERT_TRUE( data.initialize() );
    WitcherCombat combat;
    combat.initialize( &data._catalog, 1u );
    SW_EXPECT_TRUE( combat.performAction( WitcherAction::FastAttack ) == WitcherCombatResult::Ok );
    SW_EXPECT_NEAR_EQUAL( 100.0f, combat.getStamina().getValue(), 1.0e-4f ); // 빠른 공격은 공짜
    for ( int32 index = 0; index < 6; ++index )
    {
        SW_EXPECT_TRUE( combat.performAction( WitcherAction::StrongAttack ) == WitcherCombatResult::Ok );
    }
    SW_EXPECT_NEAR_EQUAL( 10.0f, combat.getStamina().getValue(), 1.0e-4f );
    SW_EXPECT_TRUE( combat.performAction( WitcherAction::StrongAttack ) == WitcherCombatResult::NotEnoughStamina );
    SW_EXPECT_TRUE( combat.performAction( WitcherAction::Roll ) == WitcherCombatResult::Ok );
    SW_EXPECT_TRUE( combat.performAction( WitcherAction::Dodge ) == WitcherCombatResult::NotEnoughStamina );
    combat.update( 1.5f ); // 지연 1 초 뒤 0.5 초 × 10
    SW_EXPECT_TRUE( combat.performAction( WitcherAction::Dodge ) == WitcherCombatResult::Ok );

    // 아드레날린: 적중 4 번에 1 포인트, 피격하면 1 포인트 잃는다, 3 에서 멈춘다.
    for ( int32 index = 0; index < 3; ++index )
    {
        combat.registerHitLanded();
    }
    SW_EXPECT_EQUAL( 0, combat.getAdrenalinePoints() );
    combat.registerHitLanded();
    SW_EXPECT_EQUAL( 1, combat.getAdrenalinePoints() );
    SW_EXPECT_NEAR_EQUAL( 1.1f, combat.computeDamageScale(), 1.0e-5f );
    combat.registerHitTaken();
    SW_EXPECT_EQUAL( 0, combat.getAdrenalinePoints() );
    combat.registerHitTaken();
    SW_EXPECT_NEAR_EQUAL( 0.0f, combat.getAdrenaline(), 1.0e-5f );
    for ( int32 index = 0; index < 40; ++index )
    {
        combat.registerHitLanded();
    }
    SW_EXPECT_EQUAL( 3, combat.getAdrenalinePoints() );
    SW_EXPECT_NEAR_EQUAL( 1.3f, combat.computeDamageScale(), 1.0e-5f );
    SW_EXPECT_EQUAL( 3, combat.consumeAdrenalinePoints() );
    SW_EXPECT_EQUAL( 0, combat.getAdrenalinePoints() );
}

SW_TEST_CASE( WitcherRpgTest, MutagenSlotsOpenByLevelAndMatchingColorsRaiseTheBonus )
{
    WitcherCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWitcherTestXml, "WitcherRpgTest" ) );
    WitcherMutagens mutagens;
    mutagens.initialize( &catalog, 5 );
    SW_EXPECT_EQUAL( 2, mutagens.getGroupCount() );
    SW_EXPECT_TRUE( mutagens.isGroupOpen( 0 ) && mutagens.isGroupOpen( 1 ) == false );
    SW_EXPECT_TRUE( mutagens.equipSkill( 1, 0, hashed_string( "muscle_memory" ) ) == WitcherSlotResult::Locked );
    SW_EXPECT_TRUE( mutagens.equipSkill( 0, 3, hashed_string( "muscle_memory" ) ) == WitcherSlotResult::InvalidSlot );
    SW_EXPECT_TRUE( mutagens.equipMutagen( 0, hashed_string( "blue_lesser" ) ) == WitcherSlotResult::UnknownMutagen );
    SW_EXPECT_TRUE( mutagens.equipSkill( 0, 0, hashed_string( "muscle_memory" ) ) == WitcherSlotResult::Ok );
    SW_EXPECT_TRUE( mutagens.equipSkill( 0, 1, hashed_string( "muscle_memory" ) ) == WitcherSlotResult::AlreadyEquipped );
    SW_EXPECT_TRUE( mutagens.equipSkill( 0, 1, hashed_string( "firestream" ) ) == WitcherSlotResult::Ok );
    SW_EXPECT_TRUE( mutagens.equipMutagen( 0, hashed_string( "red_greater" ) ) == WitcherSlotResult::Ok );

    StatBlock stats;
    mutagens.computeStats( stats );
    SW_EXPECT_EQUAL( 1, mutagens.countMatches( 0 ) ); // 파란 스킬은 맞지 않는다
    SW_EXPECT_NEAR_EQUAL( 15.0f, stats.getValue( hashed_string( "attackPower" ) ), 1.0e-4f );
    SW_EXPECT_TRUE( mutagens.equipSkill( 0, 2, hashed_string( "strength_training" ) ) == WitcherSlotResult::Ok );
    stats.clear();
    mutagens.computeStats( stats );
    SW_EXPECT_NEAR_EQUAL( 20.0f, stats.getValue( hashed_string( "attackPower" ) ), 1.0e-4f );

    // 레벨이 오르면 둘째 묶음이 열린다. 레벨이 내려가 닫힌 묶음은 능력치에 들지 않는다.
    mutagens.setCharacterLevel( 8 );
    SW_EXPECT_TRUE( mutagens.equipMutagen( 1, hashed_string( "red_greater" ) ) == WitcherSlotResult::Ok );
    stats.clear();
    mutagens.computeStats( stats );
    SW_EXPECT_NEAR_EQUAL( 30.0f, stats.getValue( hashed_string( "attackPower" ) ), 1.0e-4f );
    mutagens.setCharacterLevel( 1 );
    stats.clear();
    mutagens.computeStats( stats );
    SW_EXPECT_NEAR_EQUAL( 20.0f, stats.getValue( hashed_string( "attackPower" ) ), 1.0e-4f );
    vector<hashed_string> listSkill;
    mutagens.collectEquippedSkills( listSkill );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listSkill.size() ) );
}

SW_TEST_CASE( WitcherRpgTest, ContractCluesFollowOrderAdvanceTheQuestAndGreedyHaggleBreaksOff )
{
    WitcherCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWitcherTestXml, "WitcherRpgTest" ) );
    QuestCatalog questCatalog;
    SW_ASSERT_TRUE( questCatalog.loadFromXmlText( R"(
<QuestCatalog>
  <Quest id="contract_griffin">
    <Stage id="tracks" next="lair"><Objective kind="Investigate" target="tracks"/></Stage>
    <Stage id="lair" next="slay"><Objective kind="Investigate" target="lair"/></Stage>
    <Stage id="slay" next="paid"><Objective kind="Kill" target="griffin"/></Stage>
    <Stage id="paid" complete="true"/>
  </Quest>
</QuestCatalog>)",
                                                  "WitcherRpgTest" ) );
    QuestLog questLog;
    questLog.initialize( &questCatalog );
    SW_ASSERT_TRUE( questLog.start( hashed_string( "contract_griffin" ), 1 ) == QuestStartResult::Ok );

    WitcherInvestigation investigation;
    SW_EXPECT_FALSE( investigation.initialize( &catalog, hashed_string( "hydra" ), &questLog ) );
    SW_ASSERT_TRUE( investigation.initialize( &catalog, hashed_string( "griffin" ), &questLog ) );

    // 위쳐 감각은 순서가 열린 단서만 비춘다.
    vector<const WitcherClueDef*> listClue;
    investigation.senseClues( float3{ 5.0f, 0.0f, 5.0f }, 20.0f, listClue );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listClue.size() ) );
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "feathers" ), float3{ 10.0f, 0.0f, 0.0f } ) == WitcherClueResult::OutOfOrder );
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "blood" ), float3{ 3.0f, 0.0f, 0.0f } ) == WitcherClueResult::TooFar );
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "nest" ), float3{ 50.0f, 0.0f, 50.0f } ) == WitcherClueResult::UnknownClue );
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "blood" ), float3{ 1.0f, 0.0f, 1.0f } ) == WitcherClueResult::Found );
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "blood" ), float3{ 1.0f, 0.0f, 1.0f } ) == WitcherClueResult::AlreadyFound );
    investigation.senseClues( float3{ 5.0f, 0.0f, 5.0f }, 20.0f, listClue );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( listClue.size() ) ); // 같은 순서 둘이 함께 열린다
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "carcass" ), float3{ 10.0f, 0.0f, 9.0f } ) == WitcherClueResult::Found );
    SW_EXPECT_TRUE( questLog.findProgress( hashed_string( "contract_griffin" ) )->_stageId == hashed_string( "tracks" ) );
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "feathers" ), float3{ 10.0f, 0.0f, 1.0f } ) == WitcherClueResult::Found );
    SW_EXPECT_TRUE( investigation.getStepId() == hashed_string( "lair" ) );
    SW_EXPECT_TRUE( questLog.findProgress( hashed_string( "contract_griffin" ) )->_stageId == hashed_string( "lair" ) );
    SW_EXPECT_TRUE( investigation.isClueFound( hashed_string( "blood" ) ) );
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "nest" ), float3{ 52.0f, 0.0f, 50.0f } ) == WitcherClueResult::Found );
    SW_EXPECT_TRUE( investigation.isSolved() );
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "nest" ), float3{ 52.0f, 0.0f, 50.0f } ) == WitcherClueResult::Solved );
    SW_EXPECT_TRUE( questLog.findProgress( hashed_string( "contract_griffin" ) )->_stageId == hashed_string( "slay" ) );
    vector<WitcherInvestigationEvent> listEvent;
    investigation.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( listEvent.size() ) ); // 단서 4 + 단계 2 + 해결 1

    // 흥정: 한도(280) 안이면 받는다.
    WitcherHaggle fair;
    SW_ASSERT_TRUE( fair.initialize( &catalog, hashed_string( "griffin" ) ) );
    SW_EXPECT_TRUE( fair.propose( 280 ) == WitcherHaggleResult::Accepted );
    SW_EXPECT_EQUAL( 280, fair.getFinalReward() );
    SW_EXPECT_TRUE( fair.propose( 300 ) == WitcherHaggleResult::Closed );

    // 조금 넘기면 반 올린 제안, 그걸 받으면 끝.
    WitcherHaggle patient;
    SW_ASSERT_TRUE( patient.initialize( &catalog, hashed_string( "griffin" ) ) );
    SW_EXPECT_TRUE( patient.propose( 300 ) == WitcherHaggleResult::Countered ); // 분노 0.1 + 20/200 × 2 = 0.3
    SW_EXPECT_EQUAL( 250, patient.getOffer() );
    SW_EXPECT_NEAR_EQUAL( 0.3f, patient.getAnger(), 1.0e-4f );
    SW_EXPECT_EQUAL( 250, patient.acceptOffer() );

    // 욕심내면 분노가 차 결렬 — 처음 보상만.
    WitcherHaggle greedy;
    SW_ASSERT_TRUE( greedy.initialize( &catalog, hashed_string( "griffin" ) ) );
    SW_EXPECT_TRUE( greedy.propose( 320 ) == WitcherHaggleResult::Countered ); // 0.1 + 0.4 = 0.5
    SW_EXPECT_TRUE( greedy.propose( 340 ) == WitcherHaggleResult::BrokenOff ); // 0.5 + 0.1 + 0.6 = 1.2
    SW_EXPECT_EQUAL( 200, greedy.getFinalReward() );
    SW_EXPECT_TRUE( greedy.isClosed() );
}

/**
 * @brief [WitcherRpgTest] 연금 · 도감 · 전투 · 조사 · 흥정 · 변이 상태 바이트 — 충전 · 효과 · 오일, 지식, 스태미나 · 난수 · 아드레날린, 계약 단계 · 찾은 단서, 제안 · 분노,
 *        끼운 스킬 · 변이원이 그대로 와서 같은 걸음이 이어진다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( WitcherRpgTest, StateRoundTripContinuesTheSameHunt )
{
    WitcherTestData data;
    SW_ASSERT_TRUE( data.initialize() );

    // 연금 — 제비를 마시고 오일을 발라 한 번 벴다.
    Inventory inventory;
    inventory.initialize( &data._itemCatalog, 20 );
    SW_EXPECT_EQUAL( 10, inventory.addItem( hashed_string( "celandine" ), 10 ) );
    SW_EXPECT_EQUAL( 2, inventory.addItem( hashed_string( "dwarven_spirit" ), 2 ) );
    WitcherAlchemy alchemy;
    alchemy.initialize( &data._catalog, &data._recipeCatalog );
    SW_ASSERT_TRUE( alchemy.brew( hashed_string( "brew_swallow" ), inventory, hashed_string{}, 1 ) == CraftResult::Ok );
    SW_ASSERT_TRUE( alchemy.brew( hashed_string( "brew_thunderbolt" ), inventory, hashed_string{}, 1 ) == CraftResult::Ok );
    SW_ASSERT_TRUE( alchemy.brew( hashed_string( "brew_oil" ), inventory, hashed_string{}, 1 ) == CraftResult::Ok );
    SW_ASSERT_TRUE( alchemy.drink( hashed_string( "swallow" ), inventory ) == WitcherUseResult::Ok );
    SW_ASSERT_TRUE( alchemy.applyOil( hashed_string( "necrophage_oil" ), inventory ) == WitcherUseResult::Ok );
    (void)alchemy.consumeOilHit();
    alchemy.update( 5.0f );

    const vector<uint8> alchemyBytes = captureWitcherBytes( alchemy );
    WitcherAlchemy      restoredAlchemy;
    restoredAlchemy.initialize( &data._catalog, &data._recipeCatalog );
    Archive alchemyReader( alchemyBytes.data(), alchemyBytes.size() );
    SW_ASSERT_TRUE( restoredAlchemy.readState( alchemyReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, alchemyReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureWitcherBytes( restoredAlchemy ) == alchemyBytes );
    SW_EXPECT_EQUAL( 2, restoredAlchemy.getOilHits() );
    SW_EXPECT_NEAR_EQUAL( alchemy.getToxicity(), restoredAlchemy.getToxicity(), 1.0e-4f );

    // 같은 걸음을 둘 다 더 돌리면 바이트가 같다.
    alchemy.update( 10.0f );
    restoredAlchemy.update( 10.0f );
    SW_EXPECT_TRUE( alchemy.drink( hashed_string( "thunderbolt" ), inventory ) == restoredAlchemy.drink( hashed_string( "thunderbolt" ), inventory ) );
    SW_EXPECT_TRUE( alchemy.consumeOilHit() == restoredAlchemy.consumeOilHit() );
    SW_EXPECT_TRUE( captureWitcherBytes( alchemy ) == captureWitcherBytes( restoredAlchemy ) );

    WitcherAlchemy truncatedAlchemy;
    truncatedAlchemy.initialize( &data._catalog, &data._recipeCatalog );
    Archive alchemyCut( alchemyBytes.data(), alchemyBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedAlchemy.readState( alchemyCut ) );
    SW_EXPECT_TRUE( truncatedAlchemy.getActiveEffects().empty() );

    // 도감 — 조사 한 번 · 처치 둘.
    const hashed_string drowner( "drowner" );
    WitcherBestiary     bestiary;
    bestiary.initialize( &data._catalog, &data._chart );
    SW_EXPECT_TRUE( bestiary.investigate( drowner ) );
    (void)bestiary.recordKill( drowner );
    (void)bestiary.recordKill( drowner );

    const vector<uint8> bestiaryBytes = captureWitcherBytes( bestiary );
    WitcherBestiary     restoredBestiary;
    restoredBestiary.initialize( &data._catalog, &data._chart );
    Archive bestiaryReader( bestiaryBytes.data(), bestiaryBytes.size() );
    SW_ASSERT_TRUE( restoredBestiary.readState( bestiaryReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, bestiaryReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureWitcherBytes( restoredBestiary ) == bestiaryBytes );
    SW_EXPECT_EQUAL( bestiary.getKnowledge( drowner ), restoredBestiary.getKnowledge( drowner ) );
    for ( int32 kill = 0; kill < 2; ++kill )
    {
        SW_EXPECT_TRUE( bestiary.recordKill( drowner ) == restoredBestiary.recordKill( drowner ) );
    }
    SW_EXPECT_TRUE( captureWitcherBytes( bestiary ) == captureWitcherBytes( restoredBestiary ) );

    WitcherBestiary truncatedBestiary;
    truncatedBestiary.initialize( &data._catalog, &data._chart );
    Archive bestiaryCut( bestiaryBytes.data(), bestiaryBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedBestiary.readState( bestiaryCut ) );
    SW_EXPECT_EQUAL( 0, truncatedBestiary.getKnowledge( drowner ) );

    // 전투 — 강공 한 번 · 적중 셋으로 아드레날린이 쌓였다.
    WitcherCombat combat;
    combat.initialize( &data._catalog, 11u );
    SW_ASSERT_TRUE( combat.performAction( WitcherAction::StrongAttack ) == WitcherCombatResult::Ok );
    for ( int32 hit = 0; hit < 3; ++hit )
    {
        combat.registerHitLanded();
    }
    combat.update( 0.5f );

    const vector<uint8> combatBytes = captureWitcherBytes( combat );
    WitcherCombat       restoredCombat;
    restoredCombat.initialize( &data._catalog, 99u );
    Archive combatReader( combatBytes.data(), combatBytes.size() );
    SW_ASSERT_TRUE( restoredCombat.readState( combatReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, combatReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureWitcherBytes( restoredCombat ) == combatBytes );
    SW_EXPECT_NEAR_EQUAL( combat.getAdrenaline(), restoredCombat.getAdrenaline(), 1.0e-5f );

    const StatBlock stats;
    for ( int32 cast = 0; cast < 3; ++cast ) // 상태이상 굴림이 같은 난수를 쓴다
    {
        const WitcherSignCast original    = combat.castSign( hashed_string( "igni" ), false, nullptr, stats, &data._chart );
        const WitcherSignCast restoredOne = restoredCombat.castSign( hashed_string( "igni" ), false, nullptr, stats, &data._chart );
        SW_EXPECT_TRUE( original._status == restoredOne._status && original._result == restoredOne._result );
        combat.update( 2.0f );
        restoredCombat.update( 2.0f );
    }
    SW_EXPECT_TRUE( captureWitcherBytes( combat ) == captureWitcherBytes( restoredCombat ) );

    WitcherCombat truncatedCombat;
    truncatedCombat.initialize( &data._catalog, 99u );
    Archive combatCut( combatBytes.data(), combatBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedCombat.readState( combatCut ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, truncatedCombat.getAdrenaline(), 1.0e-5f );

    // 조사 — 피 자국을 찾았다. 일지는 빌린 것이라 둘이 각자 든다.
    QuestCatalog questCatalog;
    SW_ASSERT_TRUE( questCatalog.loadFromXmlText( R"(
<QuestCatalog>
  <Quest id="contract_griffin">
    <Stage id="tracks" next="lair"><Objective kind="Investigate" target="tracks"/></Stage>
    <Stage id="lair" next="paid"><Objective kind="Investigate" target="lair"/></Stage>
    <Stage id="paid" complete="true"/>
  </Quest>
</QuestCatalog>)",
                                                  "WitcherRpgTest" ) );
    QuestLog questLog;
    QuestLog restoredQuestLog;
    questLog.initialize( &questCatalog );
    restoredQuestLog.initialize( &questCatalog );
    SW_ASSERT_TRUE( questLog.start( hashed_string( "contract_griffin" ), 1 ) == QuestStartResult::Ok );
    SW_ASSERT_TRUE( restoredQuestLog.start( hashed_string( "contract_griffin" ), 1 ) == QuestStartResult::Ok );
    WitcherInvestigation investigation;
    SW_ASSERT_TRUE( investigation.initialize( &data._catalog, hashed_string( "griffin" ), &questLog ) );
    SW_ASSERT_TRUE( investigation.investigate( hashed_string( "blood" ), float3{ 1.0f, 0.0f, 1.0f } ) == WitcherClueResult::Found );

    const vector<uint8>  investigationBytes = captureWitcherBytes( investigation );
    WitcherInvestigation restoredInvestigation;
    SW_ASSERT_TRUE( restoredInvestigation.initialize( &data._catalog, hashed_string( "griffin" ), &restoredQuestLog ) );
    Archive investigationReader( investigationBytes.data(), investigationBytes.size() );
    SW_ASSERT_TRUE( restoredInvestigation.readState( investigationReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, investigationReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureWitcherBytes( restoredInvestigation ) == investigationBytes );
    SW_EXPECT_TRUE( restoredInvestigation.isClueFound( hashed_string( "blood" ) ) );

    SW_EXPECT_TRUE( restoredInvestigation.investigate( hashed_string( "carcass" ), float3{ 10.0f, 0.0f, 9.0f } ) == WitcherClueResult::Found );
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "carcass" ), float3{ 10.0f, 0.0f, 9.0f } ) == WitcherClueResult::Found );
    SW_EXPECT_TRUE( restoredInvestigation.investigate( hashed_string( "feathers" ), float3{ 10.0f, 0.0f, 1.0f } ) == WitcherClueResult::Found );
    SW_EXPECT_TRUE( investigation.investigate( hashed_string( "feathers" ), float3{ 10.0f, 0.0f, 1.0f } ) == WitcherClueResult::Found );
    SW_EXPECT_TRUE( restoredInvestigation.getStepId() == hashed_string( "lair" ) ); // 단계가 이어진다
    SW_EXPECT_TRUE( restoredQuestLog.findProgress( hashed_string( "contract_griffin" ) )->_stageId == hashed_string( "lair" ) );
    SW_EXPECT_TRUE( captureWitcherBytes( investigation ) == captureWitcherBytes( restoredInvestigation ) );

    WitcherInvestigation truncatedInvestigation;
    SW_ASSERT_TRUE( truncatedInvestigation.initialize( &data._catalog, hashed_string( "griffin" ), nullptr ) );
    Archive investigationCut( investigationBytes.data(), investigationBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedInvestigation.readState( investigationCut ) );
    SW_EXPECT_FALSE( truncatedInvestigation.isClueFound( hashed_string( "blood" ) ) );

    // 흥정 — 한 번 맞제안을 받았다.
    WitcherHaggle haggle;
    SW_ASSERT_TRUE( haggle.initialize( &data._catalog, hashed_string( "griffin" ) ) );
    SW_ASSERT_TRUE( haggle.propose( 300 ) == WitcherHaggleResult::Countered );

    const vector<uint8> haggleBytes = captureWitcherBytes( haggle );
    WitcherHaggle       restoredHaggle;
    SW_ASSERT_TRUE( restoredHaggle.initialize( &data._catalog, hashed_string( "griffin" ) ) );
    Archive haggleReader( haggleBytes.data(), haggleBytes.size() );
    SW_ASSERT_TRUE( restoredHaggle.readState( haggleReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, haggleReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureWitcherBytes( restoredHaggle ) == haggleBytes );
    SW_EXPECT_EQUAL( 250, restoredHaggle.getOffer() );
    SW_EXPECT_TRUE( haggle.propose( 320 ) == restoredHaggle.propose( 320 ) );
    SW_EXPECT_TRUE( captureWitcherBytes( haggle ) == captureWitcherBytes( restoredHaggle ) );

    WitcherHaggle truncatedHaggle;
    SW_ASSERT_TRUE( truncatedHaggle.initialize( &data._catalog, hashed_string( "griffin" ) ) );
    Archive haggleCut( haggleBytes.data(), haggleBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedHaggle.readState( haggleCut ) );
    SW_EXPECT_EQUAL( 200, truncatedHaggle.getOffer() );

    // 변이 — 첫 묶음에 스킬 둘 · 변이원 하나.
    WitcherMutagens mutagens;
    mutagens.initialize( &data._catalog, 5 );
    SW_ASSERT_TRUE( mutagens.equipSkill( 0, 0, hashed_string( "muscle_memory" ) ) == WitcherSlotResult::Ok );
    SW_ASSERT_TRUE( mutagens.equipSkill( 0, 1, hashed_string( "firestream" ) ) == WitcherSlotResult::Ok );
    SW_ASSERT_TRUE( mutagens.equipMutagen( 0, hashed_string( "red_greater" ) ) == WitcherSlotResult::Ok );

    const vector<uint8> mutagenBytes = captureWitcherBytes( mutagens );
    WitcherMutagens     restoredMutagens;
    restoredMutagens.initialize( &data._catalog, 1 );
    Archive mutagenReader( mutagenBytes.data(), mutagenBytes.size() );
    SW_ASSERT_TRUE( restoredMutagens.readState( mutagenReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, mutagenReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureWitcherBytes( restoredMutagens ) == mutagenBytes );
    SW_EXPECT_TRUE( restoredMutagens.getMutagen( 0 ) == hashed_string( "red_greater" ) );
    SW_EXPECT_TRUE( mutagens.equipSkill( 0, 2, hashed_string( "strength_training" ) ) == restoredMutagens.equipSkill( 0, 2, hashed_string( "strength_training" ) ) );
    SW_EXPECT_TRUE( captureWitcherBytes( mutagens ) == captureWitcherBytes( restoredMutagens ) );

    WitcherMutagens truncatedMutagens;
    truncatedMutagens.initialize( &data._catalog, 5 );
    Archive mutagenCut( mutagenBytes.data(), mutagenBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedMutagens.readState( mutagenCut ) );
    SW_EXPECT_TRUE( truncatedMutagens.getSkill( 0, 0 ).empty() );
}
