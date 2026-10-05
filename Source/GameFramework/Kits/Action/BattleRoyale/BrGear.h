/**
 * @file BrGear.h
 * @brief 장비 — 헬멧 · 조끼 등급(피해 감소율 · 내구도), 가방 등급(인벤토리 무게 한도), 탄약 아이템을 무기 재장전으로 잇기입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct BrArmorDef;

    class Archive;
    class BrCatalog;
    class Inventory;
    class WeaponState;

    /** @brief 맞은 부위입니다. 머리는 헬멧, 몸은 조끼가 막고 팔다리는 막지 않습니다. */
    enum class BrHitZone : uint8
    {
        Body = 0,
        Head,
        Limb
    };

    /** @brief 입은 방어구 한 칸입니다. */
    struct BrArmorSlot
    {
        const BrArmorDef* _pDef{ nullptr };
        float32           _durability{ 0.0f };

        bool isEmpty() const { return _pDef == nullptr; }
    };
} // namespace sw

namespace sw
{
    /** @brief 방어구가 받은 한 방의 결과입니다. */
    struct BrArmorResult
    {
        float32 _damage{ 0.0f };   ///< 몸에 들어가는 피해
        float32 _absorbed{ 0.0f }; ///< 방어구가 줄인 양
        uint8   _bBroken{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class BrLoadout
     * @brief 한 사람의 헬멧 · 조끼 · 가방입니다. 카탈로그는 빌려 씁니다.
     * @details 방어구는 맞은 피해(줄이기 전)만큼 내구도가 닳고, 0 이 되는 한 방까지는 막은 뒤 부서져 사라집니다. 가방은 인벤토리 무게 한도를
     *          `기본 + 가방 용량` 으로 바꾸며, 지금 든 무게가 새 한도를 넘으면 바꾸지 않습니다(작은 가방으로 갈아입어 물건이 사라지지 않게).
     */
    class SW_GF_API BrLoadout
    {
    public:
        BrLoadout();

        void initialize( const BrCatalog* pCatalog );
        /** @brief 방어구를 입습니다(같은 칸의 것은 벗겨진다 — 게임이 바닥에 떨어뜨린다). @p durability 음수면 새것입니다. 모르는 id 면 false 입니다. */
        [[nodiscard]] bool tryEquipArmor( const hashed_string& itemId, float32 durability = -1.0f );
        /** @brief 가방을 멥니다(빈 id 는 가방 벗기). 모르는 가방 · 무게가 넘치면 false 이고 그대로입니다. */
        [[nodiscard]] bool tryEquipBackpack( const hashed_string& itemId, Inventory& inoutInventory );
        /** @brief 한 방을 방어구로 거릅니다. */
        BrArmorResult absorbDamage( float32 damage, BrHitZone hitZone );
        /**
         * @brief 인벤토리의 탄약 아이템(`WeaponDef::_ammoId`)을 탄창을 채울 만큼 예비탄으로 옮기고 재장전을 시작합니다.
         * @return 재장전을 시작했으면 true. 탄약 아이템이 없거나 탄창이 가득이거나 재장전 중이면 false 이고 아무것도 옮기지 않습니다.
         */
        [[nodiscard]] bool tryReload( WeaponState& inoutWeapon, Inventory& inoutInventory ) const;

        const BrArmorSlot&   getHelmet() const { return _helmet; }
        const BrArmorSlot&   getVest() const { return _vest; }
        const hashed_string& getBackpackId() const { return _backpackId; }
        /** @brief 지금 무게 한도입니다(기본 + 가방). */
        float32 computeCarryLimit() const;
        float32 computeCarryLimit( const hashed_string& backpackId ) const;

        /** @brief 헬멧 · 조끼(방어구 id · 내구도) · 가방 id 를 씁니다. 방어구 정의는 카탈로그의 것이라 id 만 싣습니다(무게 한도는 인벤토리가 든다). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다 — 방어구 · 가방은 `initialize` 의 카탈로그에서 id 로 찾습니다. 없는 id · 칸이 다른 방어구거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        const BrCatalog* _pCatalog;
        BrArmorSlot      _helmet;
        BrArmorSlot      _vest;
        hashed_string    _backpackId;
    };
} // namespace sw
