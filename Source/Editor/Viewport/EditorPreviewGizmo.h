/**
 * @file EditorPreviewGizmo.h
 * @brief 미리보기 창(`EditorPreviewPanel`)의 뷰포트 위에 창 선택의 트랜스폼 기즈모를 놓고, 클릭 피킹을 창 선택에 놓습니다.
 */
#pragma once
#include "Editor/Common/EditorExports.h"
#include "Editor/Viewport/EditorViewportGizmo.h"
#include "Editor/Viewport/EditorViewportToolbar.h"

namespace sw::editor
{
    class EditorPreviewPanel;

    /**
     * @class EditorPreviewGizmo
     * @brief 미리보기 창이 값으로 드는 기즈모입니다. 대상은 창 선택(`EditorPreviewPanel::getPreviewSelection`)이고 조작은 그 창의 스택에 남습니다.
     * @details 창 틀(`Common/GUI`)은 뷰포트 층(`Viewport`)을 include 하지 못하므로 기즈모를 쓰는 창(`Panels/`)이 이것을 든다. 쓰는 순서:
     *          `drawPreviewViewport( size, gizmo.hasMouse() )` 로 뷰포트를 그린 바로 뒤에 `draw( *this )` 를 부른다 — 기즈모 위 · 드래그 중에는 오빗 입력을 넘기지 않는다.
     */
    class SW_EDITOR_API EditorPreviewGizmo
    {
    public:
        EditorPreviewGizmo();

        /** @brief 지난 프레임에 기즈모가 마우스를 가졌으면(위 · 드래그 중) true 입니다. 뷰포트의 오빗 입력을 막는 데 씁니다. */
        bool hasMouse() const { return _gizmo.isHovered() || _gizmo.isDragging(); }
        /**
         * @brief @p panel 의 뷰포트 이미지 위에 창 선택의 기즈모를 그리고 조작합니다. 창 선택이 묶이지 않았거나 이미지가 없으면 하지 않습니다.
         * @details 이미지를 끌지 않고 누른 클릭은 미리보기 씬을 집어 창 선택에 놓는다(Ctrl 은 토글, 빈 곳은 비움 — `EditorPreviewPickUtil`).
         */
        void draw( EditorPreviewPanel& panel );

    private:
        EditorViewportGizmo     _gizmo;
        ViewportToolbarSettings _settings; ///< 스냅 설정. 창에는 툴바가 없어 기본값(스냅 끔)이다
    };
} // namespace sw::editor
