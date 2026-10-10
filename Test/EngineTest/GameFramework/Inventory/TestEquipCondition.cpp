#include "pch.h"

#include "EngineTest/AppearanceTestFixture.h"

#include "GameFramework/Base/Gameplay/Inventory/EquipCondition.h"
#include "GameFramework/Base/Gameplay/Inventory/Equipment.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

#include "TestFramework/TestFramework.h"

// 장착 조건 — 세트 완성 · n 조각 · 다른 장비의 태그 · 캐릭터 태그 · 체형을 Equipment::canEquip 이 판정하고, 조건이 깨질 때 아이템마다 고른 정책
// (함께 벗기 · 낀 채 숨기기 · 벗기 거부)대로 처리한다.

using namespace sw;
using appearancetest::Fixture;

namespace
{
    struct EquipConditionTestInternal
    {
        static InventorySlot makeItem( const utf8* pItemID )
        {
            InventorySlot item;
            item._itemID = hashed_string( pItemID );
            item._count  = 1;
            return item;
        }

        static EquipResult equip( Equipment& equipment, const utf8* pSlot, const utf8* pItemID )
        {
            vector<InventorySlot> listRemoved;
            return equipment.equip( hashed_string( pSlot ), makeItem( pItemID ), listRemoved );
        }

        /** @brief 기사 세트 셋을 낍니다. */
        static bool equipKnightSet( Equipment& equipment )
        {
            return equip( equipment, "Head", "helm" ) == EquipResult::Ok && equip( equipment, "Body", "plate" ) == EquipResult::Ok && equip( equipment, "Legs", "greaves" ) == EquipResult::Ok;
        }
    };
} // namespace

/**
 * @brief [EquipConditionTest] 세트 전용 망토는 세트를 다 입기 전에는 끼기를 거부하고, 다 입으면 낀다
 */
SW_TEST_CASE( EquipConditionTest, SetOnlyItemIsRefusedUntilTheSetIsComplete )
{
    using Internal = EquipConditionTestInternal;
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    Equipment equipment;
    fixture.makeEquipment( equipment );

    SW_EXPECT_TRUE( equipment.evaluateEquip( hashed_string( "Back" ), hashed_string( "cape_hidden" ) ) == EquipResult::ConditionNotMet );
    SW_EXPECT_TRUE( Internal::equip( equipment, "Head", "helm" ) == EquipResult::Ok );
    SW_EXPECT_TRUE( Internal::equip( equipment, "Body", "plate" ) == EquipResult::Ok );
    SW_EXPECT_FALSE( equipment.canEquip( hashed_string( "Back" ), hashed_string( "cape_hidden" ) ) ); // 두 조각
    SW_EXPECT_TRUE( equipment.canEquip( hashed_string( "Back" ), hashed_string( "banner" ) ) );       // 두 조각이면 되는 깃발
    SW_EXPECT_TRUE( Internal::equip( equipment, "Legs", "greaves" ) == EquipResult::Ok );
    const uint32 revision = equipment.getRevision();
    SW_EXPECT_TRUE( Internal::equip( equipment, "Back", "cape_hidden" ) == EquipResult::Ok );
    SW_EXPECT_TRUE( equipment.getRevision() != revision );

    // 몸 종류 변형 — 여성 몸은 plate_f 가 몸통 조각이다.
    Equipment             female;
    vector<InventorySlot> listRemoved;
    fixture.makeEquipment( female );
    EquipCharacterContext context;
    context._bodyType = hashed_string( "Female" );
    female.setCharacterContext( context, listRemoved );
    SW_EXPECT_TRUE( Internal::equip( female, "Head", "helm" ) == EquipResult::Ok );
    SW_EXPECT_TRUE( Internal::equip( female, "Body", "plate_f" ) == EquipResult::Ok );
    SW_EXPECT_TRUE( Internal::equip( female, "Legs", "greaves" ) == EquipResult::Ok );
    SW_EXPECT_TRUE( female.canEquip( hashed_string( "Back" ), hashed_string( "cape_hidden" ) ) );
}

/**
 * @brief [EquipConditionTest] 조건이 깨지면 아이템마다 고른 정책대로 — 낀 채 숨기기(능력치 빠짐, 다시 갖추면 돌아옴) · 함께 벗기 · 벗기 거부
 */
