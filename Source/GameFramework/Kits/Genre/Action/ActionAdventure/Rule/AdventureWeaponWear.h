/**
 * @file AdventureWeaponWear.h
 * @brief 야생의 숨결의 무기 내구도 규칙입니다. 칠 때마다 인벤토리 슬롯의 내구도가 닳고, 부서지는 마지막 한 방은 피해가 커지며(치명), 내구도가 얼마 남지 않으면 경고합니다.
 * @details 내구도와 부서짐 처리 자체는 기반 `Inventory::wearSlot` 이 한다(0이 되면 슬롯이 빈다). 내구도가 없는 아이템(`_maxDurability` 가 0)은 닳지 않는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Inventory;

    /** @brief 내구도 규칙입니다. */
    struct AdventureWeaponWearSettings
    {
        float32 _wearPerHit{ 1.0f };
        float32 _wearPerThrow{ 2.0f };    ///< 던지면 더 많이 닳는다
        float32 _breakMultiplier{ 2.0f }; ///< 부서지는 한 방의 피해 배율
        float32 _warningRatio{ 0.25f };   ///< 남은 내구도가 최대값의 이 비율 아래면 경고한다(무기 아이콘이 깜빡인다)
    };
} // namespace sw

namespace sw
{
    /** @brief 한 번 친 결과입니다. */
    struct AdventureStrikeResult
    {
        hashed_string _itemID{};
        float32       _damage{ 0.0f };
        float32       _durabilityLeft{ 0.0f };
        uint8         _bBroke{ SW_FALSE };
        uint8         _bWarning{ SW_FALSE }; ///< 이번에 경고 구간에 들어섰다(한 번만 true 가 된다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class AdventureWeaponWear
     * @brief 규칙만 가지고 상태는 없습니다. 무기는 인벤토리 슬롯 하나입니다.
     */
    class SW_KIT_API AdventureWeaponWear
    {
    public:
        AdventureWeaponWear();

        void setSettings( const AdventureWeaponWearSettings& settings ) { _settings = settings; }

        /**
         * @brief @p slot 의 무기로 칩니다. 빈 슬롯이면 피해가 0입니다.
         * @param bThrown 던지기이면 `_wearPerThrow` 만큼 닳는다
         */
        AdventureStrikeResult strike( Inventory& inventory, int32 slot, float32 baseDamage, bool bThrown = false ) const;

        const AdventureWeaponWearSettings& getSettings() const { return _settings; }

    private:
        AdventureWeaponWearSettings _settings;
    };
} // namespace sw
