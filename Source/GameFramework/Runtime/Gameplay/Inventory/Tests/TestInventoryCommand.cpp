#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/EquipmentSetCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Equipment.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryCommand.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

// InventoryCommandTest — 가방 · 장비 조작 명령의 검사와 적용: 옮기기(겹치기 · 바꾸기), 나누기, 버리기, 장착 · 해제, 사용, 서버가 거절할 요청(범위 밖 · 복제 수량 · 음수), 인벤토리 화면이 드는 장비 컴포넌트 약한 참조(nogpu).

namespace
{
    /** @brief 포션(겹침 10) · 칼(무기 칸) · 돌(겹침 10, 쓰임 없음)과 가방 네 칸 · 장비(Head · MainHand)입니다. */
    struct BagFixture
    {
        sw::ItemCatalog               _items;
        sw::Inventory                 _bag;
        sw::Equipment                 _equipment;
        sw::vector<sw::InventorySlot> _listDropped;

        BagFixture()
        {
            SW_REQUIRE_TRUE( _items.loadFromXMLText( R"(<ItemCatalog>
  <Item id="potion" category="Consumable" maxStack="10" weight="0.1" useEffect="Heal"/>
  <Item id="sword" category="Weapon" slot="MainHand" maxStack="1" weight="3"><Stats attack="12"/></Item>
  <Item id="stone" category="Material" maxStack="10" weight="1"/>
</ItemCatalog>)",
                                                     "InventoryCommandTest" ) );
            SW_REQUIRE_TRUE( _bag.initialize( &_items, 4 ) );
            _equipment.initialize( &_items, "Head,MainHand", nullptr, nullptr );
        }

        sw::InventoryCommandResult run( sw::InventoryCommandKind kind, int32 from, int32 to = sw::InventoryCommand::kNoSlot, int32 count = 0, const utf8* pEquipSlot = "" )
        {
            sw::InventoryCommand command{};
            command._kind      = kind;
            command._fromSlot  = from;
            command._toSlot    = to;
            command._count     = count;
            command._equipSlot = sw::hashed_string( pEquipSlot );
            return command.apply( _bag, _equipment, _listDropped );
        }
    };

    const sw::hashed_string kPotion{ "potion" };
    const sw::hashed_string kSword{ "sword" };
} // namespace

/** @brief [InventoryCommandTest] 옮기기는 같은 아이템이면 겹치고 다르면 바꾼다. 나누기는 일부만 떼어 빈 칸에 둔다 */
SW_TEST_CASE( InventoryCommandTest, MoveStacksOrSwapsAndSplitTakesPart )
{
    BagFixture fixture;
    SW_REQUIRE_EQUAL( 6, fixture._bag.addItem( kPotion, 6 ) );
    SW_REQUIRE_EQUAL( 1, fixture._bag.addItem( kSword, 1 ) );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Split, 0, sw::InventoryCommand::kNoSlot, 2 ) == sw::InventoryCommandResult::Ok );
    SW_EXPECT_EQUAL( 4, fixture._bag.getSlot( 0 )._count );
    SW_EXPECT_EQUAL( 2, fixture._bag.getSlot( 2 )._count );                                                  // 첫 빈 칸
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Move, 2, 0 ) == sw::InventoryCommandResult::Ok ); // 같은 아이템 — 겹친다
    SW_EXPECT_EQUAL( 6, fixture._bag.getSlot( 0 )._count );
    SW_EXPECT_TRUE( fixture._bag.getSlot( 2 ).isEmpty() );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Move, 1, 0 ) == sw::InventoryCommandResult::Ok ); // 다른 아이템 — 바꾼다
    SW_EXPECT_TRUE( fixture._bag.getSlot( 0 )._itemID == kSword );
    SW_EXPECT_TRUE( fixture._bag.getSlot( 1 )._itemID == kPotion );
}

