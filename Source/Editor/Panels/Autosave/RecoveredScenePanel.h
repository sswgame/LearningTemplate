/**
 * @file RecoveredScenePanel.h
 * @brief 비정상 종료 뒤 남은 자동 저장 사본을 보이고 복원 · 폐기를 받는 창입니다(언리얼의 Restore Packages 대화상자 자리).
 * @details 줄은 `RecoveredSceneViewModel::gather` 가 모으고 명령도 그쪽이 받으며, 이 창은 그리기만 한다(MVVM 계약). 후보가 있으면 기동 첫 프레임에 자동 저장이 이 창을 연다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorPanelID.h"
#include "Editor/Panels/Autosave/Model/RecoveredSceneViewModel.h"

namespace sw::editor
{
    /** @brief 씬마다 가장 새 사본 한 줄 — 씬 · 언제 · 남은 세대 · 복원할 수 없는 이유, 그리고 Restore · Discard 입니다. */
    class RecoveredScenePanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL` — 자동 저장이 같은 상수로 연다). */
        static constexpr const utf8* kPanelID = editorpanel::kRecoveredScene.c_str();

        /** @brief 창을 만듭니다(닫힌 채 시작한다 — 후보가 있을 때 자동 저장이 연다). */
        RecoveredScenePanel();

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Recovered Work"; }
        /** @brief 후보 표를 그립니다. */
        void drawContent() override;

    private:
        RecoveredSceneViewState _viewState; ///< 프레임마다 다시 채운다
    };
} // namespace sw::editor
