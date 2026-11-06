/**
 * @file DamageMath.h
 * @brief 방어(고정, 비율, 최소)를 적용합니다. 한 발의 피해(거리 감쇠 · 머리 배율)는 장전한 조합이 계산합니다(`ResolvedShot::computeDamage`).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct DamageMath
     * @brief 장르가 달라도 같은 모양인 피해 공식을 한 곳에 둡니다. 결과는 숫자뿐이라 어빌리티 시스템의 피해 이펙트(SetByCaller)에 그대로 넘깁니다.
     */
    struct SW_GF_API DamageMath
    {
        /**
         * @brief 방어를 적용합니다. (피해 - 고정 방어) × (1 - 비율 감소)이고, 최소 피해 이상입니다.
         * @param percentReduction 0..1 (조끼 · 헬멧 등급).
         */
        static float32 applyArmor( float32 damage, float32 flatArmor, float32 percentReduction, float32 minimumDamage );
    };
} // namespace sw
