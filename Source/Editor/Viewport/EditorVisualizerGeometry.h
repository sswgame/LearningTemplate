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
    struct float4x4;

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
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief `debug_draw` 시각화가 마지막으로 그린 양입니다. 에디터 자체 시험(`sceneView.debugDraw`)이 "그려졌다" 를 확인할 때 읽습니다.
     * @details 시각화는 그리기 함수 하나라 자기 상태를 둘 곳이 없어 이 값 하나만 둡니다. 그린 프레임 번호(ImGui 프레임)를 같이 적습니다.
     */
    struct EditorDebugDrawStats
    {
        uint32 _segmentCount{ 0 };
        uint32 _textCount{ 0 };
        int32  _frame{ -1 };

        /** @brief 에디터 모듈 하나의 값입니다. */
        static EditorDebugDrawStats& get();
    };
} // namespace sw::editor

namespace sw::editor
{
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
         * @brief 디버그 큐가 확정한(`getVisible*`) 선 · 구를 월드 선분으로 이어 붙입니다. 구는 대원 셋, 각 `kSphereCircleSegmentCount` 선분입니다.
         * @param bFlat2D 2D 뷰(직교 카메라가 Z 축을 본다)면 true — 구는 XY 평면의 원 하나만 냅니다. 다른 두 대원은 그 뷰에서 중심을 지나는
         *                선으로 겹쳐 보일 뿐입니다.
         * @details @p outListSegment 를 비우지 않고 뒤에 붙입니다 — 부르는 쪽이 프레임마다 재사용 버퍼를 비웁니다.
         */
        static void appendDebugDrawSegments( const DebugDrawQueue& queue, bool bFlat2D, vector<EditorWorldSegment>& outListSegment );
        /**
         * @brief 이 뷰가 2D(직교 투영이고 시선이 월드 Z 축과 나란하다)인지 판단합니다.
         * @param bOrthographic 카메라가 직교 투영인가
         * @param view          카메라의 뷰 행렬
         */
        static bool isFlat2DView( bool bOrthographic, const float4x4& view );
    };
} // namespace sw::editor
