/**
 * @file EditorViewportProjection.h
 * @brief 월드 좌표를 뷰포트 화면 좌표로 투영하고, 마우스 아래 레이와 ImGuizmo 행렬 배열을 만듭니다(뷰포트 조각들이 함께 씁니다).
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/EditorExports.h"

// ImGui 헤더를 프로젝트 헤더에서 include 하지 않는다. 화면 좌표 out 파라미터는 참조로만
// 받으므로 전방 선언으로 충분하다 (EditorViewportCanvasOverlay.h 의 ImDrawList 와 같은 방식).

struct ImVec2;

namespace sw
{
    struct float2;
    struct float3;
    struct float4x4;
} // namespace sw

namespace sw::editor
{
    struct EditorPickRay;

    /**
     * @class EditorViewportProjectionUtil
     * @brief 뷰포트 캔버스 좌표 변환입니다. 그리드 · 자 · 컴포넌트 시각화 · 피킹 · 기즈모 · 애셋 드롭이 함께 씁니다.
     */
    class SW_EDITOR_API EditorViewportProjectionUtil
    {
    public:
        /**
         * @brief 엔진 행렬을 ImGuizmo 배열(float[16])로 옮깁니다. 그대로 복사합니다.
         * @details 엔진의 float4x4 는 행 벡터 규약(이동이 _41, _42, _43)의 행 우선 저장이므로 메모리가 곧 ImGuizmo 가 받는 배열
         *          (이동이 [12], [13], [14])이다. 전치하면 이동이 [3], [7], [11] 로 가서 ImGuizmo 는 물체가 원점이나 카메라 눈 위치에 있다고 보고
         *          (클립 w = 0) 기즈모를 엉뚱한 곳에 그리며 핸들을 잡지 못한다.
         */
        static void storeGizmoMatrix( float32* pOutMatrix, const float4x4& matrix );
        /** @brief ImGuizmo 배열(float[16])을 엔진 행렬로 옮깁니다. `storeGizmoMatrix` 의 역입니다. */
        static void loadGizmoMatrix( float4x4& outMatrix, const float32* pMatrix );
        /** @brief view 와 proj 배열(ImGuizmo 형식)에서 뷰-투영 행렬을 만듭니다. */
        static void loadViewProjection( const float32* pView, const float32* pProj, float4x4& outViewProjection );

        /**
         * @brief 마우스 아래의 월드 레이를 만듭니다. 캔버스 밖이거나 퇴화했으면 false 입니다.
         * @details 피킹, 자, 애셋 드롭이 같은 레이를 쓴다. ImGui 에 의존하는 부분은 마우스 위치뿐이고, 나머지는
         *          `EditorViewportPick::makeRay` 이므로 테스트가 있다.
         */
        static bool makeMouseRay( const float4x4& invViewProj, const float2& canvasPos, const float2& canvasSize, EditorPickRay& outRay );

        /** @brief 월드 점을 캔버스 좌표로 옮깁니다. 카메라 뒤면 false 입니다. */
        static bool projectPoint( const float4x4& viewProj, const float3& worldPt, const float2& canvasPos,
                                  const float2& canvasSize, ImVec2& outScreenPt );

        /**
         * @brief 월드 선분을 화면 선분으로 옮깁니다. **근평면에서 잘라서** 냅니다.
         * @details 점 단위로 투영하면 끝점 하나가 카메라 뒤에 있는 선분을 통째로 버리게 됩니다. 그리드는 카메라를 중심으로
         *          ±kGridExtent 로 깔리므로 카메라를 가로지르는 선이 대부분이고, 그래서 격자가 한두 줄만 남았습니다. 동차
         *          좌표에서 w 가 근평면을 넘는 지점을 찾아 그 점으로 자릅니다.
         * @return 선분 전체가 카메라 뒤면 false
         */
        static bool projectSegment( const float4x4& viewProj, const float3& worldA, const float3& worldB,
                                    const float2& canvasPos, const float2& canvasSize, ImVec2& outScreenA,
                                    ImVec2& outScreenB );
    };
} // namespace sw::editor
