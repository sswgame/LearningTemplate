#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

// InventoryTest — 넣기(겹침 · 무게 한도), 빼기(모두 있을 때만), 상태 저장 왕복. 카탈로그는 글로 준다(nogpu).

namespace
{
    constexpr const utf8* kCatalogXML = R"(<ItemCatalog>
  <Item id="arrow" category="Ammo" maxStack="20" weight="0.1"/>
  <Item id="sword" category="Weapon" slot="MainHand" maxStack="1" weight="3" durability="100"/>
  <Item id="stone" category="Material" maxStack="10" weight="2"/>
</ItemCatalog>)";

    void loadCatalog( sw::ItemCatalog& outCatalog )
    {
        SW_REQUIRE_TRUE( outCatalog.loadFromXMLText( kCatalogXML, "InventoryTest" ) );
    }

    const sw::hashed_string kArrow{ "arrow" };
    const sw::hashed_string kSword{ "sword" };
    const sw::hashed_string kStone{ "stone" };
} // namespace

/**
 * @brief [InventoryTest] 같은 아이템이 든 슬롯을 먼저 채우고, 넘치는 만큼 빈 슬롯으로 간다. 슬롯이 모자라면 넣은 수만 돌려준다
 */
SW_TEST_CASE( InventoryTest, FillsMatchingStacksFirst )
{
    sw::ItemCatalog catalog;
    loadCatalog( catalog );
    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &catalog, 3 ) );

    SW_EXPECT_EQUAL( 15, inventory.addItem( kArrow, 15 ) );
    SW_EXPECT_EQUAL( 1, inventory.addItem( kSword, 1 ) );
    SW_EXPECT_EQUAL( 10, inventory.addItem( kArrow, 10 ) ); // 첫 슬롯을 20 으로 채우고 5 는 마지막 빈 슬롯으로
    SW_EXPECT_EQUAL( 20, inventory.getSlot( 0 )._count );
    SW_EXPECT_EQUAL( 5, inventory.getSlot( 2 )._count );
    SW_EXPECT_EQUAL( 0, inventory.countEmptySlots() );

    SW_EXPECT_EQUAL( 15, inventory.addItem( kArrow, 40 ) ); // 화살 칸 둘의 남은 자리(0 + 15)만
    SW_EXPECT_EQUAL( 40, inventory.getItemCount( kArrow ) );
    SW_EXPECT_FALSE( inventory.hasRoomFor( kStone, 1 ) );
}

/**
 * @brief [InventoryTest] 무게 한도를 넘는 만큼은 들어가지 않는다
 */
SW_TEST_CASE( InventoryTest, StopsAtTheWeightLimit )
{
    sw::ItemCatalog catalog;
    loadCatalog( catalog );
    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &catalog, 10, 7.0f ) );

    SW_EXPECT_EQUAL( 3, inventory.addItem( kStone, 5 ) ); // 2 × 3 = 6, 넷째는 8 이라 넘는다
    SW_EXPECT_TRUE( inventory.computeWeight() <= inventory.getMaxWeight() );
    SW_EXPECT_LOG( "Inventory.NegativeLimit" );
    SW_EXPECT_FALSE( inventory.setMaxWeight( -1.0f ) );
}

/**
 * @brief [InventoryTest] 빼기는 모두 있을 때만 한다 — 제작 · 거래가 반쯤 끝난 상태로 남지 않는다
 */
SW_TEST_CASE( InventoryTest, RemovesAllOrNothing )
{
    sw::ItemCatalog catalog;
    loadCatalog( catalog );
    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &catalog, 4 ) );
    SW_REQUIRE_EQUAL( 25, inventory.addItem( kArrow, 25 ) );

    const uint32 revision = inventory.getRevision();
    SW_EXPECT_FALSE( inventory.consumeItem( kArrow, 26 ) );
    SW_EXPECT_EQUAL( 25, inventory.getItemCount( kArrow ) );
    SW_EXPECT_EQUAL( revision, inventory.getRevision() );

    SW_EXPECT_TRUE( inventory.consumeItem( kArrow, 22 ) );
    SW_EXPECT_EQUAL( 3, inventory.getItemCount( kArrow ) );
    SW_EXPECT_TRUE( inventory.getRevision() != revision );
}

/**
 * @brief [InventoryTest] 상태를 쓰고 같은 크기의 인벤토리에 읽으면 슬롯이 같다. 슬롯 수가 줄어도 빈 슬롯에 들어가면 그대로 읽힌다
 */
