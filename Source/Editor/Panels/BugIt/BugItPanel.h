/**
 * @file BugItPanel.h
 * @brief 버그 리포트 창입니다. 메모를 붙여 지금 상태를 `Saved/BugIt/<시각>/` 에 모으고, 지난 폴더를 열거나 경로를 복사하거나 그 위치로 돌아갑니다(언리얼 BugIt).
 * @details 모으기는 커맨드 `help.reportBug` 와 같은 `BugItReport::makeInputs` → `BugItReport::capture` 이고, 이 창은 메모 칸과 지난 폴더 목록만 더합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/GUI/IEditorPanel.h"

namespace sw::editor
{
    /** @brief 지난 BugIt 폴더 한 줄입니다. */
    struct BugItPanelRow
    {
        string _directory; ///< 폴더 경로(작업 폴더 기준)
        string _name;      ///< 폴더 이름(`yyyyMMdd-HHmmss`)
        string _note;      ///< info.txt 의 메모. 읽지 못하면 비어 있다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 메모 입력 · 지금 상태 모으기 · 지난 BugIt 폴더 목록을 보이는 도구 창입니다. */
    class BugItPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL`, `-gv_editorOpenPanel`). */
        static constexpr const utf8* kPanelID = "bug_it";

        /** @brief 버그 리포트 창을 만듭니다(닫힌 채 시작한다). */
        BugItPanel();

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Bug Report"; }
        /** @brief 메모 필드 · 모으기 단추 · 폴더 목록을 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }

        /**
         * @brief 메모 칸의 글을 붙여 지금 상태를 BugIt 폴더에 모으고 결과를 알립니다(Collect Now 단추와 같다).
         * @return 폴더와 info.txt 를 썼으면 true 입니다.
         */
        bool captureNow();
        /** @brief 지난 그리기의 폴더 줄 수입니다. */
        uint32 getRowCount() const { return static_cast<uint32>( _listRow.size() ); }

    private:
        /** @brief `Saved/BugIt` 아래 폴더를 다시 읽어 최근 것이 먼저 오게 줄을 만듭니다. */
        void refreshRows();
        /** @brief 폴더 줄 @p row 의 단추(탐색기 · 경로 복사 · 그 위치로)를 그립니다. */
        void drawRowActions( const BugItPanelRow& row ) const;

    private:
        vector<BugItPanelRow> _listRow;
        string                _note;
        bool                  _bRowsDirty;
    };
} // namespace sw::editor
