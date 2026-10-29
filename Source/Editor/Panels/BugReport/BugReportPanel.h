/**
 * @file BugReportPanel.h
 * @brief 버그 리포트 창입니다. 메모를 붙여 지금 상태를 `Saved/BugReports/<시각>/` 에 모으고, 지난 폴더를 열거나 경로를 복사하거나 그 위치로 돌아갑니다(언리얼 BugReport).
 * @details 모으기는 커맨드 `help.reportBug` 와 같은 `BugReport::makeInputs` → `BugReport::capture` 이고, 이 창은 메모 칸과 지난 폴더 목록만 더합니다.
 *          상태와 명령은 `BugReportModel`(Model/, ImGui 없음)이 들고, 이 패널은 그리기와 경로 복사만 한다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/BugReport/Model/BugReportModel.h"

namespace sw::editor
{
    /** @brief 메모 입력 · 지금 상태 모으기 · 지난 BugReport 폴더 목록을 보이는 도구 창입니다. */
    class BugReportPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL`, `-gv_editorOpenPanel`). */
        static constexpr const utf8* kPanelID = "bug_it";

        /** @brief 버그 리포트 창을 만듭니다(닫힌 채 시작한다). */
        BugReportPanel();

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Bug Report"; }
        /** @brief 메모 필드 · 모으기 단추 · 폴더 목록을 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }

        /** @brief 메모 · 폴더 줄과 모으기 명령입니다. */
        BugReportModel& getModel() { return _model; }

    private:
        /** @brief 폴더 줄 @p row 의 단추(탐색기 · 경로 복사 · 그 위치로)를 그립니다. */
        static void drawRowActions( const BugReportRow& row );

    private:
        BugReportModel _model;
    };
} // namespace sw::editor
