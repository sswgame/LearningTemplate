/**
 * @file TestRunnerPanel.h
 * @brief Test Runner 창 — 에디터 자체 테스트(같은 프로세스에서), 자동화 시나리오 · 단위 테스트(새 프로세스로)을 목록에서 골라 돌리고 결과를 봅니다.
 * @details 언리얼 Session Frontend > Automation · 유니티 Test Runner 에 해당합니다. 자체 테스트는 `EditorSelfTestRunner::requestRun` 으로 이 에디터 안에서,
 *          시나리오는 프로세스 하나를 통째로 쓰므로 `App -scenario … -unattended` 새 프로세스로, 단위 테스트는 `TestBin/<이름>Test.exe --test_filter=` 새 프로세스로
 *          (작업 폴더 Bin) 돌립니다. 출력 읽기는 ImGui 없는 `EditorTestOutputParser` 입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/SelfTest/EditorTestOutputParser.h"

namespace sw::editor
{
    class EditorExternalToolJob;

    /** @brief 테스트 목록 · 실행 · 결과를 한 창에 보이는 에디터 도구 창입니다. */
    class TestRunnerPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "test_runner";

        TestRunnerPanel();
        virtual ~TestRunnerPanel() override;

        const utf8* getPanelTitle() const override { return "Test Runner"; }
        void        drawContent() override;
        bool        isToolPanel() const override { return true; }

    private:
        /** @brief 에디터 자체 테스트 탭 — 레지스트리 목록(영역별 그룹) · 체크 · 검색 · Run Selected · Run All · 결과 색. */
        void drawSelfTestTab();
        /** @brief 시나리오 탭 — `Content/<영역>/automation/<이름>.scenario.xml` 목록, 고른 것을 새 프로세스로. */
        void drawScenarioTab();
        /** @brief 단위 테스트 탭 — `TestBin/<이름>Test.exe` 마다 `--test_list` 나무, 고른 케이스를 새 프로세스로. */
        void drawUnitTestTab();
        /** @brief 더럽혀졌으면 `TestBin` 의 `*Test.exe` 목록을 다시 모읍니다. */
        void refreshExecutableList( const string& binDirectory );
        /** @brief 실행 파일 고르기 콤보를 그립니다. 고르면 그 파일에 케이스 목록을 묻습니다(`--test_list`). */
        void drawExecutableCombo( const string& binDirectory, bool bBusy, bool bExecutableChosen );
        /** @brief 고른 케이스만 새 프로세스로 돌리는 단추를 그립니다. */
        void drawRunSelectedCasesButton( const string& binDirectory, bool bBusy, bool bExecutableChosen );
        /** @brief 케이스 나무(스위트별 접기 · 체크 · 마지막 결과)를 그립니다. */
        void drawUnitTestList();
        /** @brief 외부 실행이 끝났으면 결과를 가져옵니다. */
        void pollExternalRun();
        /** @brief 외부 실행 결과 줄 · 종료 코드를 그립니다(시나리오 · 단위 테스트가 같이 쓴다). */
        void drawExternalOutput();

    private:
        /** @brief 목록에서 아무것도 고르지 않았음을 뜻합니다. */
        static constexpr uint32 kNoSelection = invalid_index::kUint32;

        /** @brief 외부 실행이 무엇을 하는 중인지입니다. */
        enum class ExternalRunKind : uint8
        {
            None = 0,
            Scenario,
            UnitTestList,
            UnitTestRun,
        };

        unique_ptr<EditorExternalToolJob> _pJob;
        unordered_set<string>             _uniqueSelectedSelfTest; ///< 체크한 자체 테스트 id
        unordered_set<string>             _uniqueSelectedUnitTest; ///< 체크한 단위 테스트 케이스 이름
        vector<string>                    _listScenarioPath;       ///< 리소스 id(`engine/automation/…`)
        vector<string>                    _listTestExecutablePath; ///< `TestBin/<이름>Test.exe`
        vector<string>                    _listUnitTestName;       ///< 고른 실행 파일의 케이스
        vector<EditorTestCaseResult>      _listUnitTestResult;     ///< 마지막 단위 테스트 실행의 결과
        vector<string>                    _listExternalLine;       ///< 마지막 외부 실행의 출력
        string                            _externalTitle;          ///< 마지막 외부 실행이 무엇이었는지
        utf8                              _arrFilter[constant::kMaxBuffer128];
        int32                             _externalExitCode;
        uint32                            _externalErrorCount;      ///< 출력의 `[Error]` 줄 수
        uint32                            _selectedScenarioIndex;   ///< 고른 시나리오(`kNoSelection` 이면 없음)
        uint32                            _selectedExecutableIndex; ///< 고른 테스트 실행 파일(`kNoSelection` 이면 없음)
        ExternalRunKind                   _externalRunKind;
        uint8                             _bScenarioListDirty   : 1;
        uint8                             _bExecutableListDirty : 1;
        uint8                             _bExternalLaunched    : 1;
        [[maybe_unused]] uint8            _reserved             : 5;
    };
} // namespace sw::editor
