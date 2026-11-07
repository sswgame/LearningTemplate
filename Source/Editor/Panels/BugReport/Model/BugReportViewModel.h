/**
 * @file BugReportViewModel.h
 * @brief 버그 리포트 창의 ViewModel — 지난 BugReport 폴더 줄(최근 것 먼저)을 모으고(`gather`), 지금 상태 모으기 · 폴더 열기 · 그 위치로 가기 명령을 받습니다. ImGui 를 모릅니다.
 * @details 패널(`BugReportPanel`)은 메모 칸(편집 버퍼는 View 의 것이다) · 표 · 단추를 그리고 경로 복사(ImGui 클립보드)만 한다(`docs/agents/EditorUI.md` MVVM 계약,
 *          언리얼 BugIt 명령과 그 결과를 여는 창이 나뉜 것과 같다). 모으기는 커맨드 `help.reportBug` 와 같은 `BugReport::makeInputs` → `BugReport::capture` 다.
 *          모으기 · 폴더 열기는 디스크 · 탐색기 일이고 씬 문서를 바꾸지 않아 되돌리기에 남지 않는다. 그 위치로 가기는 씬 · 카메라를 옮기는 탐색이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 지난 BugReport 폴더 한 줄입니다(값만). */
    struct BugReportRow
    {
        string _directory; ///< 폴더의 절대 경로(`ProcessRoots::_saved` 아래 - 목록을 만들 때 이미 절대다)
        string _name;      ///< 폴더 이름(`yyyyMMdd-HHmmss`)
        string _note;      ///< info.txt 의 메모. 읽지 못하면 비어 있다
    };

    /** @brief 버그 리포트 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct BugReportViewState
    {
        vector<BugReportRow> _listRow{};
    };

    /**
     * @class BugReportViewModel
     * @brief 버그 리포트 창의 ViewModel 입니다. 파일 머리말 참고.
     */
    class BugReportViewModel
    {
    public:
        BugReportViewModel();

        /** @brief 창이 그릴 값을 채웁니다. 줄을 다시 읽어야 하면(처음 · 모은 뒤 · `refresh`) `Saved/BugReports` 아래 폴더를 다시 읽습니다. */
        void gather( BugReportViewState& outState );
        /** @brief 다음 `gather` 가 줄을 다시 읽게 합니다(Refresh 단추). */
        void refresh() { _bRowsDirty = SW_TRUE; }
        /**
         * @brief 메모 @p note 를 붙여 지금 상태를 BugReport 폴더에 모으고 결과를 알림으로 알립니다(Collect Now 단추).
         * @return 폴더와 info.txt 를 썼으면 true 입니다(View 가 메모 칸을 비운다 — info.txt 에 남았다).
         */
        bool captureNow( const string& note );
        /** @brief 폴더 @p directory 를 파일 탐색기로 엽니다. */
        static void showInFileExplorer( const string& directory );
        /** @brief 폴더 @p directory 가 남긴 자리(씬 · 카메라)로 돌아갑니다. 결과는 로그에 남습니다. */
        static void goTo( const string& directory );

    private:
        /** @brief `Saved/BugReports` 아래 폴더를 다시 읽어 줄을 채웁니다. */
        void readRows();

    private:
        vector<BugReportRow> _listRow;
        uint8                _bRowsDirty : 1;
    };
} // namespace sw::editor
