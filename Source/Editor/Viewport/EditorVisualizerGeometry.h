/**
 * @file EditorVisualizerGeometry.h
 * @brief 뷰포트 시각화가 그릴 월드 도형을 만듭니다(ImGui 없이 — 투영 · 그리기는 시각화 파일이 합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class BoxCollider2DComponent;
    class DebugDrawQueue;
} // namespace sw

namespace sw::editor
{
    /** @brief 월드 선분 하나와 그 색입니다. */
    struct EditorWorldSegment
    {
        float3 _from{};
        float3 _to{};
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    /**
     * @struct EditorVisualizerGeometryUtil
     * @brief 시각화가 함께 쓰는 월드 도형 계산입니다. 판정이 ImGui 밖에 있어야 `Test/EditorTest` 가 시험할 수 있습니다.
     */
    struct EditorVisualizerGeometryUtil
    {
        /** @brief 구 하나를 그리는 데 쓰는 원(XY · XZ · YZ 대원) 하나의 선분 수입니다. */
        static constexpr uint32 kSphereCircleSegmentCount = 24;
        /** @brief 크기가 0 에 가까운 콜라이더도 보이도록 그리는 최소 반 크기(월드 단위)입니다. */
        static constexpr float32 kMinColliderHalfExtent = 0.05f;

        /**
         * @brief 콜라이더를 물리가 판정하는 그 상자(`BoxCollider2DComponent::getWorldBox` — 월드 회전 · 스케일을 받은 축 정렬 상자)의 네 모서리로 냅니다.
         * @details 반 크기가 `kMinColliderHalfExtent` 보다 작은 축은 그만큼 넓혀 그립니다(점 콜라이더도 보이게). 모서리 순서는 (min,min) → (max,min) →
         *          (max,max) → (min,max) 이고 Z 는 콜라이더의 월드 Z 입니다.
         */
        static void computeColliderCorners( const BoxCollider2DComponent& collider, float3 ( &outArrCorner )[4] );

        /**
         * @brief 디버그 큐의 선 · 구를 월드 선분으로 이어 붙입니다(구는 대원 셋, 각 `kSphereCircleSegmentCount` 선분).
         * @details @p outListSegment 를 비우지 않고 뒤에 붙입니다 — 부르는 쪽이 프레임마다 재사용 버퍼를 비웁니다.
         */
        static void appendDebugDrawSegments( const DebugDrawQueue& queue, vector<EditorWorldSegment>& outListSegment );
    };
} // namespace sw::editor
