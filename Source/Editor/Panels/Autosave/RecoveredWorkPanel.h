/**
 * @file RecoveredWorkPanel.h
 * @brief 비정상 종료 뒤 남은 자동 저장 사본을 보이고 복원 · 폐기를 받는 창입니다(언리얼의 Restore Packages 대화상자 자리).
 * @details 후보와 명령은 `EditorAutosaveCommands`(Commands, ImGui 없음)가 들고, 이 창은 그리기만 한다. 후보가 있으면 기동 첫 프레임에 자동 저장이 이 창을 연다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"
#include "Editor/Common/GUI/Panel/IEditorPanel.h"

namespace sw::editor
{
    /** @brief 씬마다 가장 새 사본 한 줄 — 씬 · 언제 · 남은 세대 · 복원할 수 없는 이유, 그리고 Restore · Discard 입니다. */
    class RecoveredWorkPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL` — 자동 저장이 같은 상수로 연다). */
        static constexpr const utf8* kPanelID = EditorAutosaveCommands::kRecoveryPanelID;

        /** @brief 창을 만듭니다(닫힌 채 시작한다). */
        RecoveredWorkPanel();

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Recovered Work"; }
        /** @brief 후보 표를 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }
    };
} // namespace sw::editor
