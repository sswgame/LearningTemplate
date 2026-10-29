/**
 * @file BugReportModel.h
 * @brief 버그 리포트 창의 모델 — 메모, 지난 BugReport 폴더 줄(최근 것 먼저), 지금 상태 모으기 · 폴더 열기 · 그 위치로 가기 명령. ImGui 를 모릅니다.
 * @details 패널(`BugReportPanel`)은 메모 칸 · 표 · 단추를 그리고 경로 복사(ImGui 클립보드)만 한다 — 계층 · 인스펙터 `Model/` 과 같은 선이다
 *          (언리얼 BugIt 명령과 그 결과를 여는 창이 나뉜 것과 같다). 모으기는 커맨드 `help.reportBug` 와 같은 `BugReport::makeInputs` → `BugReport::capture` 다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 지난 BugReport 폴더 한 줄입니다. */
    struct BugReportRow
    {
        string _directory; ///< 폴더 경로(작업 폴더 기준)
        string _name;      ///< 폴더 이름(`yyyyMMdd-HHmmss`)
        string _note;      ///< info.txt 의 메모. 읽지 못하면 비어 있다
    };

    /**
     * @class BugReportModel
     * @brief 버그 리포트 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class BugReportModel
    {
    public:
        BugReportModel();

        /** @brief 줄을 다시 읽어야 하면(처음 · 모은 뒤 · Refresh) `Saved/BugReports` 아래 폴더를 다시 읽습니다. 패널이 그리기마다 부릅니다. */
        void refreshIfDirty();
        /** @brief 다음 `refreshIfDirty` 가 줄을 다시 읽게 합니다(Refresh 단추). */
        void markDirty() { _bRowsDirty = true; }

        /** @brief 메모 칸이 직접 고치는 글입니다. */
        string&                     editNote() { return _note; }
        const vector<BugReportRow>& getRows() const { return _listRow; }

        /**
         * @brief 메모를 붙여 지금 상태를 BugReport 폴더에 모으고 결과를 알림으로 알립니다(Collect Now 단추). 모았으면 메모를 비웁니다(info.txt 에 남았다).
         * @return 폴더와 info.txt 를 썼으면 true 입니다.
         */
        bool captureNow();
        /** @brief 폴더 줄 @p row 를 파일 탐색기로 엽니다. */
        static void showInFileExplorer( const BugReportRow& row );
        /** @brief 폴더 줄 @p row 가 남긴 자리(씬 · 카메라)로 돌아갑니다. 결과는 로그에 남습니다. */
        static void goTo( const BugReportRow& row );
        /** @brief @p row 의 절대 경로입니다. 바꾸지 못하면 그대로 돌려줍니다. */
        static string makeAbsolutePath( const BugReportRow& row );

    private:
        vector<BugReportRow> _listRow;
        string               _note;
        bool                 _bRowsDirty;
    };
} // namespace sw::editor
