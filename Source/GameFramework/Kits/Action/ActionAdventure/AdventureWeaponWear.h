/**
 * @file AdventureWeaponWear.h
 * @brief 야생의 숨결의 무기 내구도 — 칠 때마다 인벤토리 칸의 내구도가 닳고, 부서지는 마지막 한 방은 피해가 커지며(치명), 얼마 남지 않으면 경고합니다.
 * @details 내구도 · 부서짐 자체는 기반 `Inventory::wearSlot` 입니다(0 이 되면 칸이 빈다). 내구도가 없는 아이템(`_maxDurability` 0)은 닳지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Inventory;

    /** @brief 내구도 규칙입니다. */
    struct AdventureWeaponWearSettings
    {
        float32 _wearPerHit{ 1.0f };
        float32 _wearPerThrow{ 2.0f };    ///< 던지기는 더 닳는다
        float32 _breakMultiplier{ 2.0f }; ///< 부서지는 한 방의 피해 배율
        float32 _warningRatio{ 0.25f };   ///< 남은 내구도가 최대의 이 비율 아래면 경고(깜빡이는 무기 아이콘)
    };
} // namespace sw

namespace sw
{
    /** @brief 한 번 친 결과입니다. */
    struct AdventureStrikeResult
    {
        hashed_string _itemId{};
        float32       _damage{ 0.0f };
        float32       _durabilityLeft{ 0.0f };
        uint8         _bBroke{ SW_FALSE };
        uint8         _bWarning{ SW_FALSE }; ///< 이번에 경고 구간에 들어섰다(한 번만)
    };
} // namespace sw

namespace sw
{
    /**
     * @class AdventureWeaponWear
     * @brief 규칙만 듭니다(상태 없음). 무기는 인벤토리 칸 하나입니다.
     */
    class SW_GF_API AdventureWeaponWear
    {
    public:
        AdventureWeaponWear();

        void setSettings( const AdventureWeaponWearSettings& settings ) { _settings = settings; }

        /**
         * @brief @p slot 의 무기로 칩니다. 빈 칸이면 피해 0 입니다.
         * @param bThrown 던지기 — `_wearPerThrow` 만큼 닳는다
         */
        AdventureStrikeResult strike( Inventory& inventory, int32 slot, float32 baseDamage, bool bThrown = false ) const;

        const AdventureWeaponWearSettings& getSettings() const { return _settings; }

    private:
        AdventureWeaponWearSettings _settings;
    };
} // namespace sw
