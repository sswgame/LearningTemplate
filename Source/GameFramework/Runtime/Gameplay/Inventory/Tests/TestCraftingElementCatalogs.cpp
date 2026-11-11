// CraftingElementCatalogsTest — 제작 레시피(RecipeDef · RecipeItemParams)와 속성 상성표(ElementRuleParams · ElementStatusChance)의 칸 읽기.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/Combat/Damage/ElementChart.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"

using namespace sw;

/** @brief [CraftingElementCatalogsTest] 레시피 — 칸 이름(`level` · `known`), 물건 줄의 `count` 기본 1, 0 개 줄은 그 레시피를 넣지 않는다 */
SW_TEST_CASE( CraftingElementCatalogsTest, RecipeReadsPropertiesAndItemLines )
{
    RecipeCatalog catalog;
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_EXPECT_LOG( "Crafting.RecipeInvalidFieldsSkipped" );
    (void)catalog.loadFromXMLText( R"(<RecipeCatalog>
        <Recipe id="Sword" station="Anvil" time="3" level="2" known="false"><In item="Iron" count="2"/><Tool item="Hammer"/><Out item="Sword"/></Recipe>
        <Recipe id="Bad"><In item="Iron" count="0"/></Recipe>
    </RecipeCatalog>)",
                                   "recipes" );
    const RecipeDef* pSword = catalog.find( hashed_string( "Sword" ) );
    SW_REQUIRE_NOT_NULL( pSword );
    SW_EXPECT_EQUAL( 2, pSword->_requiredLevel );
    SW_EXPECT_FALSE( pSword->_bStartsKnown );
    SW_EXPECT_TRUE( pSword->_inputs.hasItem( hashed_string( "Iron" ) ) );
    SW_EXPECT_TRUE( pSword->_tools.hasItem( hashed_string( "Hammer" ) ) );
    SW_EXPECT_TRUE( catalog.find( hashed_string( "Bad" ) ) == nullptr );
}

/** @brief [CraftingElementCatalogsTest] 상성표 — 규칙 배율과 상태 확률을 읽고, 확률 1 초과는 넣지 않는다 */
SW_TEST_CASE( CraftingElementCatalogsTest, ElementChartReadsRulesAndStatus )
{
    ElementChart chart;
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_EXPECT_LOG( "ElementChart.StatusInvalidSkipped" );
    (void)chart.loadFromXMLText( R"(<ElementChart>
        <Element id="Fire"/><Element id="Ice"/>
        <Rule attack="Fire" defend="Ice" multiplier="2"/>
        <Status element="Fire" status="Burn" chance="0.25"/>
        <Status element="Ice" status="Freeze" chance="1.5"/>
    </ElementChart>)",
                                 "elements" );
    SW_EXPECT_NEAR( 2.0f, chart.getMultiplier( hashed_string( "Fire" ), hashed_string( "Ice" ) ), 1e-6f );
    SW_REQUIRE_EQUAL( size_t( 1 ), chart.getStatusChances().size() );
    SW_EXPECT_NEAR( 0.25f, chart.getStatusChances()[0]._chance, 1e-6f );
}
