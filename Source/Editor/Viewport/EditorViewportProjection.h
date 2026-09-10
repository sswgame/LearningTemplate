/**
 * @file EditorViewportProjection.h
 * @brief 월드 → 뷰포트 화면 좌표 투영 (뷰포트 오버레이가 공유합니다)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

// ImGui 헤더를 프로젝트 헤더에서 include 하지 않는다. 화면 좌표 out 파라미터는 참조로만
// 받으므로 전방 선언으로 충분하다 (EditorViewportClient.h 의 ImDrawList 와 같은 방식).

struct ImVec2;

namespace sw::editor
{
    /**
     * @class EditorViewportProjectionUtil
     * @brief 뷰포트 캔버스 좌표 변환. 그리드·자·컴포넌트 시각화가 함께 씁니다.
     */
    class EditorViewportProjectionUtil
    {
    public:
        /** @brief 월드 점을 캔버스 좌표로. 카메라 뒤면 false입니다. */
        static bool projectPoint( const float4x4& viewProj, const float3& worldPt, const float2& canvasPos,
                                  const float2& canvasSize, ImVec2& outScreenPt );

        /**
         * @brief 월드 선분을 화면 선분으로 — **근평면에서 잘라서** 냅니다.
         * @details 점 단위로 투영하면 끝점 하나가 카메라 뒤에 있는 선분을 통째로 버리게 됩니다. 그리드는
         *          카메라를 중심으로 ±kGridExtent 로 깔리므로 카메라를 가로지르는 선이 대부분입니다 —
         *          그래서 격자가 한두 줄만 남았습니다. 동차 좌표에서 w 가 근평면을 넘는 지점을 찾아
         *          그 점으로 자릅니다.
         * @return 선분 전체가 카메라 뒤면 false.
         */
        static bool projectSegment( const float4x4& viewProj, const float3& worldA, const float3& worldB,
                                    const float2& canvasPos, const float2& canvasSize, ImVec2& outScreenA,
                                    ImVec2& outScreenB );
    };
} // namespace sw::editor
