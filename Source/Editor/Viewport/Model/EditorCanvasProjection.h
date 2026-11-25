/**
 * @file EditorCanvasProjection.h
 * @brief 월드 좌표를 뷰포트 캔버스 좌표(float2)로 옮깁니다. ImGui 를 모르는 Model 쪽 투영이고, View 의 `EditorViewportProjectionUtil` 이 이것을 감쌉니다.
 * @details 피킹 · 상자 선택 · 빌보드 모으기처럼 Model 이 화면 좌표로 판정하는 일이 같은 규칙(근평면 w 0.001, 캔버스 왼쪽 위가 원점)을 쓴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    struct AABB;
    struct float4x4;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorCanvasProjection
     * @brief 캔버스 투영 계산입니다(상태 없음).
     */
    struct SW_EDITOR_API EditorCanvasProjection
    {
        /** @brief 월드 점을 캔버스 좌표로 옮깁니다. 카메라 뒤면 false 입니다. */
        [[nodiscard]] static bool projectPoint( const float4x4& viewProj, const float3& worldPt, const float2& canvasPos, const float2& canvasSize, float2& outScreenPt );
        /**
         * @brief 월드 선분을 근평면에서 잘라 화면 선분으로 옮깁니다. 점마다 투영하면 한쪽 끝이 카메라 뒤인 선분(카메라를 가로지르는 격자선)을 통째로 버린다.
         * @return 선분 전체가 카메라 뒤면 false 입니다.
         */
        [[nodiscard]] static bool projectSegment( const float4x4& viewProj, const float3& worldA, const float3& worldB, const float2& canvasPos, const float2& canvasSize,
                                                  float2& outScreenA, float2& outScreenB );
        /**
         * @brief 월드 상자가 화면에서 차지하는 사각형입니다. 열두 모서리를 `projectSegment` 로 근평면에서 잘라 옮긴 끝점들을 감쌉니다.
         * @details 꼭짓점만 투영하면 카메라 뒤로 넘어간 꼭짓점을 버려 카메라를 품거나 걸친 큰 물체의 사각형이 작게 잡힌다(상자 선택이 놓친다 — 감사 E-10).
         * @return 상자 전체가 카메라 뒤면 false 입니다.
         */
        [[nodiscard]] static bool projectBoxRect( const float4x4& viewProj, const AABB& box, const float2& canvasPos, const float2& canvasSize, float2& outMin, float2& outMax );
        /** @brief 점 @p point 가 캔버스 사각형 안(경계 포함)이면 true 입니다. */
        [[nodiscard]] static bool isInsideCanvas( const float2& point, const float2& canvasPos, const float2& canvasSize );
    };
} // namespace sw::editor
