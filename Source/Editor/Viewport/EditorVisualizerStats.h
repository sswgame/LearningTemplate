/**
 * @file EditorVisualizerStats.h
 * @brief 뷰포트 시각화가 마지막으로 그린 양입니다. 뷰포트(`EditorViewportClient`)가 값으로 들고, 에디터 자체 테스트와 시나리오 프로브가 읽습니다.
 * @details 예전에는 시각화마다 모듈 지역 단일체(`static T& get()`)에 적었다. 그 모양은 뷰포트가 둘이면 서로의 값을 덮고, 뷰포트가 없어도 값이 남는다.
 *          그래서 값은 뷰포트가 들고 시각화는 그리기 인자(`EditorViewportVisualizerArgs::_pStats`)로 받아 적는다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /**
     * @brief `debug_draw` 시각화가 마지막으로 그린 양입니다. 에디터 자체 테스트(`sceneView.debugDraw`)가 그려졌는지 확인할 때 읽습니다.
     * @details 그린 프레임 번호(ImGui 프레임)를 함께 기록한다.
     */
    struct EditorDebugDrawStats
    {
        uint32 _segmentCount{ 0 };
        uint32 _textCount{ 0 };
        int32  _frame{ -1 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 물리 셰이프 · 내비메시 시각화가 지난 프레임에 그린 선분 수입니다. 시나리오 감시 값(`Editor.PhysicsShapeSegments` · `Editor.NavMeshSegments`)이 읽습니다. */
    struct EditorShapeVisualizerStats
    {
        uint32 _physicsSegmentCount{ 0 };
        uint32 _navMeshSegmentCount{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 뷰포트 하나의 시각화 통계 묶음입니다. */
    struct EditorVisualizerStats
    {
        EditorDebugDrawStats       _debugDraw{};
        EditorShapeVisualizerStats _shape{};
    };
} // namespace sw::editor