SW_TEST_CASE( InventoryTest, StateRoundTrips )
{
    sw::ItemCatalog catalog;
    loadCatalog( catalog );
    sw::Inventory original;
    SW_REQUIRE_TRUE( original.initialize( &catalog, 3, 50.0f ) );
    SW_REQUIRE_EQUAL( 12, original.addItem( kArrow, 12 ) );
    SW_REQUIRE_EQUAL( 1, original.addItem( kSword, 1 ) );
    SW_EXPECT_FALSE( original.reduceSlotDurability( original.findFirstSlot( kSword ).value(), 30.0f ) ); // 깎였지만 부서지지 않았다(70 남음)

    sw::Archive written;
    original.writeState( written );

    sw::Inventory restored;
    SW_REQUIRE_TRUE( restored.initialize( &catalog, 3 ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( original.getMaxWeight(), restored.getMaxWeight() );
    for ( int32 slot = 0; slot < original.getSlotCount(); ++slot )
    {
        SW_EXPECT_EQUAL( original.getSlot( slot )._itemID, restored.getSlot( slot )._itemID );
        SW_EXPECT_EQUAL( original.getSlot( slot )._count, restored.getSlot( slot )._count );
        SW_EXPECT_EQUAL( original.getSlot( slot )._durability, restored.getSlot( slot )._durability );
    }

    sw::Inventory smaller;
    SW_REQUIRE_TRUE( smaller.initialize( &catalog, 2 ) );
    sw::Archive again( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( smaller.readState( again ) ); // 셋째 슬롯이 비어 있어 둘에 다 든다
    SW_EXPECT_EQUAL( 12, smaller.getItemCount( kArrow ) );
    SW_EXPECT_EQUAL( 1, smaller.getItemCount( kSword ) );
    SW_EXPECT_TRUE( smaller.getOverflow().empty() );
}

/**
 * @brief [InventoryTest] 슬롯 10 개 세이브를 설정 8 개 인벤토리로 읽으면 2 개가 넘침 목록으로 가고 잃은 아이템은 없다(감사 A-6).
 *        넘침 목록은 세이브에 실리고, 자리가 나면 돌려받고, UI 가 꺼내 갈 수 있다
 */
SW_TEST_CASE( InventoryTest, ShrunkSlotCountMovesExtraItemsToOverflow )
{
    sw::ItemCatalog catalog;
    loadCatalog( catalog );
    sw::Inventory original;
    SW_REQUIRE_TRUE( original.initialize( &catalog, 10 ) );
    SW_REQUIRE_EQUAL( 10, original.addItem( kSword, 10 ) ); // 겹치지 않는 칼 열 자루가 슬롯 열을 채운다
    sw::Archive written;
    original.writeState( written );

    sw::Inventory smaller;
    SW_REQUIRE_TRUE( smaller.initialize( &catalog, 8 ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( smaller.readState( reading ) );
    SW_EXPECT_EQUAL( 8, smaller.getItemCount( kSword ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, smaller.getOverflow().size() );
    int32 overflowCount = 0;
    for ( const sw::InventorySlot& stack : smaller.getOverflow() )
    {
        overflowCount += stack._count;
    }
    SW_EXPECT_EQUAL( 10, smaller.getItemCount( kSword ) + overflowCount ); // 잃은 아이템 0

    // 넘침 목록도 세이브에 실린다
    sw::Archive rewritten;
    smaller.writeState( rewritten );
    sw::Inventory reloaded;
    SW_REQUIRE_TRUE( reloaded.initialize( &catalog, 8 ) );
    sw::Archive rereading( rewritten.getData(), rewritten.getSize() );
    SW_REQUIRE_TRUE( reloaded.readState( rereading ) );
    SW_EXPECT_EQUAL( size_t{ 2 }, reloaded.getOverflow().size() );

    // 자리가 나면 돌려받고, 남은 것은 UI 가 꺼낸다
    SW_REQUIRE_TRUE( reloaded.consumeItem( kSword, 1 ) );
    SW_EXPECT_EQUAL( 1, reloaded.moveOverflowToSlots() );
    SW_EXPECT_EQUAL( 8, reloaded.getItemCount( kSword ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, reloaded.getOverflow().size() );
    const sw::InventorySlot taken = reloaded.takeOverflow( 0 );
    SW_EXPECT_EQUAL( kSword, taken._itemID );
    SW_EXPECT_TRUE( reloaded.getOverflow().empty() );
}

/**
 * @brief [InventoryTest] 판 1(넘침 목록 없음) 구간 본문을 판 올림 사슬(INVT 1 → 2)로 올려 읽는다 — 슬롯이 그대로다
 */
SW_TEST_CASE( InventoryTest, UpgradesVersionOneSectionBody )
{
    sw::ItemCatalog catalog;
    loadCatalog( catalog );
    sw::Inventory original;
    SW_REQUIRE_TRUE( original.initialize( &catalog, 2, 50.0f ) );
    SW_REQUIRE_EQUAL( 7, original.addItem( kArrow, 7 ) );
    sw::Archive written;
    original.writeState( written );

    // 판 1 바이트: 최대 무게(4) 뒤의 넘침 목록 개수(4)가 없다
    sw::Archive versionOne;
    versionOne.writeBytes( written.getData(), 4 );
    versionOne.writeBytes( written.getData() + 8, written.getSize() - 8 );
    sw::Archive body( versionOne.getData(), versionOne.getSize() );
    SW_REQUIRE_TRUE( sw::StateIO::upgradeSectionBody( sw::Inventory::kStateTag, 1, sw::Inventory::kStateVersion, body ) );

    sw::Inventory restored;
    SW_REQUIRE_TRUE( restored.initialize( &catalog, 2 ) );
    SW_REQUIRE_TRUE( restored.readState( body ) );
    SW_EXPECT_EQUAL( 50.0f, restored.getMaxWeight() );
    SW_EXPECT_EQUAL( 7, restored.getItemCount( kArrow ) );
    SW_EXPECT_TRUE( restored.getOverflow().empty() );
}

/**
 * @brief [CraftingTest] 닳은 장비가 재료인 작업을 대기열에 넣었다 취소하면 같은 내구도로 돌아온다(새것이 되지 않는다, 감사 G-3). 저장 · 복원한 대기열도 같다
 */
SW_TEST_CASE( CraftingTest, CancelReturnsWornInputWithSameDurability )
{
    sw::ItemCatalog catalog;
    loadCatalog( catalog );
    sw::RecipeCatalog recipes;
    SW_REQUIRE_TRUE( recipes.loadFromXMLText( R"(<RecipeCatalog>
        <Recipe id="Reforge" time="5"><In item="sword"/><In item="stone" count="2"/><Out item="arrow" count="5"/></Recipe>
    </RecipeCatalog>)",
                                              "CraftingTest" ) );
    const sw::hashed_string kReforge{ "Reforge" };

    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &catalog, 4 ) );
    SW_REQUIRE_EQUAL( 1, inventory.addItem( kSword, 1 ) );
    SW_REQUIRE_EQUAL( 2, inventory.addItem( kStone, 2 ) );
    const int32 swordSlot = inventory.findFirstSlot( kSword ).value();
    SW_EXPECT_FALSE( inventory.reduceSlotDurability( swordSlot, 60.0f ) ); // 40 남음

    sw::Crafter crafter;
    crafter.initialize( &recipes );
    crafter.learnRecipe( kReforge );
    SW_REQUIRE_TRUE( crafter.enqueue( kReforge, inventory, sw::hashed_string{}, 1 ) == sw::CraftResult::Ok );
    SW_EXPECT_EQUAL( 0, inventory.getItemCount( kSword ) );

    // 저장 · 복원한 대기열도 꺼낸 재료의 개체 상태를 들고 있다
    sw::Archive written;
    crafter.writeState( written );
    sw::Crafter restored;
    restored.initialize( &recipes );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );

    SW_REQUIRE_TRUE( restored.cancel( 0, inventory ) );
    const sw::optional<int32> returnedSlot = inventory.findFirstSlot( kSword );
    SW_REQUIRE_TRUE( returnedSlot.has_value() );
    SW_EXPECT_EQUAL( 40.0f, inventory.getSlot( returnedSlot.value() )._durability );
    SW_EXPECT_EQUAL( 2, inventory.getItemCount( kStone ) );
}

/**
 * @brief [ShopTest] 닳은 칼을 팔고 되사면 같은 내구도로 돌아오고 받은 돈 그대로 낸다(새것이 되지 않는다, 감사 G-3). 저장 · 복원한 되사기 목록도 같다
 */
SW_TEST_CASE( ShopTest, BuyBackReturnsWornItemWithSameDurability )
{
    sw::ItemCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<ItemCatalog>
  <Item id="sword" category="Weapon" slot="MainHand" maxStack="1" weight="3" value="40" durability="100"/>
</ItemCatalog>)",
                                              "ShopTest" ) );
    sw::ShopCatalog shops;
    SW_REQUIRE_TRUE( shops.loadFromXMLText( R"(<ShopCatalog><Shop id="smith" sellMultiplier="0.5" buybackLimit="2"/></ShopCatalog>)", "ShopTest" ) );
    const sw::hashed_string kSmith{ "smith" };

    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &catalog, 2 ) );
    SW_REQUIRE_EQUAL( 1, inventory.addItem( kSword, 1 ) );
    SW_EXPECT_FALSE( inventory.reduceSlotDurability( inventory.findFirstSlot( kSword ).value(), 60.0f ) ); // 40 남음

    sw::ShopManager shop;
    shop.initialize( &shops, &catalog );
    sw::Wallet wallet;
    int64      received = 0;
    SW_REQUIRE_TRUE( shop.sell( kSmith, kSword, 1, wallet, inventory, &received ) == sw::ShopResult::Ok );
    SW_EXPECT_EQUAL( int64( 20 ), received );
    SW_EXPECT_EQUAL( 0, inventory.getItemCount( kSword ) );

    // 저장 · 복원한 가게도 판 조각의 개체 상태를 들고 있다
    sw::Archive written;
    shop.writeState( written );
    sw::ShopManager restored;
    restored.initialize( &shops, &catalog );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );

    SW_EXPECT_TRUE( restored.buyBack( kSmith, 1, wallet, inventory ) == sw::ShopResult::InvalidCount );
    int64 paid = 0;
    SW_REQUIRE_TRUE( restored.buyBack( kSmith, 0, wallet, inventory, &paid ) == sw::ShopResult::Ok );
    SW_EXPECT_EQUAL( received, paid );
    SW_EXPECT_EQUAL( int64( 0 ), wallet.getBalance( restored.getCurrency( kSmith ) ) );
    const sw::optional<int32> returnedSlot = inventory.findFirstSlot( kSword );
    SW_REQUIRE_TRUE( returnedSlot.has_value() );
    SW_EXPECT_EQUAL( 40.0f, inventory.getSlot( returnedSlot.value() )._durability );
    SW_EXPECT_TRUE( restored.findRuntime( kSmith )->_listBuyback.empty() );
}