SW_TEST_CASE( EquipConditionTest, BrokenConditionFollowsThePerItemPolicy )
{
    using Internal = EquipConditionTestInternal;
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );

    // 낀 채 숨기기.
    {
        Equipment equipment;
        fixture.makeEquipment( equipment );
        SW_ASSERT_TRUE( Internal::equipKnightSet( equipment ) );
        SW_ASSERT_TRUE( Internal::equip( equipment, "Back", "cape_hidden" ) == EquipResult::Ok );
        StatBlock stats;
        equipment.computeStats( stats );
        SW_EXPECT_NEAR_EQUAL( 3.0f, stats.getValue( hashed_string( "charisma" ) ), 1.0e-4f );

        vector<InventorySlot> listRemoved;
        SW_EXPECT_TRUE( equipment.unequip( hashed_string( "Head" ), listRemoved ) == EquipResult::Ok );
        SW_EXPECT_EQUAL( size_t( 1 ), listRemoved.size() ); // 투구만 — 망토는 남는다
        SW_EXPECT_TRUE( equipment.findEquipped( hashed_string( "Back" ) ) != nullptr );
        SW_EXPECT_TRUE( equipment.isSuppressed( hashed_string( "Back" ) ) );
        equipment.computeStats( stats );
        SW_EXPECT_NEAR_EQUAL( 0.0f, stats.getValue( hashed_string( "charisma" ) ), 1.0e-4f );

        // 다시 갖추면 숨김이 풀린다.
        SW_EXPECT_TRUE( Internal::equip( equipment, "Head", "helm" ) == EquipResult::Ok );
        SW_EXPECT_FALSE( equipment.isSuppressed( hashed_string( "Back" ) ) );
    }
    // 함께 벗기.
    {
        Equipment equipment;
        fixture.makeEquipment( equipment );
        SW_ASSERT_TRUE( Internal::equipKnightSet( equipment ) );
        SW_ASSERT_TRUE( Internal::equip( equipment, "Back", "cape_together" ) == EquipResult::Ok );
        vector<InventorySlot> listRemoved;
        // 바꿔 끼기도 세트를 깬다 — 판갑을 셔츠로.
        SW_EXPECT_TRUE( equipment.equip( hashed_string( "Body" ), Internal::makeItem( "shirt" ), listRemoved ) == EquipResult::Ok );
        SW_ASSERT_EQUAL( size_t( 2 ), listRemoved.size() );
        SW_EXPECT_TRUE( listRemoved[0]._itemID == hashed_string( "plate" ) );
        SW_EXPECT_TRUE( listRemoved[1]._itemID == hashed_string( "cape_together" ) );
        SW_EXPECT_TRUE( equipment.findEquipped( hashed_string( "Back" ) ) == nullptr );
    }
    // 벗기 거부 — 아무것도 바뀌지 않는다.
    {
        Equipment equipment;
        fixture.makeEquipment( equipment );
        SW_ASSERT_TRUE( Internal::equipKnightSet( equipment ) );
        SW_ASSERT_TRUE( Internal::equip( equipment, "Back", "cape_refuse" ) == EquipResult::Ok );
        const uint32          revision = equipment.getRevision();
        vector<InventorySlot> listRemoved;
        SW_EXPECT_TRUE( equipment.unequip( hashed_string( "Legs" ), listRemoved ) == EquipResult::RefusedByDependent );
        SW_EXPECT_TRUE( listRemoved.empty() );
        SW_EXPECT_TRUE( Internal::equip( equipment, "Body", "shirt" ) == EquipResult::RefusedByDependent );
        SW_EXPECT_TRUE( equipment.findEquipped( hashed_string( "Legs" ) ) != nullptr );
        SW_EXPECT_EQUAL( revision, equipment.getRevision() );
        // 망토부터 벗으면 된다.
        SW_EXPECT_TRUE( equipment.unequip( hashed_string( "Back" ), listRemoved ) == EquipResult::Ok );
        SW_EXPECT_TRUE( equipment.unequip( hashed_string( "Legs" ), listRemoved ) == EquipResult::Ok );
    }
}

/**
 * @brief [EquipConditionTest] 다른 장비의 태그(하위 태그 포함) · 캐릭터 태그 · 체형 조건 — 캐릭터 쪽이 바뀌어 깨지면 정책대로(기본은 함께 벗기)
 */
