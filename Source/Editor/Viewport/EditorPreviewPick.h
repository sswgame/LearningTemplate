/**
 * @file EditorPreviewPick.h
 * @brief 미리보기 창(`EditorPreviewPanel`)의 뷰포트 클릭을 창 선택에 놓습니다. ImGui 를 쓰지 않아 `Test/EditorTest` 가 실제 씬으로 검증합니다.
 */
#pragma once
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorSelection.h"

namespace sw::editor
{
    struct EditorPickRay;

    /**
     * @struct EditorPreviewPickUtil
     * @brief 미리보기 씬의 레이 피킹을 창 선택으로 잇습니다.
     * @details 메인 뷰포트의 피킹(`EditorViewportPicker`)은 메인 선택에 놓는다. 미리보기 창의 클릭이 그 길을 쓰면 활성 씬을 집어 메인 선택을 고치므로,
     *          창은 이것으로 자기 씬 · 자기 선택에만 놓는다(언리얼 애셋 에디터 뷰포트는 툴킷의 선택에 놓는다).
     */
    struct SW_EDITOR_API EditorPreviewPickUtil
    {
        /**
         * @brief 창 선택 @p selection 의 씬에서 @p ray 가 처음 맞는 오브젝트를 그 선택에 @p mode 로 놓습니다. 맞은 것이 없고 @p mode 가 Replace 면 선택을 비웁니다.
         * @return 오브젝트를 맞혔으면 true 입니다. 미리보기 씬에 묶이지 않은 선택(메인)이면 아무것도 하지 않고 false 입니다.
         */
        static bool pickIntoSelection( EditorSelection& selection, const EditorPickRay& ray, SelectionMode mode );
    };
} // namespace sw::editor
