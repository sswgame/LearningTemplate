// InventorySlotUpgradeTest — 아이템 슬롯에 꾸미기 스키마 판 칸이 생긴 판 올림: 손으로 쓴 옛 판 바이트(INVT 2 · CEQP 1 · CRFT 2 · SHOP 2)가 지금 판 바이트와 같아지고,
// 염색 값이 스키마 판 1 로 남는다, 지금 판 왕복, 더 새 판 거절(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

using namespace sw;

namespace
{
    constexpr const utf8* kCatalogXML = R"(<ItemCatalog>
  <Item id="sword" category="Weapon" slot="MainHand" maxStack="1" weight="3" durability="100"/>
  <Item id="arrow" category="Ammo" maxStack="20" weight="0.1"/>
</ItemCatalog>)";

    const float4 kRed{ 0.9f, 0.1f, 0.1f, 1.0f };

    /**
     * @brief 슬롯 하나를 손으로 씁니다. @p schemaVersion 이 0 이면 판 칸이 없던 옛 모양이다.
     * @details 이름 · 개수 · 내구도 · 피해 · [꾸미기 스키마 판] · 꾸미기 수 + (매개변수 · 항목 · 수 넷) · 뗀 부품 수 + 이름.
     */
    void writeSlot( Archive& out, const utf8* pItem, int32 count, bool bDyed, uint32 schemaVersion )
    {
        StateIO::writeName( out, hashed_string( pItem ) );
        out << count << 40.0f << 0.25f;
        if ( schemaVersion != 0 )
            out << schemaVersion;
        out << uint32{ bDyed ? 1u : 0u };
        if ( bDyed )
        {
            StateIO::writeName( out, hashed_string( "Tint" ) );
            StateIO::writeName( out, hashed_string{} );
            out << kRed;
        }
        out << uint32{ bDyed ? 1u : 0u };
        if ( bDyed )
            StateIO::writeName( out, hashed_string( "Plume" ) );
    }

    void writeEmptySlot( Archive& out, uint32 schemaVersion )
    {
        StateIO::writeName( out, hashed_string{} );
        out << int32{ 0 } << 0.0f << 0.0f;
        if ( schemaVersion != 0 )
            out << schemaVersion;
        out << uint32{ 0 } << uint32{ 0 };
    }

    /** @brief 인벤토리 본문: 최대 무게 · 넘침 0 · 슬롯 2(염색한 검, 빈 칸). @p schemaVersion 0 은 판 2 모양. */
    void writeInventoryBody( Archive& out, uint32 schemaVersion )
    {
        out << 12.0f << uint32{ 0 } << uint32{ 2 };
        writeSlot( out, "sword", 1, true, schemaVersion );
        writeEmptySlot( out, schemaVersion );
    }

    vector<uint8> toBytes( const Archive& archive )
    {
        return vector<uint8>( archive.getData(), archive.getData() + archive.getSize() );
    }

    /** @brief 옛 판 본문 @p oldBody 를 표 @p tag 의 지금 판 @p toVersion 까지 올린 바이트가 @p expected 와 같은지 봅니다. */
    void expectUpgradesTo( uint32 tag, uint32 fromVersion, uint32 toVersion, const Archive& oldBody, const Archive& expected )
    {
        Archive body( oldBody.getData(), oldBody.getSize() );
        SW_REQUIRE_TRUE( StateIO::upgradeSectionBody( tag, fromVersion, toVersion, body ) );
        SW_EXPECT_TRUE( toBytes( body ) == toBytes( expected ) );
    }
} // namespace

/** @brief [InventorySlotUpgradeTest] INVT 판 2 → 3: 염색한 검의 값 · 뗀 부품이 그대로이고 꾸미기 스키마 판 1 이 찍힌다. 올린 바이트는 판 3 을 손으로 쓴 것과 같다 */
SW_TEST_CASE( InventorySlotUpgradeTest, InventoryVersionTwoKeepsDye )
{
    Archive versionTwo;
    writeInventoryBody( versionTwo, 0 );
    Archive versionThree;
    writeInventoryBody( versionThree, CustomizationValueSet::kFirstSchemaVersion );
    expectUpgradesTo( Inventory::kStateTag, 2, Inventory::kStateVersion, versionTwo, versionThree );

    ItemCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCatalogXML, "InventorySlotUpgradeTest" ) );
    Archive body( versionTwo.getData(), versionTwo.getSize() );
    SW_REQUIRE_TRUE( StateIO::upgradeSectionBody( Inventory::kStateTag, 2, Inventory::kStateVersion, body ) );
    Inventory restored;
    SW_REQUIRE_TRUE( restored.initialize( &catalog, 2 ) );
    SW_REQUIRE_TRUE( restored.readState( body ) );
    const InventorySlot& sword = restored.getSlot( 0 );
    SW_EXPECT_TRUE( sword._itemID == hashed_string( "sword" ) );
    SW_EXPECT_EQUAL( CustomizationValueSet::kFirstSchemaVersion, sword._customization.getSchemaVersion() );
    SW_REQUIRE_NOT_NULL( sword._customization.findValue( hashed_string( "Tint" ) ) );
    SW_EXPECT_TRUE( sword._customization.findValue( hashed_string( "Tint" ) )->_number == kRed );
    SW_REQUIRE_EQUAL( size_t{ 1 }, sword._listDetachedPart.size() );
    SW_EXPECT_NEAR( 0.25f, sword._damage, 1.0e-6f );
}

