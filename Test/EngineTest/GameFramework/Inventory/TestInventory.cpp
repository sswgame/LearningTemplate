#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Inventory/Crafting.h"
#include "GameFramework/Base/Inventory/Equipment.h"
#include "GameFramework/Base/Inventory/GridInventory.h"
#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/ItemBag.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Inventory/LootTable.h"
#include "GameFramework/Base/Utility/GameRandom.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 아이템 — 카탈로그, 칸 인벤토리(겹치기 · 무게 · 옮기기 · 나누기 · 정렬 · 내구도 · 가방 바꾸기), 장비 칸과 능력치 합, 전리품 표, 제작과 제작 대기열.

using namespace sw;

namespace
{
    constexpr const utf8* kItemTestXml = R"(
<ItemCatalog>
  <Item id="herb" category="Material" maxStack="10" weight="0.1" value="2"/>
  <Item id="ore" category="Material" maxStack="5" weight="2" value="5" rarity="1"/>
  <Item id="potion" category="Consumable" maxStack="5" useEffect="Heal"/>
  <Item id="sword" category="Weapon" slot="MainHand" weight="3" durability="10" maxStack="4" tags="Metal,Blade"><Stats attack="12" speed="-0.1"/></Item>
  <Item id="axe" category="Weapon" slot="MainHand" weight="4"><Stats attack="16"/></Item>
  <Item id="ring" category="Accessory" slot="Ring" rarity="3"><Stats luck="2" attack="1"/></Item>
  <Item id="mortar" category="Tool"/>
</ItemCatalog>
)";

    constexpr const utf8* kLootTestXml = R"(
<LootCatalog>
  <Table id="wolf" rolls="1" rollsMax="2" none="2">
    <Entry item="herb" weight="6" min="1" max="3"/>
    <Entry table="gems" weight="2"/>
    <Always item="ore" chance="0.5"/>
  </Table>
  <Table id="gems"><Entry item="ring" weight="1"/></Table>
  <Table id="loop"><Entry table="loop" weight="1"/></Table>
</LootCatalog>
)";

    constexpr const utf8* kRecipeTestXml = R"(
<RecipeCatalog>
  <Recipe id="brew" station="Alchemy" time="2"><In item="herb" count="3"/><Tool item="mortar"/><Out item="potion" count="1"/></Recipe>
  <Recipe id="forge" level="5" known="false"><In item="ore" count="2"/><Out item="sword"/></Recipe>
</RecipeCatalog>
)";
} // namespace

