/**
 * @file RayMath.h
 * @brief 광선과 기본 도형의 판정, 요와 피치 방향 계산을 정의합니다.
 * @details 히트스캔(슈터), 클릭 고르기, 시선 판정이 함께 쓴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 광선 하나입니다. 시작점과 단위 방향을 담습니다. */
    struct GameRay
    {
        float3 _origin{};
        float3 _direction{ 0.0f, 0.0f, 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RayMath
     * @brief 좌표계는 엔진과 같습니다(+Y 위, +Z 앞, 왼손). 방향은 단위 벡터여야 합니다.
     */
    struct SW_GF_API RayMath
    {
        /**
         * @brief 광선이 구와 만나는 가장 가까운 앞쪽 거리를 구합니다. 시작점이 구 안이면 0 이고, 안 만나면 false 입니다.
         * @param maxDistance 이 거리보다 먼 만남은 놓친 것으로 봅니다.
         */
        [[nodiscard]] static bool intersectSphere( const GameRay& ray, const float3& center, float32 radius, float32 maxDistance, float32& outDistance );
        /** @brief 광선이 축 정렬 상자와 만나는 가장 가까운 앞쪽 거리를 구합니다. 슬랩 판정을 쓰며, 시작점이 안이면 0 입니다. */
        [[nodiscard]] static bool intersectAABB( const GameRay& ray, const float3& boxMin, const float3& boxMax, float32 maxDistance, float32& outDistance );
        /**
         * @brief 광선이 캡슐과 만나는 가장 가까운 앞쪽 거리를 구합니다. 캡슐은 선분 @p segmentStart 에서 @p segmentEnd 까지의 둘레 반지름 @p radius 이며, 사람 크기 히트박스에 씁니다.
         * @details 옆면(원기둥)과 양 끝 반구를 함께 본다. 시작점이 캡슐 안이면 0 이다. 선분 길이가 0 이면 구와 같다.
         */
        [[nodiscard]] static bool intersectCapsule( const GameRay& ray, const float3& segmentStart, const float3& segmentEnd, float32 radius, float32 maxDistance,
                                                    float32& outDistance );
        /** @brief 광선이 평면(y 가 @p height)과 만나는 앞쪽 거리를 구합니다. 바닥 클릭과 탄착에 씁니다. */
        [[nodiscard]] static bool intersectHorizontalPlane( const GameRay& ray, float32 height, float32 maxDistance, float32& outDistance );
        /** @brief 요와 피치(라디안, 피치는 +가 위)가 가리키는 단위 방향입니다. */
        static float3 computeLookDirection( float32 yaw, float32 pitch );
        /**
         * @brief @p point 가 @p origin 에서 @p forward 쪽으로 반각 @p halfAngleDegree(도), 거리 @p range 의 원뿔 안이면 true 입니다. 손전등, 감시 카메라, 화염 방사에 씁니다.
         * @details 3D 각이다. @p forward 는 길이가 0 이 아니면 되며 단위가 아니어도 된다. 꼭짓점과 같은 위치는 안으로 친다.
         */
        static bool isInCone( const float3& origin, const float3& forward, float32 halfAngleDegree, float32 range, const float3& point );
        /** @brief `isInCone` 을 XZ 평면에서 봅니다. 높이를 무시하며 탑다운 시야와 바닥 손전등에 씁니다. */
        static bool isInFlatCone( const float3& origin, const float3& forward, float32 halfAngleDegree, float32 range, const float3& point );
    };
} // namespace sw