/** @brief [InventoryCommandTest] 서버가 거절할 요청 — 범위 밖 칸 · 같은 칸 · 빈 칸 · 0 이하 수량 · 가진 것보다 많은 수량(복제) · 전부 나누기는 아무것도 바꾸지 않는다 */
SW_TEST_CASE( InventoryCommandTest, RejectsForgedRequests )
{
    BagFixture fixture;
    SW_REQUIRE_EQUAL( 3, fixture._bag.addItem( kPotion, 3 ) );
    const uint32 revision = fixture._bag.getRevision();
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Move, 0, 9 ) == sw::InventoryCommandResult::InvalidSlot );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Move, -1, 1 ) == sw::InventoryCommandResult::InvalidSlot );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Move, 0, 0 ) == sw::InventoryCommandResult::InvalidSlot );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Move, 2, 0 ) == sw::InventoryCommandResult::EmptySlot );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Split, 0, sw::InventoryCommand::kNoSlot, -2 ) == sw::InventoryCommandResult::InvalidCount );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Split, 0, sw::InventoryCommand::kNoSlot, 3 ) == sw::InventoryCommandResult::InvalidCount );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Drop, 0, sw::InventoryCommand::kNoSlot, 4 ) == sw::InventoryCommandResult::InvalidCount );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Drop, 0, sw::InventoryCommand::kNoSlot, 0 ) == sw::InventoryCommandResult::InvalidCount );
    SW_EXPECT_EQUAL( revision, fixture._bag.getRevision() );
    SW_EXPECT_EQUAL( 3, fixture._bag.getItemCount( kPotion ) );
    SW_EXPECT_TRUE( fixture._listDropped.empty() );
}

/** @brief [InventoryCommandTest] 버리기는 꺼낸 조각을 넘기고, 장착 · 해제는 장비 칸과 가방을 오가며 능력치가 따라온다. 쓰임이 없는 것은 쓰지 못한다 */
SW_TEST_CASE( InventoryCommandTest, DropEquipUnequipAndUse )
{
    BagFixture fixture;
    SW_REQUIRE_EQUAL( 5, fixture._bag.addItem( kPotion, 5 ) );
    SW_REQUIRE_EQUAL( 1, fixture._bag.addItem( kSword, 1 ) );
    SW_REQUIRE_EQUAL( 1, fixture._bag.addItem( sw::hashed_string( "stone" ), 1 ) );

    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Drop, 0, sw::InventoryCommand::kNoSlot, 2 ) == sw::InventoryCommandResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), fixture._listDropped.size() );
    SW_EXPECT_EQUAL( 2, fixture._listDropped[0]._count );
    SW_EXPECT_EQUAL( 3, fixture._bag.getItemCount( kPotion ) );

    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Equip, 0, sw::InventoryCommand::kNoSlot, 0, "MainHand" ) == sw::InventoryCommandResult::Refused ); // 포션은 무기 칸에 못 낀다
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Equip, 1 ) == sw::InventoryCommandResult::Ok );                                                    // 받는 칸을 고른다
    const sw::InventorySlot* pWorn = fixture._equipment.findEquipped( sw::hashed_string( "MainHand" ) );
    SW_REQUIRE_TRUE( pWorn != nullptr );
    SW_EXPECT_TRUE( pWorn->_itemID == kSword );
    sw::StatBlock stats;
    fixture._equipment.computeStats( stats );
    SW_EXPECT_EQUAL( 12.0f, stats.getValue( sw::hashed_string( "attack" ) ) );
    SW_EXPECT_EQUAL( 0, fixture._bag.getItemCount( kSword ) );

    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Unequip, sw::InventoryCommand::kNoSlot, sw::InventoryCommand::kNoSlot, 0, "MainHand" ) == sw::InventoryCommandResult::Ok );
    SW_EXPECT_EQUAL( 1, fixture._bag.getItemCount( kSword ) );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Unequip, sw::InventoryCommand::kNoSlot, sw::InventoryCommand::kNoSlot, 0, "MainHand" ) == sw::InventoryCommandResult::EmptySlot );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Unequip, sw::InventoryCommand::kNoSlot, sw::InventoryCommand::kNoSlot, 0, "Tail" ) == sw::InventoryCommandResult::InvalidSlot );

    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Use, 0 ) == sw::InventoryCommandResult::Ok );
    SW_EXPECT_EQUAL( 2, fixture._bag.getItemCount( kPotion ) );
    const sw::optional<int32> stoneSlot = fixture._bag.findFirstSlot( sw::hashed_string( "stone" ) );
    SW_REQUIRE_TRUE( stoneSlot.has_value() );
    SW_EXPECT_TRUE( fixture.run( sw::InventoryCommandKind::Use, stoneSlot.value() ) == sw::InventoryCommandResult::NotUsable );
}

/**
 * @brief [InventoryCommandTest] 저장된 차림을 놓을 때 칸 차례와 상관없이 조건이 맞고, 낀 채 숨김인 것은 숨김으로 남고, 낄 수 없는 것은 가방이 차도 잃지 않는다
 */
