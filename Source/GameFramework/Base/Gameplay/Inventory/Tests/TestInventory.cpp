#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

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
    SW_EXPECT_FALSE( inventory.removeItem( kArrow, 26 ) );
    SW_EXPECT_EQUAL( 25, inventory.getItemCount( kArrow ) );
    SW_EXPECT_EQUAL( revision, inventory.getRevision() );

    SW_EXPECT_TRUE( inventory.removeItem( kArrow, 22 ) );
    SW_EXPECT_EQUAL( 3, inventory.getItemCount( kArrow ) );
    SW_EXPECT_TRUE( inventory.getRevision() != revision );
}

/**
 * @brief [InventoryTest] 상태를 쓰고 같은 크기의 인벤토리에 읽으면 슬롯이 같다. 슬롯 수가 다르면 읽지 않는다
 */
SW_TEST_CASE( InventoryTest, StateRoundTrips )
{
    sw::ItemCatalog catalog;
    loadCatalog( catalog );
    sw::Inventory original;
    SW_REQUIRE_TRUE( original.initialize( &catalog, 3, 50.0f ) );
    SW_REQUIRE_EQUAL( 12, original.addItem( kArrow, 12 ) );
    SW_REQUIRE_EQUAL( 1, original.addItem( kSword, 1 ) );
    SW_EXPECT_FALSE( original.wearSlot( original.findFirstSlot( kSword ), 30.0f ) ); // 깎였지만 부서지지 않았다(70 남음)

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
    SW_EXPECT_FALSE( smaller.readState( again ) );
}
