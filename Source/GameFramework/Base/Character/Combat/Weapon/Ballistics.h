/**
 * @file Ballistics.h
 * @brief 탄도 계산입니다. 중력과 공기 저항을 받는 투사체의 한 스텝, 맞히기 위한 발사 각(포물선 각), 움직이는 목표의 앞을 겨누는 계산을 둡니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 날아가는 탄, 화살, 수류탄 하나입니다(+Y 가 위). */
    struct Projectile
    {
        float3  _position{};
        float3  _previousPosition{}; ///< 지난 스텝의 위치. 이 선분으로 명중을 판정해 빠른 탄이 얇은 벽을 건너뛰지 않게 한다
        float3  _velocity{};
        float32 _gravityScale{ 1.0f };
        float32 _drag{ 0.0f }; ///< 초당 속도 감소 비율(0 = 없음)
        float32 _age{ 0.0f };
        float32 _maxAge{ 5.0f };

        bool isExpired() const { return _age >= _maxAge; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct Ballistics
     * @brief 투사체 계산입니다. 맞음 판정은 게임이 `_previousPosition` → `_position` 선분으로 합니다(`RayMath` · 물리 질의).
     * @details 중력은 설정된 물리 중력의 크기(`PhysicsSystem::getConfiguredGravityMagnitude`) × 투사체의 중력 배율입니다.
     * @details 중력은 설정된 물리 중력의 크기(`PhysicsSystem::getConfiguredGravityMagnitude`) × 투사체의 중력 배율입니다.
     */
    struct SW_GF_API Ballistics
    {

        /** @brief @p origin 에서 @p direction(단위)으로 @p speed 로 쏜 투사체입니다. */
        static Projectile launch( const float3& origin, const float3& direction, float32 speed, float32 gravityScale, float32 maxAge );
        /** @brief 한 스텝(반암시 오일러)을 진행합니다. 저항, 중력, 위치 순서로 적용합니다. */
        static void step( Projectile& projectile, float32 deltaTime );
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