SW_TEST_CASE( InventoryTest, SlotsStackRespectWeightAndMoveSplitSortAndWear )
{
    ItemCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kItemTestXml, "InventoryTest" ) );
    const ItemDef* pSword = catalog.findItem( hashed_string( "sword" ) );
    SW_ASSERT_NOT_NULL( pSword );
    SW_EXPECT_EQUAL( 1, pSword->_maxStack ); // 내구도가 있으면 겹치지 않는다
    SW_EXPECT_TRUE( pSword->hasTag( hashed_string( "Blade" ) ) );
    SW_EXPECT_NEAR_EQUAL( 12.0f, pSword->_stats.getValue( hashed_string( "attack" ) ), 1.0e-4f );
    SW_EXPECT_TRUE( pSword->isEquipment() && catalog.findItem( hashed_string( "herb" ) )->isEquipment() == false );

    Inventory inventory;
    inventory.initialize( &catalog, 4, 20.0f );
    SW_EXPECT_EQUAL( 25, inventory.addItem( hashed_string( "herb" ), 25 ) ); // 10 + 10 + 5 — 세 칸
    SW_EXPECT_EQUAL( 1, inventory.countEmptySlots() );
    SW_EXPECT_EQUAL( 15, inventory.addItem( hashed_string( "herb" ), 20 ) ); // 셋째 칸의 나머지 5 + 빈 칸 10
    SW_EXPECT_TRUE( inventory.hasRoomFor( hashed_string( "herb" ), 1 ) == false );
    SW_EXPECT_EQUAL( 40, inventory.getItemCount( hashed_string( "herb" ) ) );
    SW_EXPECT_TRUE( inventory.removeItem( hashed_string( "herb" ), 41 ) == false ); // 모자라면 빼지 않는다
    SW_EXPECT_TRUE( inventory.removeItem( hashed_string( "herb" ), 12 ) );
    SW_EXPECT_EQUAL( 28, inventory.getItemCount( hashed_string( "herb" ) ) );

    // 무게 — 광석 2 kg, 한도 20 kg 에서 약초 1.8 kg 을 빼면 9 개까지.
    inventory.clear();
    SW_EXPECT_EQUAL( 10, inventory.addItem( hashed_string( "ore" ), 99 ) ); // 칸 둘(5+5) · 20 kg
    SW_EXPECT_NEAR_EQUAL( 20.0f, inventory.computeWeight(), 1.0e-3f );
    SW_EXPECT_EQUAL( 0, inventory.addItem( hashed_string( "sword" ), 1 ) );
    SW_EXPECT_TRUE( inventory.removeItem( hashed_string( "ore" ), 4 ) );

    // 칸 옮기기 · 나누기 · 정렬.
    SW_EXPECT_EQUAL( 1, inventory.addItem( hashed_string( "sword" ), 1 ) );
    const int32 splitSlot = inventory.splitSlot( inventory.findFirstSlot( hashed_string( "ore" ) ), 2 );
    SW_EXPECT_TRUE( splitSlot >= 0 );
    SW_EXPECT_EQUAL( 6, inventory.getItemCount( hashed_string( "ore" ) ) );
    SW_EXPECT_EQUAL( 0, inventory.countEmptySlots() );
    const int32 firstOre = inventory.findFirstSlot( hashed_string( "ore" ) );
    SW_EXPECT_TRUE( inventory.moveSlot( splitSlot, firstOre ) ); // 같은 아이템 — 합친다(넘치면 남긴다)
    SW_EXPECT_EQUAL( 6, inventory.getItemCount( hashed_string( "ore" ) ) );
    inventory.sortSlots();
    SW_EXPECT_TRUE( inventory.getSlot( 0 )._itemId == hashed_string( "ore" ) && inventory.getSlot( 0 )._count == 5 ); // Material 먼저
    SW_EXPECT_TRUE( inventory.getSlot( 2 )._itemId == hashed_string( "sword" ) );
    SW_EXPECT_EQUAL( 1, inventory.countEmptySlots() );

    // 내구도 — 다 닳으면 칸이 빈다.
    const int32 swordSlot = inventory.findFirstSlot( hashed_string( "sword" ) );
    SW_EXPECT_NEAR_EQUAL( 10.0f, inventory.getSlot( swordSlot )._durability, 1.0e-4f );
    SW_EXPECT_FALSE( inventory.wearSlot( swordSlot, 4.0f ) );
    SW_EXPECT_TRUE( inventory.wearSlot( swordSlot, 6.0f ) );
    SW_EXPECT_EQUAL( 0, inventory.getItemCount( hashed_string( "sword" ) ) );

    // 가방이 작아지면 넘치는 것이 나온다.
    vector<InventorySlot> listOverflow;
    inventory.resize( 1, listOverflow );
    SW_EXPECT_EQUAL( 1, inventory.getSlotCount() );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listOverflow.size() ) );
    ItemBag bag;
    inventory.fillItemBag( bag );
    SW_EXPECT_EQUAL( 5, bag.getItemCount( hashed_string( "ore" ) ) );
}