/** @brief [InventorySlotUpgradeTest] 지금 판은 슬롯의 꾸미기 스키마 판을 그대로 싣는다. 이 빌드보다 새 판 인벤토리는 거절한다(음성 대조: 지금 판은 그대로 읽힌다) */
SW_TEST_CASE( InventorySlotUpgradeTest, CurrentRoundTripAndNewerRefused )
{
    ItemCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCatalogXML, "InventorySlotUpgradeTest" ) );
    Inventory original;
    SW_REQUIRE_TRUE( original.initialize( &catalog, 2 ) );
    InventorySlot dyed;
    dyed._itemID = hashed_string( "sword" );
    dyed._count  = 1;
    dyed._customization.setColor( hashed_string( "Primary" ), kRed );
    dyed._customization.setSchemaVersion( 7 );
    SW_REQUIRE_TRUE( original.addStack( dyed ) );
    Archive written;
    original.writeState( written );
    Inventory restored;
    SW_REQUIRE_TRUE( restored.initialize( &catalog, 2 ) );
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( uint32{ 7 }, restored.getSlot( 0 )._customization.getSchemaVersion() );

    Archive future( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( StateIO::upgradeSectionBody( Inventory::kStateTag, Inventory::kStateVersion + 1, Inventory::kStateVersion, future ) );
    SW_EXPECT_LOG( "FormatUpgrade.NewerThanSupported" );
    SW_EXPECT_TRUE( StateIO::upgradeSectionBody( Inventory::kStateTag, Inventory::kStateVersion, Inventory::kStateVersion, future ) );
}

/** @brief [InventorySlotUpgradeTest] CEQP 판 1 → 2: 낀 칸(칸 이름 + 슬롯)과 가방(인벤토리 판 2 본문)의 슬롯 모두에 판 칸이 들어간다 */
SW_TEST_CASE( InventorySlotUpgradeTest, EquipmentVersionOneUpgrades )
{
    const auto writeEquipment = []( Archive& out, uint32 schemaVersion )
    {
        out << uint32{ 1 };
        StateIO::writeName( out, hashed_string( "MainHand" ) );
        writeSlot( out, "sword", 1, true, schemaVersion );
        writeInventoryBody( out, schemaVersion );
    };
    Archive versionOne;
    writeEquipment( versionOne, 0 );
    Archive versionTwo;
    writeEquipment( versionTwo, CustomizationValueSet::kFirstSchemaVersion );
    expectUpgradesTo( CharacterEquipmentComponent::kStateTag, 1, CharacterEquipmentComponent::kStateVersion, versionOne, versionTwo );
}

/** @brief [InventorySlotUpgradeTest] CRFT 판 2 → 3: 배운 레시피 · 작업 칸은 그대로이고 꺼낸 재료 조각 슬롯에만 판 칸이 들어간다 */
SW_TEST_CASE( InventorySlotUpgradeTest, CrafterVersionTwoUpgrades )
{
    const auto writeCrafter = []( Archive& out, uint32 schemaVersion )
    {
        out << uint32{ 1 };
        StateIO::writeName( out, hashed_string( "Reforge" ) );
        out << uint32{ 1 }; // 작업 하나
        StateIO::writeName( out, hashed_string( "Reforge" ) );
        out << 5.0f << int32{ 1 } << uint32{ 1 } << uint32{ 2 }; // 남은 시간 · 횟수 · 횟수마다 조각 목록 1 · 조각 2
        writeSlot( out, "sword", 1, true, schemaVersion );
        writeSlot( out, "arrow", 3, false, schemaVersion );
    };
    Archive versionTwo;
    writeCrafter( versionTwo, 0 );
    Archive versionThree;
    writeCrafter( versionThree, CustomizationValueSet::kFirstSchemaVersion );
    expectUpgradesTo( Crafter::kStateTag, 2, Crafter::kStateVersion, versionTwo, versionThree );
}

/** @brief [InventorySlotUpgradeTest] SHOP 판 2 → 3: 재고 · 시세 · 배율 칸은 그대로이고 되사기 조각 슬롯에만 판 칸이 들어간다 */
SW_TEST_CASE( InventorySlotUpgradeTest, ShopVersionTwoUpgrades )
{
    const auto writeShops = []( Archive& out, uint32 schemaVersion )
    {
        out << uint32{ 1 };
        StateIO::writeName( out, hashed_string( "smith" ) );
        out << uint32{ 2 } << int32{ 3 } << int32{ -1 }; // 재고
        out << uint32{ 1 };                              // 시세가 떨어진 아이템
        StateIO::writeName( out, hashed_string( "arrow" ) );
        out << 0.8f;
        out << uint32{ 0 };                // 사들이지 않는 분류
        out << 1.0f << 0.9f << int32{ 2 }; // 배율 둘 · 날짜
        out << uint32{ 1 };                // 되사기 하나
        StateIO::writeName( out, hashed_string( "sword" ) );
        out << int64{ 120 } << uint32{ 1 };
        writeSlot( out, "sword", 1, true, schemaVersion );
    };
    Archive versionTwo;
    writeShops( versionTwo, 0 );
    Archive versionThree;
    writeShops( versionThree, CustomizationValueSet::kFirstSchemaVersion );
    expectUpgradesTo( ShopManager::kStateTag, 2, ShopManager::kStateVersion, versionTwo, versionThree );
}
