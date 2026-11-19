#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureLifeCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureTown.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

// CreatureTownRulesTest — 생물 마을의 플레이 규칙: 능력은 그 생물이 아는 것만, 규칙에 맞는 셀에만, 하루 횟수만큼 쓰이고 날이 넘어가면 돌아온다.
// 선물은 가방에서 하나를 빼고 하루 한 번이다. 공유 상태(평판 · 일지 · 시계)는 빌리지 않는다 — 날은 0 이고 호감도는 움직이지 않는다. 키트 GF_CreatureLife(nogpu).

namespace
{
    constexpr int32       kRulesTownSize   = 4;
    constexpr const utf8* kRulesCatalogXML = R"(<CreatureLifeCatalog>
  <Rules houseObject="house" talkPoints="5" giftPoints="10" likedGiftPoints="30" foodPoints="20" requestPoints="50" randomSeed="7100" blockingObjects="house,tree,rock"/>
  <Habitat id="grove" name="Grove" capacity="1">
    <Key symbol="T" object="tree"/>
    <Row cells="T"/>
  </Habitat>
  <Species id="sprout" name="Sprout" habitats="grove" chance="1" abilities="plant"/>
  <Ability id="plant" uses="2"><Rule from="empty" to="tree"/></Ability>
  <Appeal diversity="10" creature="5" friendship="0.1"><Tier name="Camp" min="0"/></Appeal>
</CreatureLifeCatalog>)";

    constexpr const utf8* kRulesItemXML = R"(<ItemCatalog>
  <Item id="flower"/>
  <Item id="berry"/>
</ItemCatalog>)";

    const sw::hashed_string kSprout{ "sprout" };
    const sw::hashed_string kPlant{ "plant" };
    const sw::hashed_string kGiftItem{ "flower" };

    /** @brief 나무 한 그루의 숲에 새싹 하나가 찾아온 마을입니다. */
    struct CreatureTownRulesTest
    {
        sw::CreatureLifeCatalog _catalog{};
        sw::CreatureTown        _town{};

        CreatureTownRulesTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kRulesCatalogXML, "CreatureTownRulesTest" ) );
            SW_REQUIRE_TRUE( _town.initialize( &_catalog, sw::SessionStateRefs{}, kRulesTownSize, kRulesTownSize ) );
        }

        void attractSprout()
        {
            SW_REQUIRE_TRUE( _town.setObject( 0, 0, "tree" ) );
            SW_REQUIRE_EQUAL( 1, _town.attractVisitorsAt( 0, 8, sw::DayPhase::Day, "sunny" ) );
            SW_REQUIRE_NOT_NULL( _town.findCreature( kSprout ) );
        }
    };
} // namespace

/** @brief [CreatureTownRulesTest] 능력은 마을에 사는 생물이 아는 것만, 규칙에 맞는 마을 안 셀에만 쓰인다. 거절은 횟수를 쓰지 않는다. */
SW_TEST_CASE_F( CreatureTownRulesTest, AbilityNeedsACreatureAKnownAbilityAndAMatchingCell )
{
    SW_EXPECT_TRUE( _town.useAbility( kSprout, kPlant, 1, 1, nullptr ) == sw::CreatureAbilityResult::UnknownCreature ); // 아직 오지 않았다
    SW_EXPECT_EQUAL( 0, _town.countAbilityUsesLeft( kSprout, kPlant ) );                                                // 모르면 0
    attractSprout();
    SW_EXPECT_EQUAL( 2, _town.countAbilityUsesLeft( kSprout, kPlant ) );

    SW_EXPECT_TRUE( _town.useAbility( kSprout, "dig", 1, 1, nullptr ) == sw::CreatureAbilityResult::NotKnown );
    SW_EXPECT_TRUE( _town.useAbility( kSprout, kPlant, kRulesTownSize, 0, nullptr ) == sw::CreatureAbilityResult::OutOfBounds );
    SW_EXPECT_TRUE( _town.useAbility( kSprout, kPlant, 0, 0, nullptr ) == sw::CreatureAbilityResult::NoRule ); // 나무 위에는 심지 못한다
    SW_EXPECT_EQUAL( 2, _town.countAbilityUsesLeft( kSprout, kPlant ) );
    SW_EXPECT_EQUAL( 0, _town.countAbilityUsesLeft( kSprout, "dig" ) );
}

