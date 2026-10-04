/**
 * @file Equipment.h
 * @brief 장비 칸 — 칸 이름과 받는 종류(반지 두 칸이 같은 "Ring" 을 받는다), 끼기 · 벗기 · 인벤토리와 주고받기 · 능력치 합입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/StatBlock.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Inventory/Inventory.h"

namespace sw
{
    class ItemCatalog;

    /** @brief 장비 칸 하나입니다. */
    struct EquipSlot
    {
        hashed_string _name{};   ///< "Ring1"
        hashed_string _accept{}; ///< 받는 `ItemDef::_equipSlot`("Ring")
        ItemStack     _item{};
    };
} // namespace sw

namespace sw
{
    /** @brief 끼기 결과입니다. */
    enum class EquipResult : uint8
    {
        Ok = 0,
        UnknownSlot,
        UnknownItem,
        WrongSlot,    ///< 그 칸이 받지 않는 아이템
        InventoryFull ///< 벗은 것을 둘 자리가 없다
    };

    SW_GF_API const utf8* toString( EquipResult result );

    /**
     * @class Equipment
     * @brief 한 캐릭터의 장비입니다. 칸 구성은 장르마다 다르므로 데이터로 받습니다 — `"Head,Body,MainHand,Ring1:Ring,Ring2:Ring"`(이름:받는 종류, 생략하면 같다).
     * @details 능력치 합(`computeStats`)은 낀 아이템의 `ItemDef::_stats` 를 더한 것입니다. 어빌리티 시스템에는 게임이 무한 이펙트 하나로 넘깁니다.
     */
    class SW_GF_API Equipment
    {
    public:
        Equipment();

        void initialize( const ItemCatalog* pCatalog, string_view slotLayout );

        /** @brief @p slot 에 @p item 을 낍니다. 원래 있던 것은 @p outPrevious 로 나옵니다(비었으면 빈 칸). */
        EquipResult equip( const hashed_string& slot, const ItemStack& item, ItemStack& outPrevious );
        /** @brief 칸을 비우고 든 것을 돌려줍니다. */
        ItemStack unequip( const hashed_string& slot );
        /** @brief 인벤토리 칸의 아이템을 낍니다. 빈 장비 칸을 고르려면 @p slot 을 비웁니다. 벗은 것은 인벤토리로 갑니다. */
        EquipResult equipFromInventory( Inventory& inventory, int32 inventorySlot, const hashed_string& slot = hashed_string{} );
        /** @brief 벗어서 인벤토리에 넣습니다. 자리가 없으면 그대로 두고 false 입니다. */
        [[nodiscard]] bool unequipToInventory( const hashed_string& slot, Inventory& inventory );

        bool canEquip( const hashed_string& slot, const hashed_string& itemId ) const;
        /** @brief 그 아이템을 받는 칸 — 빈 칸 먼저, 없으면 첫 칸입니다. 없으면 빈 이름입니다. */
        hashed_string            findSlotFor( const hashed_string& itemId ) const;
        const ItemStack*         findEquipped( const hashed_string& slot ) const;
        void                     computeStats( StatBlock& outStats ) const;
        const vector<EquipSlot>& getSlots() const { return _listSlot; }
        uint32                   getRevision() const { return _revision; }

    private:
        EquipSlot* findSlot( const hashed_string& slot );

        vector<EquipSlot>  _listSlot;
        const ItemCatalog* _pCatalog;
        uint32             _revision;
    };
} // namespace sw
