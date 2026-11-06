/**
 * @file Ballistics.h
 * @brief 겨누기 계산입니다. 맞히기 위한 발사 각(포물선 각), 움직이는 목표의 앞을 겨누는 계산, 거리별 낙차를 둡니다.
 * @details 날아가는 탄의 걸음은 단일 발사체 시뮬레이션(`ProjectileSimulation`)이 한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct Ballistics
     * @brief 겨누기 계산입니다(+Y 가 위).
     * @details 중력은 설정된 물리 중력의 크기(`PhysicsSystem::getConfiguredGravityMagnitude`) × 투사체의 중력 배율입니다.
     */
    struct SW_GF_API Ballistics
    {
        /**
         * @brief @p from 에서 @p to 를 속력 @p speed 로 맞히는 발사 방향입니다(진공 포물선). 닿지 못하면 false 입니다.
         * @param bHighArc 두 해 가운데 높은 쪽(박격포 · 수류탄)
         */
        [[nodiscard]] static bool computeLaunchDirection( const float3& from, const float3& to, float32 speed, float32 gravityScale, bool bHighArc,
                                                          float3& outDirection );
        /**
         * @brief 등속으로 움직이는 목표를 속력 @p speed 의 곧은 탄으로 맞힐 곳(앞 겨누기)입니다. 따라잡지 못하면 false 입니다.
         * @details |목표 + 속도 × t - 사수| = speed × t 를 만족하는 가장 작은 양수 t 를 쓴다.
         */
        [[nodiscard]] static bool computeInterceptPoint( const float3& shooter, const float3& target, const float3& targetVelocity, float32 speed,
                                                         float3& outPoint );
        /** @brief 수평 거리 @p distance 에서의 탄 낙차(m)입니다. 조준경 영점 표시에 씁니다. */
        static float32 computeDrop( float32 distance, float32 speed, float32 gravityScale );
    };
} // namespace sw