SW_TEST_CASE( InventoryTest, EquipmentSlotsAcceptKindsSwapWithInventoryAndSumStats )
{
    ItemCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kItemTestXml, "InventoryTest" ) );
    Inventory inventory;
    inventory.initialize( &catalog, 3 );
    (void)inventory.addItem( hashed_string( "sword" ), 1 );
    (void)inventory.addItem( hashed_string( "axe" ), 1 );
    (void)inventory.addItem( hashed_string( "ring" ), 1 );

    Equipment equipment;
    equipment.initialize( &catalog, "Head, MainHand, Ring1:Ring, Ring2:Ring" );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( equipment.getSlots().size() ) );
    SW_EXPECT_TRUE( equipment.canEquip( hashed_string( "Ring2" ), hashed_string( "ring" ) ) );
    SW_EXPECT_FALSE( equipment.canEquip( hashed_string( "Head" ), hashed_string( "ring" ) ) );

    SW_EXPECT_TRUE( equipment.equipFromInventory( inventory, inventory.findFirstSlot( hashed_string( "sword" ) ) ) == EquipResult::Ok );
    SW_EXPECT_TRUE( equipment.equipFromInventory( inventory, inventory.findFirstSlot( hashed_string( "ring" ) ) ) == EquipResult::Ok );
    SW_EXPECT_TRUE( equipment.findEquipped( hashed_string( "Ring1" ) ) != nullptr ); // 빈 반지 칸 먼저
    SW_EXPECT_TRUE( equipment.equipFromInventory( inventory, inventory.findFirstSlot( hashed_string( "axe" ) ), hashed_string( "Head" ) ) == EquipResult::WrongSlot );

    StatBlock stats;
    equipment.computeStats( stats );
    SW_EXPECT_NEAR_EQUAL( 13.0f, stats.getValue( hashed_string( "attack" ) ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, stats.getValue( hashed_string( "luck" ) ), 1.0e-4f );

    // 도끼로 바꾸면 칼은 인벤토리로 돌아온다.
    SW_EXPECT_TRUE( equipment.equipFromInventory( inventory, inventory.findFirstSlot( hashed_string( "axe" ) ) ) == EquipResult::Ok );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( hashed_string( "sword" ) ) );
    SW_EXPECT_EQUAL( 0, inventory.getItemCount( hashed_string( "axe" ) ) );
    equipment.computeStats( stats );
    SW_EXPECT_NEAR_EQUAL( 17.0f, stats.getValue( hashed_string( "attack" ) ), 1.0e-4f );

    // 인벤토리가 꽉 차면 벗지 못한다.
    (void)inventory.addItem( hashed_string( "herb" ), 20 );
    SW_EXPECT_EQUAL( 0, inventory.countEmptySlots() );
    SW_EXPECT_FALSE( equipment.unequipToInventory( hashed_string( "MainHand" ), inventory ) );
    SW_EXPECT_TRUE( equipment.findEquipped( hashed_string( "MainHand" ) ) != nullptr );
}

