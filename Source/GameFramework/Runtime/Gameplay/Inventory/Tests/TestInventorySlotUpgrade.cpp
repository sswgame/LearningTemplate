// InventorySlotUpgradeTest — 아이템 슬롯의 꾸미기 스키마 판: 왕복, 더 새 판 거절, 판을 찍지 않은 값은 바이트에 실리지 않는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

using namespace sw;

namespace
{
    constexpr const utf8* kCatalogXML = R"(<ItemCatalog>
  <Item id="sword" category="Weapon" slot="MainHand" maxStack="1" weight="3" durability="100"/>
  <Item id="arrow" category="Ammo" maxStack="20" weight="0.1"/>
</ItemCatalog>)";

    const float4 kRed{ 0.9f, 0.1f, 0.1f, 1.0f };
} // namespace

/** @brief [InventorySlotUpgradeTest] 지금 판은 슬롯의 꾸미기 스키마 판을 그대로 싣는다. 이 빌드보다 새 판 인벤토리는 거절한다(음성 대조: 지금 판은 그대로 읽힌다) */
SW_TEST_CASE( InventorySlotUpgradeTest, CurrentRoundTripAndNewerRefused )
{
    ItemCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCatalogXML, "InventorySlotUpgradeTest" ) );
    Inventory original;
    SW_REQUIRE_TRUE( original.initialize( catalog, 2 ) );
    InventorySlot dyed;
    dyed._itemID = hashed_string( "sword" );
    dyed._count  = 1;
    CustomizationValueSet primary;
    primary.setColor( hashed_string( "Primary" ), kRed );
    primary.setSchemaVersion( 7 );
    SW_REQUIRE_TRUE( dyed.setCustomization( primary ) );
    SW_REQUIRE_TRUE( original.addStack( dyed ) );
    Archive written;
    original.writeState( written );
    Inventory restored;
    SW_REQUIRE_TRUE( restored.initialize( catalog, 2 ) );
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( restored.getSlot( 0 ).getCustomization().getSchemaVersion() == uint32{ 7 } );

    Archive future( written.getData(), written.getSize() );
    SW_EXPECT_LOG( "FormatUpgrade.NewerThanSupported" );
    SW_EXPECT_FALSE( StateIO::upgradeSectionBody( Inventory::kStateFormat, Inventory::kStateFormat._version + 1, future ) );
    SW_EXPECT_TRUE( StateIO::upgradeSectionBody( Inventory::kStateFormat, Inventory::kStateFormat._version, future ) );
}

/**
 * @brief [InventorySlotUpgradeTest] 판을 찍지 않은 값은 슬롯에 들어가지 못한다 — 슬롯을 바이트로 쓰는 쪽은 판 없는 값을 만날 수 없다.
 *        스키마 입구(`applyValue`)를 거친 값은 쓴 때의 판이 찍혀 읽힌다.
 */
SW_TEST_CASE( InventorySlotUpgradeTest, UnversionedValuesNeverReachBytes )
{
    CustomizationValueSet unversioned;
    unversioned.setColor( hashed_string( "Tint" ), kRed );
    InventorySlot sword;
    sword._itemID = hashed_string( "sword" );
    sword._count  = 1;
    SW_EXPECT_FALSE( sword.setCustomization( unversioned ) );
    SW_EXPECT_TRUE( sword.getCustomization().isEmpty() );
    SW_EXPECT_TRUE( sword.setCustomization( CustomizationValueSet{} ) ); // 빈 집합은 판이 없어도 된다

    // 이 실행의 스키마는 판 2 — 값 넣기가 판 2 를 찍는다
    XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( R"(<CustomizationSchemaCatalog><Schema id="Armor" version="2"><Migration toVersion="1"/><Migration toVersion="2"/>
        <Color name="Tint" default="1 1 1 1"/></Schema></CustomizationSchemaCatalog>)" ) );
    CustomizationSchemaCatalog schemas;
    AppearanceLoadReport       report;
    SW_REQUIRE_TRUE( schemas.loadFromXML( document.getRoot( "CustomizationSchemaCatalog" ), report, "test" ) );
    CustomizationValue tint;
    tint._parameter = hashed_string( "Tint" );
    tint._number    = kRed;
    CustomizationValueSet dyed;
    SW_REQUIRE_TRUE( schemas.findSchema( hashed_string( "Armor" ) )->applyValue( tint, false, dyed ) );
    SW_REQUIRE_TRUE( sword.setCustomization( dyed ) );

    ItemCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCatalogXML, "InventorySlotUpgradeTest" ) );
    Inventory original;
    SW_REQUIRE_TRUE( original.initialize( catalog, 1 ) );
    SW_REQUIRE_TRUE( original.addStack( sword ) );
    Archive written;
    original.writeState( written );
    Inventory restored;
    SW_REQUIRE_TRUE( restored.initialize( catalog, 1 ) );
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( restored.getSlot( 0 ).getCustomization().getSchemaVersion() == uint32{ 2 } );
}