SW_TEST_CASE( EquipConditionTest, TagCharacterTagAndBodyShapeConditions )
{
    using Internal = EquipConditionTestInternal;
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    Equipment equipment;
    fixture.makeEquipment( equipment );

    // 인장은 "Armor" 아래 태그를 가진 다른 장비가 있어야 한다 — 판갑은 Armor.Heavy.
    SW_EXPECT_TRUE( Internal::equip( equipment, "Ring", "sigil" ) == EquipResult::ConditionNotMet );
    SW_EXPECT_TRUE( Internal::equip( equipment, "Body", "shirt" ) == EquipResult::Ok );
    SW_EXPECT_FALSE( equipment.canEquip( hashed_string( "Ring" ), hashed_string( "sigil" ) ) );
    SW_EXPECT_TRUE( Internal::equip( equipment, "Body", "plate" ) == EquipResult::Ok );
    SW_EXPECT_TRUE( Internal::equip( equipment, "Ring", "sigil" ) == EquipResult::Ok );

    SW_EXPECT_TRUE( Internal::equip( equipment, "Head", "crown" ) == EquipResult::ConditionNotMet );
    SW_EXPECT_TRUE( Internal::equip( equipment, "Belt", "belt_big" ) == EquipResult::ConditionNotMet );
    EquipCharacterContext context;
    context._tags.addTag( TagID::request( "Class.Knight.Paladin" ) );
    context._bodyShape = hashed_string( "Huge" );
    vector<InventorySlot> listRemoved;
    equipment.setCharacterContext( context, listRemoved );
    SW_EXPECT_TRUE( Internal::equip( equipment, "Head", "crown" ) == EquipResult::Ok );
    SW_EXPECT_TRUE( Internal::equip( equipment, "Belt", "belt_big" ) == EquipResult::Ok );

    // 체형이 바뀌면 큰 허리띠의 조건이 깨진다 — 기본 정책(함께 벗기)으로 돌려받는다.
    context._bodyShape = hashed_string( "Thin" );
    equipment.setCharacterContext( context, listRemoved );
    SW_ASSERT_EQUAL( size_t( 1 ), listRemoved.size() );
    SW_EXPECT_TRUE( listRemoved[0]._itemID == hashed_string( "belt_big" ) );
    SW_EXPECT_TRUE( equipment.findEquipped( hashed_string( "Head" ) ) != nullptr );
}

/**
 * @brief [EquipConditionTest] 인벤토리로 벗기 — 함께 벗겨진 것까지 넣을 자리가 없으면 장비와 인벤토리 모두 그대로다
 */
SW_TEST_CASE( EquipConditionTest, UnequipToInventoryRollsBackTheCascadeWhenFull )
{
    using Internal = EquipConditionTestInternal;
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    Equipment equipment;
    fixture.makeEquipment( equipment );
    SW_ASSERT_TRUE( Internal::equipKnightSet( equipment ) );
    SW_ASSERT_TRUE( Internal::equip( equipment, "Back", "cape_together" ) == EquipResult::Ok );

    Inventory inventory;
    inventory.initialize( &fixture._items, 1 );
    SW_EXPECT_FALSE( equipment.unequipToInventory( hashed_string( "Head" ), inventory ) ); // 투구 + 망토 = 두 칸
    SW_EXPECT_TRUE( equipment.findEquipped( hashed_string( "Head" ) ) != nullptr );
    SW_EXPECT_TRUE( equipment.findEquipped( hashed_string( "Back" ) ) != nullptr );
    SW_EXPECT_EQUAL( 1, inventory.countEmptySlots() );

    inventory.initialize( &fixture._items, 2 );
    SW_EXPECT_TRUE( equipment.unequipToInventory( hashed_string( "Head" ), inventory ) );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( hashed_string( "cape_together" ) ) );
}

/**
 * @brief [EquipConditionTest] 인스턴스 상태(꾸미기 · 피해 · 떨어진 부품)가 있는 아이템은 같은 id 와 겹치지 않는다
 */
SW_TEST_CASE( EquipConditionTest, InstanceStateKeepsItemsFromStacking )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    ItemCatalog catalog;
    ItemDef     gem;
    gem._id       = hashed_string( "gem" );
    gem._maxStack = 10;
    catalog.addItem( gem );
    Inventory inventory;
    inventory.initialize( &catalog, 4 );
    SW_EXPECT_EQUAL( 1, inventory.addItem( hashed_string( "gem" ), 1 ) );
    InventorySlot dyed;
    dyed._itemID = hashed_string( "gem" );
    dyed._count  = 1;
    dyed._customization.setColor( hashed_string( "Tint" ), float4( 1.0f, 0.0f, 0.0f, 1.0f ) );
    SW_EXPECT_TRUE( inventory.addStack( dyed ) );
    SW_EXPECT_EQUAL( 2, inventory.countEmptySlots() );
    SW_EXPECT_TRUE( inventory.getSlot( 1 )._customization.findValue( hashed_string( "Tint" ) ) != nullptr );
}