/**
 * @brief [ShopTest] 같은 id 의 칼 둘 중 닳은 것을 슬롯으로 골라 팔면 새 칼이 남고, 되사기는 따로 `BoughtBack` 으로 알린다(감사 G-3)
 */
SW_TEST_CASE( ShopTest, SellsChosenSlotAndReportsBuyBack )
{
    sw::ItemCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<ItemCatalog>
  <Item id="sword" category="Weapon" slot="MainHand" maxStack="1" weight="3" value="40" durability="100"/>
</ItemCatalog>)",
                                              "ShopTest" ) );
    sw::ShopCatalog shops;
    SW_REQUIRE_TRUE( shops.loadFromXMLText( R"(<ShopCatalog><Shop id="smith" buybackLimit="1"/></ShopCatalog>)", "ShopTest" ) );
    const sw::hashed_string kSmith{ "smith" };

    sw::Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &catalog, 3 ) );
    SW_REQUIRE_EQUAL( 2, inventory.addItem( kSword, 2 ) );         // 슬롯 0 · 1
    SW_EXPECT_FALSE( inventory.reduceSlotDurability( 0, 70.0f ) ); // 앞 칼만 닳았다(30 남음). id 판 `sell` 은 뒤 슬롯부터 뺀다

    sw::ShopManager shop;
    shop.initialize( &shops, &catalog );
    sw::Wallet wallet;
    SW_EXPECT_TRUE( shop.sell( kSmith, 2, 1, wallet, inventory ) == sw::ShopResult::NotOwned ); // 빈 슬롯
    SW_EXPECT_TRUE( shop.sell( kSmith, 0, 2, wallet, inventory ) == sw::ShopResult::NotOwned ); // 그 슬롯에 둘은 없다
    SW_REQUIRE_TRUE( shop.sell( kSmith, 0, 1, wallet, inventory ) == sw::ShopResult::Ok );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( kSword ) );
    SW_EXPECT_EQUAL( 100.0f, inventory.getSlot( inventory.findFirstSlot( kSword ).value() )._durability );
    SW_REQUIRE_EQUAL( size_t( 1 ), shop.findRuntime( kSmith )->_listBuyback.size() );
    SW_EXPECT_EQUAL( 30.0f, shop.findRuntime( kSmith )->_listBuyback[0]._listPiece[0]._durability );

    // 상점 데이터는 buybackLimit 를 적어야 한다(없으면 그 상점을 넣지 않는다 — 기본값에 기대지 않는다)
    sw::ShopCatalog missingLimit;
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    SW_EXPECT_LOG( "Shop.ShopInvalidFieldsSkipped" );
    SW_EXPECT_TRUE( missingLimit.loadFromXMLText( R"(<ShopCatalog><Shop id="smith" buybackLimit="0"/><Shop id="bare"/></ShopCatalog>)", "ShopTest" ) );
    SW_EXPECT_TRUE( missingLimit.find( sw::hashed_string{ "smith" } ) != nullptr );
    SW_EXPECT_TRUE( missingLimit.find( sw::hashed_string{ "bare" } ) == nullptr );

    SW_REQUIRE_TRUE( shop.buyBack( kSmith, 0, wallet, inventory ) == sw::ShopResult::Ok );
    sw::vector<sw::ShopEvent> listEvent;
    shop.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t( 2 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == sw::ShopEvent::Kind::Sold );
    SW_EXPECT_TRUE( listEvent[1]._kind == sw::ShopEvent::Kind::BoughtBack );
}
