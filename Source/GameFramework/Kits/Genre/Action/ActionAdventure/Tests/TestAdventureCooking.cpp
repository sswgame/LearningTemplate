#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureCooking.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

// AdventureCookingTest — 냄비 규칙: 빈 냄비 · 재료 수 상한 · 모르는 재료, 회복 배율, 효과 합산과 단계 경계, 지속 상한, 다른 효과가 섞인 수상한 요리, 레시피 고르기와 재료 되돌림.
// 결함은 찾지 못했다(주석 하나만 고쳤다). 키트 GF_ActionAdventure(nogpu).

namespace
{
    constexpr const utf8* kCookXML = R"(<AdventureCooking maxIngredients="3" heartScale="2" durationPerIngredient="30">
  <Effect id="Chilly" tier2="2" tier3="3" maxDuration="200"/>
  <Effect id="Spicy"/>
  <Ingredient id="melon" effect="Chilly" potency="1" duration="50" hearts="1"/>
  <Ingredient id="pepper" effect="Spicy" potency="1" duration="50" hearts="1"/>
  <Ingredient id="apple" hearts="2"/>
</AdventureCooking>)";

    constexpr const utf8* kCookItemXML = R"(<ItemCatalog>
  <Item id="melon" maxStack="5"/>
  <Item id="pepper" maxStack="5"/>
  <Item id="apple" maxStack="5"/>
  <Item id="simmeredDish" maxStack="5"/>
  <Item id="dubiousFood" maxStack="5"/>
  <Item id="frostedMelon" maxStack="5"/>
  <Item id="pie" maxStack="5"/>
  <Item id="fusion" maxStack="5"/>
  <Item id="ingot" maxStack="5"/>
</ItemCatalog>)";

    constexpr const utf8* kCookRecipeXML = R"(<RecipeCatalog>
  <Recipe id="frosted" station="CookingPot"><In item="melon" count="2"/><Out item="frostedMelon"/></Recipe>
  <Recipe id="secretPie" station="CookingPot" known="false"><In item="apple"/><Out item="pie"/></Recipe>
  <Recipe id="mixed" station="CookingPot"><In item="melon"/><In item="pepper"/><Out item="fusion"/></Recipe>
  <Recipe id="forge" station="Anvil"><In item="apple" count="2"/><Out item="ingot"/></Recipe>
</RecipeCatalog>)";

    const sw::hashed_string kMelon{ "melon" };
    const sw::hashed_string kPepper{ "pepper" };
    const sw::hashed_string kApple{ "apple" };
    const sw::hashed_string kChilly{ "Chilly" };
    const sw::hashed_string kSimmered{ "simmeredDish" };
    const sw::hashed_string kDubious{ "dubiousFood" };
    const sw::hashed_string kFrosted{ "frostedMelon" };
    const sw::hashed_string kPie{ "pie" };
    const sw::hashed_string kFusion{ "fusion" };

    constexpr float32 kCookTolerance = 1.0e-3f;

    /** @brief 카탈로그 셋과 인벤토리를 묶어 둔 냄비입니다. */
    struct CookingBench
    {
        sw::AdventureCooking _cooking;
        sw::ItemCatalog      _items;
        sw::RecipeCatalog    _recipes;
        sw::Crafter          _crafter;
        sw::Inventory        _inventory;

        [[nodiscard]] bool setUp( int32 slotCount )
        {
            const bool bLoaded = _cooking.loadFromXMLText( kCookXML, "cooking" ) && _items.loadFromXMLText( kCookItemXML, "items" ) && _recipes.loadFromXMLText( kCookRecipeXML, "recipes" );
            if ( bLoaded == false )
                return false;
            _crafter.initialize( &_recipes );
            return _inventory.initialize( &_items, slotCount );
        }

        sw::AdventureCookResult cook( const sw::vector<sw::hashed_string>& listIngredient, sw::AdventureDish& outDish, int32 level = 0 )
        {
            return _cooking.cook( listIngredient, _inventory, _crafter, _recipes, level, outDish );
        }
    };
} // namespace

/**
 * @brief [AdventureCookingTest] 빈 냄비, 재료 수 상한(같은 재료도 하나씩 센다), 모르는 재료를 이유로 거절하고 결과를 비워 둔다. 상한 개수는 된다
 */
