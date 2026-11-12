/**
 * @file Ray.h
 * @brief 광선(시작점 + 단위 방향)입니다. 게임 질의 · 월드 UI 피킹 · 에디터 피킹이 함께 쓰는 수학 타입입니다.
 * @details 게임프레임워크 · 월드 UI · 에디터가 각자 같은 모양의 광선 타입을 두던 것을 하나로 모았다(유니티 `Ray`, 언리얼 `FRay` 처럼 수학 층에 하나).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    /** @brief 광선과 삼각형이 만난 자리입니다(`Ray::intersectTriangle`). */
    struct RayTriangleHit
    {
        float32 _distance{ 0.0f };       ///< 광선 시작점에서의 거리
        float32 _baryU{ 0.0f };          ///< 둘째 꼭짓점의 무게
        float32 _baryV{ 0.0f };          ///< 셋째 꼭짓점의 무게(첫째는 1 - u - v)
        uint8   _bFrontFace{ SW_FALSE }; ///< 감김이 광선을 향하면(앞면) 1
    };

    /**
     * @brief 광선 하나입니다. 방향은 정규화되어 있어야 합니다.
     * @details 광선과 기본 도형의 판정(히트스캔 · 클릭 고르기 · 시선)도 이 형식의 static 멤버다 — 옛 `RayMath`(GameFramework)를 합쳤다(언리얼 FRay · FMath::LineSphereIntersection 자리).
     *          좌표계는 엔진과 같습니다(+Y 위, +Z 앞, 왼손).
     */
    struct SW_API Ray
    {
        float3 _origin{};
        float3 _direction{ 0.0f, 0.0f, 1.0f };

        /**
         * @brief 광선이 구와 만나는 가장 가까운 앞쪽 거리를 구합니다. 시작점이 구 안이면 0 이고, 안 만나면 false 입니다.
         * @param maxDistance 이 거리보다 먼 만남은 놓친 것으로 봅니다.
         */
        [[nodiscard]] static bool intersectSphere( const Ray& ray, const float3& center, float32 radius, float32 maxDistance, float32& outDistance );
        /** @brief 광선이 축 정렬 상자와 만나는 가장 가까운 앞쪽 거리를 구합니다. 슬랩 판정을 쓰며, 시작점이 안이면 0 입니다. */
        [[nodiscard]] static bool intersectAABB( const Ray& ray, const float3& boxMin, const float3& boxMax, float32 maxDistance, float32& outDistance );
        /**
         * @brief 광선이 캡슐과 만나는 가장 가까운 앞쪽 거리를 구합니다. 캡슐은 선분 @p segmentStart 에서 @p segmentEnd 까지의 둘레 반지름 @p radius 이며, 사람 크기 히트박스에 씁니다.
         * @details 옆면(원기둥)과 양 끝 반구를 함께 본다. 시작점이 캡슐 안이면 0 이다. 선분 길이가 0 이면 구와 같다.
         */
        [[nodiscard]] static bool intersectCapsule( const Ray& ray, const float3& segmentStart, const float3& segmentEnd, float32 radius, float32 maxDistance,
                                                    float32& outDistance );
        /**
         * @brief 광선이 삼각형(양면)과 만나는 앞쪽 거리 · 무게중심 좌표 · 앞면 여부를 구합니다. 에디터 클릭 고르기 · 캐릭터 표면 · 메시 변형기가 함께 쓰는 하나뿐인 판정입니다.
         * @details Möller-Trumbore 다. 두 삼각형이 나누는 모서리를 정확히 지나는 광선이 부동소수 오차로 양쪽 다 빗나가지 않게 무게중심 경계에 작은 여유를 두고,
         *          여유 안의 값은 경계로 묶는다(같은 각도로 나뉜 원기둥 두 겹에서 실제로 났다). 방향이 단위가 아니면 거리는 방향 길이 단위다.
         *          셰이더 짝은 mesh_modifier.hlsli 의 `MeshDeform::intersectRayTriangle` 이다.
         */
        [[nodiscard]] static bool intersectTriangle( const Ray& ray, const float3& a, const float3& b, const float3& c, float32 maxDistance, RayTriangleHit& outHit );
        /** @brief 광선이 평면(y 가 @p height)과 만나는 앞쪽 거리를 구합니다. 바닥 클릭과 탄착에 씁니다. */
        [[nodiscard]] static bool intersectHorizontalPlane( const Ray& ray, float32 height, float32 maxDistance, float32& outDistance );
    };
} // namespace sw
