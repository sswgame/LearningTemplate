/**
 * @file BattleRoyaleGear.h
 * @brief 장비를 다룹니다. 헬멧과 조끼 등급(피해 감소율, 내구도), 가방 등급(인벤토리 무게 한도)을 관리하고, 탄약 아이템을 무기 재장전에 연결합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct BattleRoyaleArmorDef;

    class Archive;
    class BattleRoyaleCatalog;
    class Inventory;
    class WeaponState;

    /** @brief 맞은 부위입니다. 머리는 헬멧이, 몸은 조끼가 막고 팔다리는 막지 않습니다. */
    enum class BattleRoyaleHitZone : uint8
    {
        Body = 0,
        Head,
        Limb
    };

    /** @brief 입은 방어구 슬롯 하나입니다. */
    struct BattleRoyaleArmorSlot
    {
        const BattleRoyaleArmorDef* _pDef{ nullptr };
        float32                     _durability{ 0.0f };

        bool isEmpty() const { return _pDef == nullptr; }
    };
} // namespace sw

namespace sw
{
    /** @brief 방어구가 받은 한 방의 결과입니다. */
    struct BattleRoyaleArmorResult
    {
        float32 _damage{ 0.0f };   ///< 몸에 들어가는 피해
        float32 _absorbed{ 0.0f }; ///< 방어구가 줄인 양
        uint8   _bBroken{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class BattleRoyaleLoadout
     * @brief 한 사람의 헬멧, 조끼, 가방입니다. 카탈로그는 빌려 씁니다.
     * @details 방어구는 맞은 피해(줄이기 전 값)만큼 내구도가 닳고, 0이 되는 한 방까지는 막은 뒤 부서져 사라진다. 가방은 인벤토리 무게 한도를 `기본 + 가방 용량` 으로 바꾼다.
     *          지금 든 무게가 새 한도를 넘으면 가방을 바꾸지 않는다(작은 가방으로 바꿔서 물건이 사라지는 일을 막는다).
     */
    class SW_KIT_API BattleRoyaleLoadout
    {
    public:
        BattleRoyaleLoadout();

        void initialize( const BattleRoyaleCatalog* pCatalog );
        /** @brief 방어구를 입습니다. 같은 슬롯의 방어구는 벗겨지며, 게임이 바닥에 떨어뜨립니다. @p durability 가 음수면 새것입니다. 모르는 id 면 false 를 반환합니다. */
        [[nodiscard]] bool tryEquipArmor( const hashed_string& itemID, float32 durability = -1.0f );
        /** @brief 가방을 멥니다(빈 id 는 가방을 벗는 것입니다). 모르는 가방이거나 무게가 넘치면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool tryEquipBackpack( const hashed_string& itemID, Inventory& inoutInventory );
        /** @brief 한 방을 방어구로 걸러 냅니다. */
        BattleRoyaleArmorResult absorbDamage( float32 damage, BattleRoyaleHitZone hitZone );
        /**
         * @brief 인벤토리의 탄약 아이템(`WeaponDef::_ammoID`)을 탄창을 채울 만큼 예비탄으로 옮기고 재장전을 시작합니다.
         * @return 재장전을 시작했으면 true 입니다. 탄약 아이템이 없거나 탄창이 가득 찼거나 재장전 중이면 아무것도 옮기지 않고 false 를 반환합니다.
         */
        [[nodiscard]] bool tryReload( WeaponState& inoutWeapon, Inventory& inoutInventory ) const;

        const BattleRoyaleArmorSlot& getHelmet() const { return _helmet; }
        const BattleRoyaleArmorSlot& getVest() const { return _vest; }
        const hashed_string&         getBackpackID() const { return _backpackID; }
        /** @brief 지금 무게 한도입니다(기본 한도에 가방 용량을 더한 값). */
        float32 computeCarryLimit() const;
        float32 computeCarryLimit( const hashed_string& backpackID ) const;

        /** @brief 헬멧, 조끼(방어구 id 와 내구도), 가방 id 를 씁니다. 방어구 정의는 카탈로그의 것이라 id 만 싣습니다(무게 한도는 인벤토리가 가집니다). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 방어구와 가방은 `initialize` 의 카탈로그에서 id 로 찾습니다. 없는 id 이거나 슬롯이 다른 방어구이거나 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        const BattleRoyaleCatalog* _pCatalog;
        BattleRoyaleArmorSlot      _helmet;
        BattleRoyaleArmorSlot      _vest;
        hashed_string              _backpackID;
    };
} // namespace sw
