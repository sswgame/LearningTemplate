/**
 * @file GameViewPanel.h
 * @brief 활성 씬의 게임 카메라 출력만 보이는 Game 창입니다 — 격자 · 기즈모 · 선택 외곽선 없이 화면 UI 까지 든 게임 화면.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Viewport/EditorViewTargetUtil.h"

namespace sw::editor
{
    /**
     * @class GameViewPanel
     * @brief 게임 뷰 RT(`EditorViewKind::Game`)를 화면 비율에 맞춰 그립니다(유니티 Game 뷰). Play 중 게임 입력은 이 패널이 포커스일 때만 게임으로 갑니다.
     * @details 게임 카메라가 없거나 꺼져 있으면 "No camera rendering" 을 띄웁니다. 패널이 보이지 않으면 게임 뷰를 그리지 않습니다(RT 요청 0).
     */
    class GameViewPanel : public IEditorPanel
    {
    public:
        /** @brief Game 창을 만듭니다. */
        GameViewPanel();

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Game"; }
        /** @brief 화면 비율 · HUD 툴바와 게임 화면을 그립니다. */
        void drawContent() override;
        /** @brief 창이 숨겨지면 게임 뷰 포커스를 해제합니다(게임 입력이 끊긴다). */
        void onPanelCollapsed() override;

        /** @brief 마지막 프레임에 "No camera rendering" 안내를 띄웠으면 true 입니다. */
        bool wasNoCameraHintShown() const { return _bNoCameraHintShown; }

    private:
        /** @brief 화면 비율 고르기와 HUD 토글을 그립니다. */
        void drawToolbar();

    private:
        EditorGameViewAspect _aspect;             ///< 이미지 화면 비율(자유 · 16:9)
        bool                 _bNoCameraHintShown; ///< 마지막 프레임에 카메라 없음 안내를 띄웠다
    };
} // namespace sw::editor