SW_TEST_CASE( InventoryTest, LootTablesRollWeightsNestedTablesAndLuckDeterministically )
{
    LootCatalog loot;
    SW_ASSERT_TRUE( loot.loadFromXmlText( kLootTestXml, "InventoryTest" ) );
    GameRandom randomA( 77u );
    GameRandom randomB( 77u );
    ItemBag    bagA;
    ItemBag    bagB;
    for ( int32 index = 0; index < 200; ++index )
    {
        SW_EXPECT_TRUE( loot.roll( hashed_string( "wolf" ), randomA, bagA ) );
        (void)loot.roll( hashed_string( "wolf" ), randomB, bagB );
    }
    SW_EXPECT_EQUAL( bagA.getItemCount( hashed_string( "herb" ) ), bagB.getItemCount( hashed_string( "herb" ) ) ); // 씨앗이 같으면 같다
    SW_EXPECT_EQUAL( bagA.getItemCount( hashed_string( "ring" ) ), bagB.getItemCount( hashed_string( "ring" ) ) );
    // 200 번 × 평균 1.5 회 × 6/10 × 평균 2 개 ≈ 360 약초, 반지 ≈ 60, 광석 ≈ 100.
    const int32 herbCount = bagA.getItemCount( hashed_string( "herb" ) );
    const int32 ringCount = bagA.getItemCount( hashed_string( "ring" ) );
    const int32 oreCount  = bagA.getItemCount( hashed_string( "ore" ) );
    SW_EXPECT_TRUE( herbCount > 280 && herbCount < 440 );
    SW_EXPECT_TRUE( ringCount > 35 && ringCount < 90 );
    SW_EXPECT_TRUE( oreCount > 75 && oreCount < 125 );

    // 행운은 "없음" 을 줄이고 늘 주는 확률을 올린다.
    GameRandom randomLucky( 77u );
    ItemBag    bagLucky;
    for ( int32 index = 0; index < 200; ++index )
        (void)loot.roll( hashed_string( "wolf" ), randomLucky, bagLucky, 2.0f );
    SW_EXPECT_EQUAL( 200, bagLucky.getItemCount( hashed_string( "ore" ) ) );

    // 확률 표시 · 서로 부르는 표 · 없는 표.
    SW_EXPECT_NEAR_EQUAL( 0.5f, loot.computeDropChance( hashed_string( "gems" ), hashed_string( "ring" ) ) * 0.5f, 1.0e-4f );
    const float32 oreChance = loot.computeDropChance( hashed_string( "wolf" ), hashed_string( "ore" ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, oreChance, 1.0e-4f );
    ItemBag bagLoop;
    SW_EXPECT_TRUE( loot.roll( hashed_string( "loop" ), randomA, bagLoop ) );
    SW_EXPECT_TRUE( bagLoop.isEmpty() );
    SW_EXPECT_FALSE( loot.roll( hashed_string( "missing" ), randomA, bagLoop ) );
}

SW_TEST_CASE( InventoryTest, CraftingChecksStationLevelToolsAndQueuesTimedJobs )
{
    ItemCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kItemTestXml, "InventoryTest" ) );
    RecipeCatalog recipes;
    SW_ASSERT_TRUE( recipes.loadFromXmlText( kRecipeTestXml, "InventoryTest" ) );
    vector<const RecipeDef*> listRecipe;
    recipes.findRecipesFor( hashed_string( "potion" ), listRecipe );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listRecipe.size() ) );

    Inventory inventory;
    inventory.initialize( &catalog, 6 );
    (void)inventory.addItem( hashed_string( "herb" ), 10 );
    (void)inventory.addItem( hashed_string( "ore" ), 4 );
    Crafter crafter;
    crafter.initialize( &recipes );
    const hashed_string alchemy( "Alchemy" );

    SW_EXPECT_TRUE( crafter.evaluate( hashed_string( "brew" ), inventory, hashed_string( "Forge" ), 1 ) == CraftResult::WrongStation );
    SW_EXPECT_TRUE( crafter.evaluate( hashed_string( "brew" ), inventory, alchemy, 1 ) == CraftResult::MissingTools );
    (void)inventory.addItem( hashed_string( "mortar" ), 1 );
    SW_EXPECT_EQUAL( 3, crafter.computeMaxCraftCount( hashed_string( "brew" ), inventory, alchemy, 1 ) );
    SW_EXPECT_TRUE( crafter.craft( hashed_string( "brew" ), inventory, alchemy, 1 ) == CraftResult::Ok );
    SW_EXPECT_EQUAL( 7, inventory.getItemCount( hashed_string( "herb" ) ) );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( hashed_string( "potion" ) ) );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( hashed_string( "mortar" ) ) ); // 도구는 남는다

    // 배워야 하는 레시피 · 레벨.
    SW_EXPECT_TRUE( crafter.evaluate( hashed_string( "forge" ), inventory, hashed_string{}, 9 ) == CraftResult::NotLearned );
    crafter.learnRecipe( hashed_string( "forge" ) );
    SW_EXPECT_TRUE( crafter.evaluate( hashed_string( "forge" ), inventory, hashed_string{}, 3 ) == CraftResult::LevelTooLow );
    SW_EXPECT_TRUE( crafter.craft( hashed_string( "forge" ), inventory, hashed_string{}, 5, 3 ) == CraftResult::MissingInputs );

    // 대기열 — 재료는 넣을 때 거두고, 2 초마다 하나씩.
    SW_EXPECT_TRUE( crafter.enqueue( hashed_string( "brew" ), inventory, alchemy, 1, 2 ) == CraftResult::Ok );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( hashed_string( "herb" ) ) );
    vector<hashed_string> listFinished;
    crafter.update( 1.5f, inventory, listFinished );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( listFinished.size() ) );
    crafter.update( 1.0f, inventory, listFinished );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listFinished.size() ) );
    SW_EXPECT_EQUAL( 2, inventory.getItemCount( hashed_string( "potion" ) ) );
    // 남은 하나를 취소하면 재료(3)를 돌려받는다.
    SW_EXPECT_TRUE( crafter.cancel( 0, inventory ) );
    SW_EXPECT_EQUAL( 4, inventory.getItemCount( hashed_string( "herb" ) ) );
    SW_EXPECT_TRUE( crafter.getQueue().empty() );

    // 한 번에 시간이 많이 흐르면 이어서 만든다.
    SW_EXPECT_TRUE( crafter.enqueue( hashed_string( "forge" ), inventory, hashed_string{}, 5, 2 ) == CraftResult::Ok );
    crafter.update( 0.1f, inventory, listFinished );
    SW_EXPECT_EQUAL( 2, inventory.getItemCount( hashed_string( "sword" ) ) );
}

