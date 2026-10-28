/**
 * @file EditorProjectedLineRenderer.h
 * @brief 물리 · 내비메시의 디버그 선(`IPhysicsDebugRenderer`)을 뷰포트 캔버스에 바로 그리는 출구입니다. 엔진은 그리는 쪽을 모르므로 에디터가 선을 받아 투영합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Physics/PhysicsDebugDraw.h"

struct ImDrawList;

namespace sw
{
    struct float4x4;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorProjectedLineRenderer
     * @brief 월드 선분을 근평면에서 잘라 캔버스 좌표로 옮겨 `ImDrawList` 에 긋습니다. 그린 선분 수를 셉니다(시각화 통계 · 시나리오 감시 값).
     * @details 선택 외곽선 · 디버그 드로우 시각화와 같은 투영(`EditorViewportProjectionUtil::projectSegment`)을 씁니다. 프레임마다 스택에 만들어 쓰는 값이라 할당이 없습니다.
     */
    class SW_EDITOR_API EditorProjectedLineRenderer final : public IPhysicsDebugRenderer
    {
    public:
        /** @brief 선을 그릴 목록과 카메라를 받습니다. 모두 이 객체보다 오래 삽니다(프레임 안에서만 쓴다). */
        EditorProjectedLineRenderer( ImDrawList* pDrawList, const float4x4& viewProjection, const float2& canvasPosition, const float2& canvasSize );

        void drawLine( const float3& from, const float3& to, const float4& color ) override;

        /** @brief 화면에 그린 선분 수입니다(전부 카메라 뒤인 선분은 세지 않는다). */
        uint32 getDrawnSegmentCount() const { return _drawnSegmentCount; }

    private:
        ImDrawList*     _pDrawList;
        const float4x4& _viewProjection;
        float2          _canvasPosition;
        float2          _canvasSize;
        uint32          _drawnSegmentCount;
    };
} // namespace sw::editor