/** @brief [CreatureTownRulesTest] 능력은 셀을 바꾸고 하루 횟수를 쓴다. 다 쓰면 거절하고, 날이 넘어가면 횟수가 돌아온다. */
SW_TEST_CASE_F( CreatureTownRulesTest, AbilityChangesTheCellAndSpendsDailyUses )
{
    attractSprout();
    sw::vector<sw::CreatureTownEvent> listEvent;
    _town.pollEvents( listEvent ); // 서식지 · 방문 이벤트는 버린다
    listEvent.clear();

    SW_REQUIRE_TRUE( _town.useAbility( kSprout, kPlant, 1, 1, nullptr ) == sw::CreatureAbilityResult::Ok );
    const sw::hashed_string* pPlanted = _town.findObject( 1, 1 );
    SW_REQUIRE_NOT_NULL( pPlanted );
    SW_EXPECT_TRUE( *pPlanted == sw::hashed_string( "tree" ) );
    SW_EXPECT_EQUAL( 2, _town.countHabitats( "grove" ) ); // 심은 나무도 숲이 된다
    SW_EXPECT_EQUAL( 1, _town.countAbilityUsesLeft( kSprout, kPlant ) );

    SW_REQUIRE_TRUE( _town.useAbility( kSprout, kPlant, 2, 2, nullptr ) == sw::CreatureAbilityResult::Ok );
    SW_EXPECT_EQUAL( 0, _town.countAbilityUsesLeft( kSprout, kPlant ) );
    SW_EXPECT_TRUE( _town.useAbility( kSprout, kPlant, 3, 3, nullptr ) == sw::CreatureAbilityResult::NoUsesLeft );
    const sw::hashed_string* pUntouched = _town.findObject( 3, 3 );
    SW_REQUIRE_NOT_NULL( pUntouched );
    SW_EXPECT_TRUE( pUntouched->empty() );

    _town.pollEvents( listEvent );
    int32 abilityEventCount = 0;
    for ( const sw::CreatureTownEvent& event : listEvent )
    {
        abilityEventCount += event._kind == sw::CreatureTownEvent::Kind::AbilityUsed ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 2, abilityEventCount ); // 성공한 두 번만 알린다

    _town.advanceDay();
    SW_EXPECT_EQUAL( 2, _town.countAbilityUsesLeft( kSprout, kPlant ) );
    SW_EXPECT_TRUE( _town.useAbility( kSprout, kPlant, 3, 3, nullptr ) == sw::CreatureAbilityResult::Ok );
}

/** @brief [CreatureTownRulesTest] 선물은 가방에서 하나를 빼고 하루 한 번이다. 없는 생물 · 없는 아이템은 거절하고 가방을 건드리지 않는다. */
SW_TEST_CASE_F( CreatureTownRulesTest, GiftTakesOneItemOncePerDay )
{
    sw::ItemCatalog items;
    SW_REQUIRE_TRUE( items.loadFromXMLText( kRulesItemXML, "CreatureTownRulesTest" ) );
    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &items, 2 ) );
    SW_REQUIRE_EQUAL( 2, inventory.addItem( kGiftItem, 2 ) );

    SW_EXPECT_TRUE( _town.giveGift( kSprout, kGiftItem, inventory ) == sw::CreatureInteractResult::UnknownCreature ); // 아직 오지 않았다
    attractSprout();
    SW_EXPECT_TRUE( _town.giveGift( kSprout, "berry", inventory ) == sw::CreatureInteractResult::MissingItem );
    SW_EXPECT_EQUAL( 2, inventory.getItemCount( kGiftItem ) );

    SW_REQUIRE_TRUE( _town.giveGift( kSprout, kGiftItem, inventory ) == sw::CreatureInteractResult::Ok );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( kGiftItem ) );
    SW_EXPECT_TRUE( _town.giveGift( kSprout, kGiftItem, inventory ) == sw::CreatureInteractResult::AlreadyToday );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( kGiftItem ) ); // 거절된 선물은 빼지 않는다

    // 빌린 평판이 없으면 호감도 단계도 없다(값을 지어내지 않는다)
    SW_EXPECT_TRUE( _town.getFriendshipTier( kSprout ).empty() );
    SW_EXPECT_EQUAL( 0, _town.getFriendship( kSprout ) );
}
