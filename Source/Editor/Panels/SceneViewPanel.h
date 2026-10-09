/**
 * @file SceneViewPanel.h
 * @brief 에디터 카메라로 씬을 보는 Scene 창입니다 — 카메라 비행 · 궤도, 격자, 기즈모, 피킹, 눈금자, 시각화 · 디버그 선, 보기 모드, 통계, 방향 큐브.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Gui/IEditorPanel.h"
#include "Editor/Viewport/EditorViewportClient.h"

namespace sw::editor
{
    /**
     * @class SceneViewPanel
     * @brief 씬 뷰 RT(`EditorViewKind::Scene`)를 그리고 그 위에 편집 보조선 · 기즈모를 얹습니다(유니티 Scene 뷰 · 언리얼 레벨 뷰포트).
     * @details Play 중에도 에디터 카메라로 그립니다 — 게임 카메라 출력은 `GameViewPanel` 이 그립니다. 패널이 보이지 않으면 씬 뷰를 그리지 않습니다.
     */
    class SceneViewPanel : public IEditorPanel
    {
    public:
        /** @brief Scene 창을 만듭니다. */
        SceneViewPanel();

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Scene"; }
        /** @brief 뷰포트 툴바 · 씬 뷰 캔버스 · 기즈모를 그립니다. */
        void drawContent() override;

        /** @brief 뷰포트 클라이언트입니다(에디터 자체 시험이 기즈모 · 카메라 상태를 읽습니다). */
        const EditorViewportClient& getViewportClient() const { return _viewportClient; }

    private:
        /** @brief 디버그 드로우 카테고리를 켜고 끄는 팝업을 그립니다. */
        void drawDebugCategoryPopup();

    private:
        EditorViewportClient  _viewportClient;
        vector<hashed_string> _listDebugCategory; ///< 카테고리 팝업 재사용 버퍼
    };
} // namespace sw::editor