SW_TEST_CASE( AdventureCookingTest, EvaluateRejectsBadPots )
{
    sw::AdventureCooking cooking;
    SW_REQUIRE_TRUE( cooking.loadFromXMLText( kCookXML, "cooking" ) );
    SW_EXPECT_EQUAL( 3, cooking.getMaxIngredientCount() );

    sw::AdventureDish dish;
    dish._heartQuarters = 99;
    SW_EXPECT_TRUE( cooking.evaluate( {}, dish ) == sw::AdventureCookResult::EmptyPot );
    SW_EXPECT_EQUAL( 0, dish._heartQuarters );
    SW_EXPECT_TRUE( cooking.evaluate( { kApple, kApple, kApple, kApple }, dish ) == sw::AdventureCookResult::TooManyIngredients );
    SW_EXPECT_TRUE( cooking.evaluate( { kApple, sw::hashed_string( "rock" ) }, dish ) == sw::AdventureCookResult::UnknownIngredient );
    SW_EXPECT_TRUE( cooking.evaluate( { kApple, kApple, kApple }, dish ) == sw::AdventureCookResult::Ok );
}

/**
 * @brief [AdventureCookingTest] 효과 없는 재료는 일반 요리이고 회복은 합에 배율을 곱한다. 효과 없는 재료는 효과 요리에 섞여도 회복만 보태고 재료 수만큼 시간을 늘린다(넣는 순서는 상관없다)
 */
