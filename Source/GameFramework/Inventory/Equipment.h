/**
 * @file Equipment.h
 * @brief 장비 칸 — 칸 이름과 받는 종류(반지 두 칸이 같은 "Ring" 을 받는다), 장착 조건, 끼기 · 벗기 · 인벤토리와 주고받기 · 능력치 합입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/StatBlock.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Inventory/EquipCondition.h"
#include "GameFramework/Inventory/Inventory.h"

namespace sw
{
    struct ItemDef;

    class ItemCatalog;

    /** @brief 장비 칸 하나입니다. */
    struct EquipSlot
    {
        ItemStack     _item{};
        hashed_string _name{};                  ///< "Ring1"
        hashed_string _accept{};                ///< 받는 `ItemDef::_equipSlot`("Ring")
        uint8         _bSuppressed{ SW_FALSE }; ///< 장착 조건이 깨져 낀 채 숨김(`EquipBreakPolicy::KeepHidden`) — 능력치 · 다른 조건에 세지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 끼기 · 벗기 결과입니다. */
    enum class EquipResult : uint8
    {
        Ok = 0,
        UnknownSlot,
        UnknownItem,
        WrongSlot,          ///< 그 칸이 받지 않는 아이템
        InventoryFull,      ///< 벗은 것을 둘 자리가 없다
        ConditionNotMet,    ///< 아이템의 장착 조건(`<Requires>`)이 맞지 않는다
        RefusedByDependent, ///< 이 변화가 다른 장비의 조건을 깨는데 그 장비가 벗기를 거부한다(`EquipBreakPolicy::RefuseUnequip`)
        EmptySlot           ///< 벗을 것이 없다
    };

    SW_GF_API const utf8* toString( EquipResult result );

    /**
     * @class Equipment
     * @brief 한 캐릭터의 장비입니다. 칸 구성은 장르마다 다르므로 데이터로 받습니다 — `"Head,Body,MainHand,Ring1:Ring,Ring2:Ring"`(이름:받는 종류, 생략하면 같다).
     * @details 능력치 합(`computeStats`)은 낀(숨김이 아닌) 아이템의 `ItemDef::_stats` 를 더한 것입니다. 어빌리티 시스템에는 게임이 무한 이펙트 하나로 넘깁니다.
     *
     *          장착 조건: 끼기는 "끼운 뒤의 상태" 에서 그 아이템의 조건을 판정합니다. 모든 변화(끼기 · 벗기 · 캐릭터 태그 · 체형) 뒤에는 다른 장비의 조건을
     *          다시 보고 깨진 것을 아이템의 정책대로 처리합니다 — 함께 벗기(@p outListRemoved 로 돌려준다), 낀 채 숨기기, 거부하기. 조건끼리 순환하는 데이터는
     *          로드 오류라(`EquipConditionUtil::findConditionCycle`) 이 처리는 항상 끝납니다. 세트 조건은 `setSetLookup` 으로 받은 세트 정보로 봅니다.
     */
    class SW_GF_API Equipment
    {
    public:
        Equipment();

        void initialize( const ItemCatalog* pCatalog, string_view slotLayout );
        /** @brief 세트 조건이 볼 세트 정보입니다(빌려 쓴다). 없으면 세트 조건은 맞지 않습니다. */
        void setSetLookup( const IEquipSetLookup* pSetLookup ) { _pSetLookup = pSetLookup; }
        /** @brief 캐릭터의 태그 · 체형을 바꾸고 조건을 다시 봅니다. 함께 벗겨진 것은 @p outListRemoved 로 나옵니다. */
        void setCharacterContext( const EquipCharacterContext& context, vector<ItemStack>& outListRemoved );

        /**
         * @brief @p slot 에 @p item 을 낍니다. 원래 있던 것과 조건이 깨져 함께 벗겨진 것은 @p outListRemoved 로 나옵니다(원래 것이 먼저).
         * @details 실패하면 아무것도 바뀌지 않습니다.
         */
        EquipResult equip( const hashed_string& slot, const ItemStack& item, vector<ItemStack>& outListRemoved );
        /** @brief 칸을 비웁니다. 벗은 것과 함께 벗겨진 것이 @p outListRemoved 로 나옵니다(벗은 것이 먼저). 실패하면 아무것도 바뀌지 않습니다. */
        EquipResult unequip( const hashed_string& slot, vector<ItemStack>& outListRemoved );
        /** @brief 인벤토리 칸의 아이템을 낍니다. 빈 장비 칸을 고르려면 @p slot 을 비웁니다. 벗은 것은 인벤토리로 갑니다(자리가 없으면 모두 되돌린다). */
        EquipResult equipFromInventory( Inventory& inventory, int32 inventorySlot, const hashed_string& slot = hashed_string{} );
        /** @brief 벗어서(함께 벗겨진 것까지) 인벤토리에 넣습니다. 자리가 없거나 거부되면 그대로 두고 false 입니다. */
        [[nodiscard]] bool unequipToInventory( const hashed_string& slot, Inventory& inventory );
        /** @brief 낀 아이템의 인스턴스 상태(피해 · 떨어져 나간 부품 · 꾸미기 값)를 바꿉니다. 칸이 비었으면 false 입니다. */
        [[nodiscard]] bool setEquippedInstance( const hashed_string& slot, const ItemStack& item );

        /** @brief 끼기의 결과를 바꾸지 않고 미리 봅니다 — 칸 종류 · 장착 조건 · 다른 장비의 거부까지. */
        EquipResult evaluateEquip( const hashed_string& slot, const hashed_string& itemId ) const;
        bool        canEquip( const hashed_string& slot, const hashed_string& itemId ) const { return evaluateEquip( slot, itemId ) == EquipResult::Ok; }
        /** @brief 그 아이템을 받는 칸 — 빈 칸 먼저, 없으면 첫 칸입니다. 없으면 빈 이름입니다. */
        hashed_string    findSlotFor( const hashed_string& itemId ) const;
        const ItemStack* findEquipped( const hashed_string& slot ) const;
        /** @brief 칸의 아이템이 조건이 깨져 숨김 상태인가입니다. */
        bool                         isSuppressed( const hashed_string& slot ) const;
        void                         computeStats( StatBlock& outStats ) const;
        const vector<EquipSlot>&     getSlots() const { return _listSlot; }
        const EquipCharacterContext& getCharacterContext() const { return _context; }
        const ItemCatalog*           getCatalog() const { return _pCatalog; }
        const IEquipSetLookup*       getSetLookup() const { return _pSetLookup; }
        /** @brief 칸 · 아이템 · 숨김 상태가 바뀔 때마다 오릅니다(외형 해석이 이것으로 다시 돈다). */
        uint32 getRevision() const { return _revision; }

    private:
        int32 findSlotIndex( const hashed_string& slot ) const;
        bool  areConditionsMet( const ItemDef& def, const vector<EquipSlot>& listSlot, int32 slotIndex ) const;
        /**
         * @brief @p inoutListSlot 의 깨진 조건을 정책대로 정리합니다. @p bAllowRefuse 면 거부 정책이 결과를 `RefusedByDependent` 로 만듭니다.
         * @details 숨김 해제도 여기서 합니다(조건이 다시 맞으면).
         */
        EquipResult settleConditions( vector<EquipSlot>& inoutListSlot, bool bAllowRefuse, vector<ItemStack>& outListRemoved ) const;
        /** @brief 칸 @p slotIndex 에 @p item 을 둔 시험 상태를 만들고 판정합니다. */
        EquipResult makeTrialEquip( int32 slotIndex, const ItemStack& item, vector<EquipSlot>& outListSlot, vector<ItemStack>& outListRemoved ) const;
        /** @brief 시험 상태를 들이고, 실제로 바뀌었으면 판을 올립니다. */
        void commitSlots( vector<EquipSlot>&& listSlot );

        vector<EquipSlot>      _listSlot;
        EquipCharacterContext  _context;
        const ItemCatalog*     _pCatalog;
        const IEquipSetLookup* _pSetLookup;
        uint32                 _revision;
    };
} // namespace sw
