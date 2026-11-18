/**
 * @file PhysicsShapeVisualizer.h
 * @brief 물리 셰이프 시각화(`physics_shapes`)의 모으기 함수입니다. 같은 토글 아래 콜라이더 손잡이 화면 도구가 있어 등록 줄은 View(`EditorColliderHandleTool.cpp`)가 둔다.
 */
#pragma once

namespace sw::editor
{
    struct EditorViewportVisualizerArgs;
    struct EditorVisualizerDrawList;

    /** @brief 강체(RigidBody · RigidBody2D)의 충돌 셰이프를 와이어프레임 도형 값으로 모읍니다(편집 중 · 플레이 중 같은 셰이프 목록). */
    struct PhysicsShapeGatherer
    {
        static void gather( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList );
    };
} // namespace sw::editor