SW_TEST_CASE( AdventureCookingTest, NeutralIngredientsMixWithAnyEffect )
{
    sw::AdventureCooking cooking;
    SW_REQUIRE_TRUE( cooking.loadFromXMLText( kCookXML, "cooking" ) );
    sw::AdventureDish dish;
    SW_REQUIRE_TRUE( cooking.evaluate( { kApple }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( kSimmered, dish._itemID );
    SW_EXPECT_EQUAL( 4, dish._heartQuarters );
    SW_EXPECT_EQUAL( 0, dish._effectTier );
    SW_EXPECT_TRUE( dish._effect.empty() );
    SW_EXPECT_NEAR( 0.0f, dish._duration, kCookTolerance );

    const sw::vector<sw::hashed_string> arrOrder[] = {
        {kMelon, kApple},
        {kApple, kMelon}
    };
    for ( const sw::vector<sw::hashed_string>& listIngredient : arrOrder )
    {
        SW_REQUIRE_TRUE( cooking.evaluate( listIngredient, dish ) == sw::AdventureCookResult::Ok );
        SW_EXPECT_EQUAL( kChilly, dish._effect );
        SW_EXPECT_EQUAL( 1, dish._effectTier );
        SW_EXPECT_EQUAL( 6, dish._heartQuarters );
        SW_EXPECT_NEAR( 110.0f, dish._duration, kCookTolerance ); // 50 + 30 × 2
        SW_EXPECT_TRUE( dish._bDubious == SW_FALSE );
    }
}

/**
 * @brief [AdventureCookingTest] 효과 세기 합이 정확히 기준이면 그 단계이고, 지속 시간은 효과의 상한에서 멈춘다
 */
SW_TEST_CASE( AdventureCookingTest, TierBoundariesAreInclusiveAndDurationIsCapped )
{
    sw::AdventureCooking cooking;
    SW_REQUIRE_TRUE( cooking.loadFromXMLText( kCookXML, "cooking" ) );
    sw::AdventureDish dish;
    SW_REQUIRE_TRUE( cooking.evaluate( { kMelon }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( 1, dish._effectTier );
    SW_REQUIRE_TRUE( cooking.evaluate( { kMelon, kMelon }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( 2, dish._effectTier );
    SW_EXPECT_NEAR( 2.0f, dish._potency, kCookTolerance );
    SW_EXPECT_NEAR( 160.0f, dish._duration, kCookTolerance );
    SW_REQUIRE_TRUE( cooking.evaluate( { kMelon, kMelon, kMelon }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( 3, dish._effectTier );
    SW_EXPECT_NEAR( 200.0f, dish._duration, kCookTolerance ); // 240 은 상한 200 에서 잘린다
}

/**
 * @brief [AdventureCookingTest] 서로 다른 효과가 섞이면 수상한 요리로 효과도 회복 배율도 사라지고 정해진 회복만 남는다
 */
SW_TEST_CASE( AdventureCookingTest, MixedEffectsMakeDubiousFood )
{
    sw::AdventureCooking cooking;
    SW_REQUIRE_TRUE( cooking.loadFromXMLText( kCookXML, "cooking" ) );
    sw::AdventureDish dish;
    SW_REQUIRE_TRUE( cooking.evaluate( { kMelon, kApple, kPepper }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( kDubious, dish._itemID );
    SW_EXPECT_TRUE( dish._bDubious == SW_TRUE );
    SW_EXPECT_EQUAL( 4, dish._heartQuarters );
    SW_EXPECT_EQUAL( 0, dish._effectTier );
    SW_EXPECT_TRUE( dish._effect.empty() );

    // 섞인 뒤에 나온 모르는 재료도 거절한다(앞에서 답을 내지 않는다).
    SW_EXPECT_TRUE( cooking.evaluate( { kMelon, kPepper, sw::hashed_string( "rock" ) }, dish ) == sw::AdventureCookResult::UnknownIngredient );
}

/**
 * @brief [AdventureCookingTest] 앞에 적지 않은 효과를 든 재료는 넣지 않고 오류를 남기며, 재료가 하나도 없으면 읽기 실패다
 */
SW_TEST_CASE( AdventureCookingTest, IngredientWithUndeclaredEffectIsSkipped )
{
    SW_EXPECT_LOG( "AdventureCooking.IngredientUndeclaredEffectNot" );
    sw::AdventureCooking cooking;
    SW_REQUIRE_TRUE( cooking.loadFromXMLText( R"(<AdventureCooking>
  <Ingredient id="bad" effect="Ghost"/>
  <Ingredient id="good"/>
</AdventureCooking>)",
                                              "undeclared" ) );
    SW_EXPECT_TRUE( cooking.findIngredient( sw::hashed_string( "bad" ) ) == nullptr );
    SW_EXPECT_TRUE( cooking.findIngredient( sw::hashed_string( "good" ) ) != nullptr );

    sw::AdventureCooking empty;
    SW_EXPECT_FALSE( empty.loadFromXMLText( R"(<AdventureCooking><Effect id="Chilly"/></AdventureCooking>)", "empty" ) );
}

/**
 * @brief [AdventureCookingTest] 재료가 똑같은 냄비 레시피(종류와 개수)면 그 요리를 만들고, 효과와 회복은 냄비 규칙 그대로다
 */
SW_TEST_CASE( AdventureCookingTest, CookUsesAnExactRecipe )
{
    CookingBench bench;
    SW_REQUIRE_TRUE( bench.setUp( 4 ) );
    SW_REQUIRE_EQUAL( 3, bench._inventory.addItem( kMelon, 3 ) );

    sw::AdventureDish dish;
    SW_EXPECT_TRUE( bench.cook( { kMelon, kMelon }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( kFrosted, dish._itemID );
    SW_EXPECT_TRUE( dish._bNamedRecipe == SW_TRUE );
    SW_EXPECT_EQUAL( 2, dish._effectTier );
    SW_EXPECT_EQUAL( 1, bench._inventory.getItemCount( kFrosted ) );
    SW_EXPECT_EQUAL( 1, bench._inventory.getItemCount( kMelon ) );

    // 개수가 다르면 그 레시피가 아니다.
    SW_EXPECT_TRUE( bench.cook( { kMelon }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( kSimmered, dish._itemID );
    SW_EXPECT_TRUE( dish._bNamedRecipe == SW_FALSE );
    SW_EXPECT_EQUAL( 0, bench._inventory.getItemCount( kMelon ) );
}

/**
 * @brief [AdventureCookingTest] 배우지 않은 레시피 · 다른 작업대 레시피는 일반 요리로 넘어가고, 배우면 이름 있는 요리가 된다. 수상한 요리에는 레시피가 있어도 이름이 없다
 */
SW_TEST_CASE( AdventureCookingTest, CookFallsToGenericWithoutAUsableRecipe )
{
    CookingBench bench;
    SW_REQUIRE_TRUE( bench.setUp( 6 ) );
    SW_REQUIRE_EQUAL( 5, bench._inventory.addItem( kApple, 5 ) );
    SW_REQUIRE_EQUAL( 1, bench._inventory.addItem( kMelon, 1 ) );
    SW_REQUIRE_EQUAL( 1, bench._inventory.addItem( kPepper, 1 ) );

    sw::AdventureDish dish;
    SW_EXPECT_TRUE( bench.cook( { kApple }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( kSimmered, dish._itemID );
    SW_EXPECT_EQUAL( 4, bench._inventory.getItemCount( kApple ) );

    SW_EXPECT_TRUE( bench.cook( { kApple, kApple }, dish ) == sw::AdventureCookResult::Ok ); // 작업대가 Anvil 인 레시피는 보지 않는다
    SW_EXPECT_EQUAL( kSimmered, dish._itemID );
    SW_EXPECT_EQUAL( 2, bench._inventory.getItemCount( kApple ) );

    bench._crafter.learnRecipe( sw::hashed_string( "secretPie" ) );
    SW_EXPECT_TRUE( bench.cook( { kApple }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( kPie, dish._itemID );
    SW_EXPECT_TRUE( dish._bNamedRecipe == SW_TRUE );

    SW_EXPECT_TRUE( bench.cook( { kMelon, kPepper }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( kDubious, dish._itemID );
    SW_EXPECT_TRUE( dish._bNamedRecipe == SW_FALSE );
    SW_EXPECT_EQUAL( 0, bench._inventory.getItemCount( kFusion ) );
    SW_EXPECT_EQUAL( 1, bench._inventory.getItemCount( kDubious ) );
}

/**
 * @brief [AdventureCookingTest] 재료가 모자라면 아무것도 쓰지 않고, 결과 넣을 자리가 없으면 일반 요리든 레시피든 재료를 그대로 되돌린다. 재료 자리가 비면 그 자리에 들어간다
 */
SW_TEST_CASE( AdventureCookingTest, CookKeepsIngredientsWhenItFails )
{
    CookingBench missing;
    SW_REQUIRE_TRUE( missing.setUp( 4 ) );
    SW_REQUIRE_EQUAL( 1, missing._inventory.addItem( kMelon, 1 ) );
    sw::AdventureDish dish;
    SW_EXPECT_TRUE( missing.cook( { kMelon, kMelon }, dish ) == sw::AdventureCookResult::MissingIngredients );
    SW_EXPECT_EQUAL( 1, missing._inventory.getItemCount( kMelon ) );
    SW_EXPECT_TRUE( missing.cook( {}, dish ) == sw::AdventureCookResult::EmptyPot );

    CookingBench genericFull;
    SW_REQUIRE_TRUE( genericFull.setUp( 1 ) );
    SW_REQUIRE_EQUAL( 2, genericFull._inventory.addItem( kApple, 2 ) ); // 하나를 써도 슬롯이 비지 않는다
    SW_EXPECT_TRUE( genericFull.cook( { kApple }, dish ) == sw::AdventureCookResult::NoRoom );
    SW_EXPECT_EQUAL( 2, genericFull._inventory.getItemCount( kApple ) );
    SW_EXPECT_EQUAL( 0, genericFull._inventory.getItemCount( kSimmered ) );

    CookingBench recipeFull;
    SW_REQUIRE_TRUE( recipeFull.setUp( 1 ) );
    SW_REQUIRE_EQUAL( 3, recipeFull._inventory.addItem( kMelon, 3 ) );
    SW_EXPECT_TRUE( recipeFull.cook( { kMelon, kMelon }, dish ) == sw::AdventureCookResult::NoRoom );
    SW_EXPECT_EQUAL( 3, recipeFull._inventory.getItemCount( kMelon ) );
    SW_EXPECT_EQUAL( 0, recipeFull._inventory.getItemCount( kFrosted ) );

    CookingBench lastItem;
    SW_REQUIRE_TRUE( lastItem.setUp( 1 ) );
    SW_REQUIRE_EQUAL( 1, lastItem._inventory.addItem( kApple, 1 ) );
    SW_EXPECT_TRUE( lastItem.cook( { kApple }, dish ) == sw::AdventureCookResult::Ok );
    SW_EXPECT_EQUAL( 1, lastItem._inventory.getItemCount( kSimmered ) );
}
