/**
 * @file DamageMath.h
 * @brief 무기 피해 계산 — 거리 감쇠 · 머리 배율 · 방어(고정 · 비율 · 최소)입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct WeaponDef;

    /**
     * @struct DamageMath
     * @brief 장르가 달라도 같은 모양인 피해 공식을 한 곳에 둡니다. 결과는 숫자뿐이라 어빌리티 시스템의 피해 이펙트(SetByCaller)에 그대로 넘깁니다.
     */
    struct SW_GF_API DamageMath
    {
        /** @brief 거리 감쇠 배율입니다 — `_falloffStart` 까지 1, `_falloffEnd` 에서 `_falloffMinScale`, 그 사이는 선형입니다. */
        static float32 computeFalloffScale( const WeaponDef& weapon, float32 distance );
        /** @brief 한 알의 피해 — 기본 × 거리 감쇠 × (머리면 `_headshotMultiplier`)입니다. */
        static float32 computeWeaponDamage( const WeaponDef& weapon, float32 distance, bool bHeadshot );
        /**
         * @brief 방어를 적용합니다 — (피해 − 고정 방어) × (1 − 비율 감소), 그리고 최소 피해 이상입니다.
         * @param percentReduction 0..1 (조끼 · 헬멧 등급).
         */
        static float32 applyArmor( float32 damage, float32 flatArmor, float32 percentReduction, float32 minimumDamage );
    };
} // namespace sw