SW_TEST_CASE( InventoryCommandTest, RestoredWornItemsDoNotDependOnSlotOrderAndAreNeverLost )
{
    sw::ItemCatalog items;
    SW_REQUIRE_TRUE( items.loadFromXMLText( R"(<ItemCatalog>
  <Item id="helm" category="Armor" slot="Head" maxStack="1"><Requires equippedTag="InventoryCommandTest.Plate"/></Item>
  <Item id="cape" category="Armor" slot="Back" maxStack="1" breakPolicy="KeepHidden"><Requires equippedTag="InventoryCommandTest.Missing"/></Item>
  <Item id="plate" category="Armor" slot="Body" maxStack="1" tags="InventoryCommandTest.Plate"/>
</ItemCatalog>)",
                                            "InventoryCommandTest" ) );
    sw::Equipment equipment;
    equipment.initialize( &items, "Head,Back,Body", nullptr, nullptr );
    sw::Inventory bag;
    SW_REQUIRE_TRUE( bag.initialize( &items, 1 ) );
    SW_REQUIRE_EQUAL( 1, bag.addItem( sw::hashed_string( "plate" ), 1 ) ); // 가방이 찼다

    const auto makeWorn = []( const utf8* pSlot, const utf8* pItem )
    {
        sw::EquipmentSlot worn;
        worn._name         = sw::hashed_string( pSlot );
        worn._item._itemID = sw::hashed_string( pItem );
        worn._item._count  = 1;
        return worn;
    };
    SW_EXPECT_LOG( "Equipment.SavedItemNotWorn" );
    equipment.restoreWorn( { makeWorn( "Head", "helm" ), makeWorn( "Back", "cape" ), makeWorn( "Body", "plate" ), makeWorn( "Gone", "plate" ) }, bag );
    SW_EXPECT_NOT_NULL( equipment.findEquipped( sw::hashed_string( "Head" ) ) ); // 하나씩 끼면 Body 를 놓기 전이라 조건이 깨진다
    SW_EXPECT_NOT_NULL( equipment.findEquipped( sw::hashed_string( "Back" ) ) );
    SW_EXPECT_TRUE( equipment.isSuppressed( sw::hashed_string( "Back" ) ) );
    SW_EXPECT_NOT_NULL( equipment.findEquipped( sw::hashed_string( "Body" ) ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), bag.getOverflow().size() );
    SW_EXPECT_TRUE( bag.getOverflow()[0]._itemID == sw::hashed_string( "plate" ) );
}

/**
 * @brief [InventoryCommandTest] 세트 조건은 초기화에 준 세트 정보로 판정한다 — 조각을 낀 뒤에야 낄 수 있다
 */
SW_TEST_CASE( InventoryCommandTest, SetConditionUsesTheLookupGivenAtInitialize )
{
    sw::ItemCatalog items;
    SW_REQUIRE_TRUE( items.loadFromXMLText( R"(<ItemCatalog>
  <Item id="helm" category="Armor" slot="Head" maxStack="1"/>
  <Item id="cape" category="Armor" slot="Back" maxStack="1"><Requires set="Knight" pieces="1"/></Item>
</ItemCatalog>)",
                                            "InventoryCommandTest" ) );
    sw::AppearanceLoadReport report;
    sw::XMLDocument          doc;
    SW_REQUIRE_TRUE( doc.parseData( R"(<EquipmentSetCatalog><Set id="Knight"><Piece slot="Head" items="helm"/></Set></EquipmentSetCatalog>)", "InventoryCommandTest" ) );
    sw::EquipmentSetCatalog sets;
    SW_REQUIRE_TRUE( sets.loadFromXML( doc.getRoot(), report, "InventoryCommandTest" ) );
    sw::Equipment equipment;
    equipment.initialize( &items, "Head,Back", &sets, nullptr );
    SW_EXPECT_TRUE( equipment.evaluateEquip( sw::hashed_string( "Back" ), sw::hashed_string( "cape" ) ) == sw::EquipResult::ConditionNotMet );
    sw::InventorySlot helm;
    helm._itemID = sw::hashed_string( "helm" );
    helm._count  = 1;
    sw::vector<sw::InventorySlot> listRemoved;
    SW_REQUIRE_TRUE( equipment.equip( sw::hashed_string( "Head" ), helm, listRemoved ) == sw::EquipResult::Ok );
    SW_EXPECT_TRUE( equipment.canEquip( sw::hashed_string( "Back" ), sw::hashed_string( "cape" ) ) );
}