SW_TEST_CASE( InventoryTest, CrafterConsumeHandlerAndGridInventoryShapes )
{
    ItemCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kItemTestXml, "InventoryTest" ) );
    RecipeCatalog recipes;
    SW_ASSERT_TRUE( recipes.loadFromXmlText( kRecipeTestXml, "InventoryTest" ) );
    Inventory inventory;
    inventory.initialize( &catalog, 6 );
    (void)inventory.addItem( hashed_string( "herb" ), 3 );
    (void)inventory.addItem( hashed_string( "mortar" ), 1 );
    Crafter crafter;
    crafter.initialize( &recipes );
    SW_EXPECT_TRUE( crafter.getCatalog() == &recipes );

    // 재료를 다른 장부에서 거둔다 — 인벤토리의 약초는 그대로, 결과만 들어온다.
    int32 ledgerHerb = 3;
    crafter.setConsumeInputsHandler( Crafter::ConsumeInputsDelegate::create(
        [&ledgerHerb]( const ItemBag& inputs, int32 count )
    {
        const int32 needed = inputs.getItemCount( hashed_string( "herb" ) ) * count;
        if ( ledgerHerb < needed )
            return false;
        ledgerHerb -= needed;
        return true;
    } ) );
    const hashed_string alchemy( "Alchemy" );
    SW_EXPECT_TRUE( crafter.craft( hashed_string( "brew" ), inventory, alchemy, 1 ) == CraftResult::Ok );
    SW_EXPECT_EQUAL( 0, ledgerHerb );
    SW_EXPECT_EQUAL( 3, inventory.getItemCount( hashed_string( "herb" ) ) );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( hashed_string( "potion" ) ) );
    SW_EXPECT_TRUE( crafter.craft( hashed_string( "brew" ), inventory, alchemy, 1 ) == CraftResult::MissingInputs ); // 장부가 비었다
    SW_EXPECT_TRUE( crafter.enqueue( hashed_string( "brew" ), inventory, alchemy, 1 ) == CraftResult::MissingInputs );

    // 기반 격자 가방 — 모양은 연결 함수로.
    GridInventory grid;
    grid.initialize( GridInventory::ShapeDelegate::create(
                         []( const hashed_string& itemId, GridItemShape& outShape )
    {
        if ( itemId == hashed_string( "rifle" ) )
        {
            outShape._width  = 3;
            outShape._height = 1;
            return true;
        }
        if ( itemId == hashed_string( "ammo" ) )
        {
            outShape._maxStack = 30;
            return true;
        }
        return false;
    } ),
                     3, 2 );
    SW_EXPECT_TRUE( grid.placeItem( hashed_string( "rifle" ), 1, 0, 0, false ) > 0 );
    SW_EXPECT_FALSE( grid.canPlace( hashed_string( "rifle" ), 0, 1, true ) );            // 세우면 3 칸 — 2 줄을 넘는다
    SW_EXPECT_EQUAL( -1, grid.placeItem( hashed_string( "unknown" ), 1, 0, 1, false ) ); // 모르는 아이템
    SW_EXPECT_EQUAL( 45, grid.addItem( hashed_string( "ammo" ), 45 ) );                  // 30 + 15 — 두 칸
    SW_EXPECT_EQUAL( 1, grid.countFreeCells() );
    SW_EXPECT_EQUAL( 45, grid.getItemCount( hashed_string( "ammo" ) ) );
}
