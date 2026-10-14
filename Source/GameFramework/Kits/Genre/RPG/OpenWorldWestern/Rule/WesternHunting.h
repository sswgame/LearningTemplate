/**
 * @file WesternHunting.h
 * @brief 사냥을 다룹니다. 가죽 등급(1~3성은 원래 품질, 사용 무기, 명중 부위, 더 쏜 발 수로 정함), 가죽 손상, 사체 부패, 손질 전리품, 상인 매입 값을 계산합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameRandom;
    class ItemStackList;
    class LootCatalog;
    class WesternCatalog;

    /** @brief 사냥한 한 마리의 사정입니다. */
    struct WesternKill
    {
        hashed_string _animalID{};
        hashed_string _weaponID{};    ///< 마지막(죽인) 무기
        hashed_string _zoneID{};      ///< 죽인 한 발이 맞은 부위
        int32         _hitCount{ 1 }; ///< 맞힌 발 수(첫 발 뒤로는 한 발마다 가죽이 상한다)
    };
} // namespace sw

namespace sw
{
    /** @brief 땅(또는 말 등)에 있는 사체 하나입니다. */
    struct WesternCarcass
    {
        hashed_string _animalID{};
        float32       _ageHours{ 0.0f };
        int32         _stars{ 0 }; ///< 잡았을 때의 등급(0이면 못 쓰는 가죽)
        uint8         _bSkinned{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 벗긴 가죽 하나입니다(썩지 않습니다). */
    struct WesternPelt
    {
        hashed_string _animalID{};
        int32         _stars{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WesternHunting
     * @brief 상태가 없는 계산입니다. 등급 = 원래 품질 − (크기에 맞지 않는 무기면 1) − 부위 벌점 − (더 맞힌 발 × 벌점)이며, 0..품질 범위로 자릅니다.
     *        폭발물은 가죽을 못 쓰게 합니다(0). 사체는 부패 시간의 반이 지나면 한 등급 떨어지고, 다 지나면 썩어서 값이 없습니다.
     */
    struct SW_KIT_API WesternHunting
    {
        /** @brief 잡은 순간의 가죽 등급(0..3)을 반환합니다. 모르는 동물이면 0 을 반환하고, 모르는 무기와 부위는 벌점이 없는 것으로 봅니다. */
        static int32 computePeltStars( const WesternCatalog& catalog, const WesternKill& kill );
        /** @brief 잡은 사체를 만듭니다. */
        static WesternCarcass makeCarcass( const WesternCatalog& catalog, const WesternKill& kill );
        /** @brief 사체를 @p gameHours 만큼 묵힙니다. */
        static void ageCarcass( WesternCarcass& inoutCarcass, float32 gameHours );
        /** @brief 썩은 정도를 반영한 사체의 지금 등급을 반환합니다. */
        static int32 computeCarcassStars( const WesternCatalog& catalog, const WesternCarcass& carcass );
        static bool  isRotten( const WesternCatalog& catalog, const WesternCarcass& carcass );
        /**
         * @brief 가죽을 벗깁니다(지금 등급 그대로). 이미 벗겼거나 썩었으면 false 를 반환합니다. 전리품 테이블이 있으면 @p pLoot 로 굴려서 @p outItems 에 더합니다.
         */
        [[nodiscard]] static bool skin( const WesternCatalog& catalog, WesternCarcass& inoutCarcass, const LootCatalog* pLoot, GameRandom& random,
                                        WesternPelt& outPelt, ItemStackList& outItems );
        /** @brief 상인이 가죽에 주는 값을 반환합니다(가죽 값 × 등급 배율, 센트 단위로 달러의 소수점을 정수로 바꾼 값). */
        static int32 computePeltPrice( const WesternCatalog& catalog, const WesternPelt& pelt );
        /** @brief 정육점이 사체 통째에 주는 값을 반환합니다(센트 단위이며, 가죽을 벗긴 사체는 절반이고 썩었으면 0). */
        static int32 computeCarcassPrice( const WesternCatalog& catalog, const WesternCarcass& carcass );
    };
} // namespace sw