/**
 * @brief [InventoryCommandTest] 캐릭터 조건은 빌린 캐릭터 정보로 판정하고, 정보가 바뀌면 함께 벗겨진 것이 가방(차면 넘침)으로 간다
 */
SW_TEST_CASE( InventoryCommandTest, CharacterConditionFollowsTheBorrowedContext )
{
    sw::ItemCatalog items;
    SW_REQUIRE_TRUE( items.loadFromXMLText( R"(<ItemCatalog>
  <Item id="crown" category="Armor" slot="Head" maxStack="1"><Requires characterTag="InventoryCommandTest.Royal"/></Item>
  <Item id="belt" category="Armor" slot="Waist" maxStack="1" breakPolicy="KeepHidden"><Requires bodyShape="Slim,Average"/></Item>
</ItemCatalog>)",
                                            "InventoryCommandTest" ) );
    sw::EquipmentCharacterContext character;
    character._tags.addTag( sw::TagID::request( "InventoryCommandTest.Royal" ) );
    character._bodyShape = sw::hashed_string( "Slim" );
    sw::Equipment equipment;
    equipment.initialize( &items, "Head,Waist", nullptr, &character );
    sw::Inventory bag;
    SW_REQUIRE_TRUE( bag.initialize( &items, 0 ) );
    sw::InventorySlot item;
    item._count  = 1;
    item._itemID = sw::hashed_string( "crown" );
    sw::vector<sw::InventorySlot> listRemoved;
    SW_REQUIRE_TRUE( equipment.equip( sw::hashed_string( "Head" ), item, listRemoved ) == sw::EquipResult::Ok );
    item._itemID = sw::hashed_string( "belt" );
    SW_REQUIRE_TRUE( equipment.equip( sw::hashed_string( "Waist" ), item, listRemoved ) == sw::EquipResult::Ok );

    character._tags.clear();
    character._bodyShape = sw::hashed_string( "Heavy" );
    equipment.settleCharacterChange( bag );
    SW_EXPECT_NULL( equipment.findEquipped( sw::hashed_string( "Head" ) ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), bag.getOverflow().size() );
    SW_EXPECT_TRUE( bag.getOverflow()[0]._itemID == sw::hashed_string( "crown" ) );
    SW_EXPECT_TRUE( equipment.isSuppressed( sw::hashed_string( "Waist" ) ) );

    character._bodyShape = sw::hashed_string( "Average" );
    equipment.settleCharacterChange( bag );
    SW_EXPECT_FALSE( equipment.isSuppressed( sw::hashed_string( "Waist" ) ) );
}

/**
 * @brief [InventoryCommandTest] 시작 전에 받은 상태는 다시 떠도 그대로다 — 불러온 직후의 저장이 장비를 기본값으로 덮지 않는다
 */
SW_TEST_CASE( InventoryCommandTest, StateHeldBeforePlaySurvivesAnotherCapture )
{
    sw::CharacterEquipmentComponent character;
    const sw::vector<uint8>         saved{ 1, 2, 3, 4, 5 }; // 걸지 않은 바이트라 뜻은 상관없다
    character.restoreState( sw::vector<uint8>( saved ) );
    sw::vector<uint8> captured;
    sw::ComponentStateStore::writeComponent<sw::CharacterEquipmentComponent>( character, captured );
    SW_EXPECT_TRUE( captured == saved );
}

/**
 * @brief [InventoryCommandTest] 인벤토리 화면이 드는 장비 컴포넌트 약한 참조는 컴포넌트가 사라지면 빈다
 * @details 화면은 엔진 UI 가 소유해 씬 · 게임 인스턴스보다 오래 산다 — 날 포인터면 캐릭터가 지워진 뒤 다음 틱이 사라진 컴포넌트를 읽는다(상점 공급자와 같은 모양).
 */
SW_TEST_CASE( InventoryCommandTest, ScreenReferenceEmptiesWhenTheEquipmentComponentIsDestroyed )
{
    sw::WeakRef<sw::CharacterEquipmentComponent> reference;
    {
        sw::CharacterEquipmentComponent character;
        reference = sw::WeakRef<sw::CharacterEquipmentComponent>{ character };
        SW_EXPECT_TRUE( reference.get() == &character );
    }
    SW_EXPECT_NULL( reference.get() );
}
