/**
 * @file ShooterMath.h
 * @brief 슈터의 수학 — 탄 퍼짐 원뿔입니다. 광선 판정 · 1인칭 시점 · 결정적 난수는 장르를 가리지 않아 기반(`Base/RayMath.h` ·
 *        `Base/FirstPersonLook.h` · `Base/GameRandom.h`)에 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/GameRandom.h"
#include "GameFramework/Base/RayMath.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct ShooterMath
     * @brief 좌표계는 엔진과 같습니다(+Y 위, +Z 앞, 왼손).
     */
    struct SW_GF_API ShooterMath
    {
        /**
         * @brief @p direction 주위의 원뿔(반각 @p coneHalfAngle 라디안) 안에서 고르게 고른 방향입니다. 반각이 0 이면 그대로입니다.
         * @details 원뿔 안의 입체각에 고르게 — cos θ 를 [cos 반각, 1] 에서 고르고 방위각을 [0, 2π) 에서 고른다.
         */
        static float3 applySpread( const float3& direction, float32 coneHalfAngle, GameRandom& random );
    };
} // namespace sw
