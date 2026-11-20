#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
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
            _equipment.initialize( &_items, "Head,MainHand" );
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
