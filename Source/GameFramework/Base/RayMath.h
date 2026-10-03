/**
 * @file RayMath.h
 * @brief 광선과 기본 도형의 판정 · 요 · 피치 방향 — 히트스캔(슈터) · 클릭 고르기 · 시선 판정이 함께 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 광선 하나 — 시작점과 단위 방향입니다. */
    struct GameRay
    {
        float3 _origin{};
        float3 _direction{ 0.0f, 0.0f, 1.0f };
    };

    /**
     * @struct RayMath
     * @brief 좌표계는 엔진과 같습니다(+Y 위, +Z 앞, 왼손). 방향은 단위 벡터여야 합니다.
     */
    struct SW_GF_API RayMath
    {
        /**
         * @brief 광선이 구와 만나는 가장 가까운 앞쪽 거리입니다. 시작점이 구 안이면 0 입니다. 안 만나면 false 입니다.
         * @param maxDistance 이 거리보다 먼 만남은 놓친 것으로 봅니다.
         */
        [[nodiscard]] static bool intersectSphere( const GameRay& ray, const float3& center, float32 radius, float32 maxDistance, float32& outDistance );
        /** @brief 광선이 축 정렬 상자와 만나는 가장 가까운 앞쪽 거리입니다(슬랩 판정). 시작점이 안이면 0 입니다. */
        [[nodiscard]] static bool intersectAabb( const GameRay& ray, const float3& boxMin, const float3& boxMax, float32 maxDistance, float32& outDistance );
        /** @brief 광선이 평면(y = @p height)과 만나는 앞쪽 거리입니다 — 바닥 클릭 · 탄착. */
        [[nodiscard]] static bool intersectHorizontalPlane( const GameRay& ray, float32 height, float32 maxDistance, float32& outDistance );
        /** @brief 요 · 피치(라디안, 피치 + 가 위)의 바라보는 단위 방향입니다. */
        static float3 computeLookDirection( float32 yaw, float32 pitch );
    };
} // namespace sw
