/**
 * @file EditorColliderHandleTool.h
 * @brief 선택한 강체의 콜라이더 크기 손잡이를 씬 뷰에 그리고 끌어 고칩니다(유니티 Box Collider 편집 · 언리얼 브러시 핸들). 물리 셰이프 시각화(Phys)가 켜져 있을 때 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    struct EditorViewportVisualizerArgs;

    /**
     * @struct EditorColliderHandleTool
     * @brief 손잡이의 위치와 크기 계산은 `EditorColliderHandleMath`(ImGui 없음)이고, 이 도구는 그리기 · 마우스 · 되돌리기만 합니다.
     * @details 단일 선택된 오브젝트의 `RigidBodyComponent` 셰이프(상자 · 구 · 캡슐)마다 면 가운데에 사각 손잡이를 그린다. 손잡이를 끌면 레이와 손잡이 축이 가장 가까운 점이
     *          새 가장자리가 되어 셰이프 크기가 바뀌고(`setShapes`), 놓으면 끌기 하나가 되돌리기 하나(`Resize Collider`)로 남는다. 플레이 중에는 편집이 막혀 그리지도 않는다.
     *          손잡이 위에 마우스가 있는 동안에는 클릭 선택과 변환 기즈모가 마우스를 양보한다(`isCapturingMouse`) — 손잡이가 기즈모 축 위에 놓이기 때문이다.
     */
    struct SW_EDITOR_API EditorColliderHandleTool
    {
        /** @brief 손잡이를 그리고 끌기를 처리합니다. 물리 셰이프 시각화가 프레임마다 부릅니다. */
        static void process( const EditorViewportVisualizerArgs& args );
        /** @brief 마우스가 손잡이 위에 있거나 손잡이를 끄는 중이면 true 입니다(지난 `process` 기준). 선택 클릭과 기즈모가 보고 물러납니다. */
        static bool isCapturingMouse();
        /** @brief 끌기를 마치고 되돌리기를 남긴 횟수입니다(탐침 `Editor.ColliderHandleCommitCount`). */
        static uint32 getCommitCount();
        /** @brief 이번 프레임에 그린 손잡이 수입니다(탐침 `Editor.ColliderHandleCount`). */
        static uint32 getDrawnHandleCount();
    };
} // namespace sw::editor
