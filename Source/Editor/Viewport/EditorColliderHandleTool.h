/**
 * @file EditorColliderHandleTool.h
 * @brief 선택한 강체의 콜라이더 크기 손잡이 View 입니다(유니티 Box Collider 편집 · 언리얼 브러시 핸들). 물리 셰이프 시각화(Phys)가 켜져 있을 때 씁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    struct EditorViewportVisualizerArgs;

    /**
     * @struct EditorColliderHandleTool
     * @brief 손잡이 값(`EditorColliderHandleEdit::gather`)을 그리고 마우스를 명령으로 냅니다 — 적용과 되돌리기는 `EditorColliderHandleEdit`(Model), 명령 순서는 `EditorColliderHandleDragTracker`.
     * @details 단일 선택된 오브젝트의 `RigidBodyComponent` 셰이프(상자 · 구 · 캡슐)마다 면 가운데에 사각 손잡이를 그린다. 손잡이를 끌면 레이와 손잡이 축이 가장 가까운 점이
     *          새 가장자리가 되어 셰이프 크기가 바뀌고, 놓으면 끌기 하나가 되돌리기 하나(`Resize Collider`)로 남는다. 플레이 중에는 편집이 막혀 그리지도 않는다.
     *          손잡이 위에 마우스가 있는 동안에는 클릭 선택과 변환 기즈모가 마우스를 양보한다(`isCapturingMouse`) — 손잡이가 기즈모 축 위에 놓이기 때문이다.
     */
    struct SW_EDITOR_API EditorColliderHandleTool
    {
        /** @brief 손잡이를 그리고 끌기를 처리합니다. 물리 셰이프 시각화가 프레임마다 부릅니다. */
        static void process( const EditorViewportVisualizerArgs& args );
        /** @brief 마우스가 손잡이 위에 있거나 손잡이를 끄는 중이면 true 입니다(지난 `process` 기준). 선택 클릭과 기즈모가 보고 물러납니다. */
        static bool isCapturingMouse();
    };
} // namespace sw::editor
