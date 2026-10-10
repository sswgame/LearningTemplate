/**
 * @file TestRunnerPanel.h
 * @brief Test Runner 창 — 에디터 자체 시험(그 자리에서), 자동화 시나리오 · 단위 시험(새 프로세스로)을 목록에서 골라 돌리고 결과를 봅니다.
 * @details 언리얼 Session Frontend > Automation · 유니티 Test Runner 의 자리입니다. 자체 시험은 `EditorSelfTestRunner::requestRun` 으로 이 에디터 안에서,
 *          시나리오는 프로세스 하나를 통째로 쓰므로 `App -scenario … -unattended` 새 프로세스로, 단위 시험은 `TestBin/<이름>Test.exe --test_filter=` 새 프로세스로
 *          (작업 폴더 Bin) 돌립니다. 출력 읽기는 ImGui 없는 `EditorTestOutputParser` 입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/EditorTestOutputParser.h"

namespace sw::editor
{
    class EditorExternalToolJob;

    /** @brief 시험 목록 · 실행 · 결과를 한 창에 보이는 에디터 도구 창입니다. */
    class TestRunnerPanel : public IEditorPanel
    {
    public:
        TestRunnerPanel();
        virtual ~TestRunnerPanel() override;

        const utf8* getPanelTitle() const override { return "Test Runner"; }
        void        drawContent() override;
        bool        isToolPanel() const override { return true; }

    private:
        /** @brief 에디터 자체 시험 탭 — 등록부 목록(영역별 묶음) · 체크 · 검색 · Run Selected · Run All · 결과 색. */
        void drawSelfTestTab();
        /** @brief 시나리오 탭 — `Resource/<영역>/automation/<이름>.scenario.xml` 목록, 고른 것을 새 프로세스로. */
        void drawScenarioTab();
        /** @brief 단위 시험 탭 — `TestBin/<이름>Test.exe` 마다 `--test_list` 나무, 고른 케이스를 새 프로세스로. */
        void drawUnitTestTab();
        /** @brief 외부 실행이 끝났으면 결과를 가져옵니다. */
        void pollExternalRun();
        /** @brief 외부 실행 결과 줄 · 종료 코드를 그립니다(시나리오 · 단위 시험이 같이 쓴다). */
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
        unordered_set<string>             _uniqueSelectedSelfTest; ///< 체크한 자체 시험 id
        unordered_set<string>             _uniqueSelectedUnitTest; ///< 체크한 단위 시험 케이스 이름
        vector<string>                    _listScenarioPath;       ///< 리소스 id(`engine/automation/…`)
        vector<string>                    _listTestExecutablePath; ///< `TestBin/<이름>Test.exe`
        vector<string>                    _listUnitTestName;       ///< 고른 실행 파일의 케이스
        vector<EditorTestCaseResult>      _listUnitTestResult;     ///< 마지막 단위 시험 실행의 결과
        vector<string>                    _listExternalLine;       ///< 마지막 외부 실행의 출력
        string                            _externalTitle;          ///< 마지막 외부 실행이 무엇이었는지
        utf8                              _arrFilter[constant::kMaxBuffer128];
        int32                             _externalExitCode;
        uint32                            _externalErrorCount;      ///< 출력의 `[Error]` 줄 수
        uint32                            _selectedScenarioIndex;   ///< 고른 시나리오(`kNoSelection` 이면 없음)
        uint32                            _selectedExecutableIndex; ///< 고른 시험 실행 파일(`kNoSelection` 이면 없음)
        ExternalRunKind                   _externalRunKind;
        uint8                             _bScenarioListDirty   : 1;
        uint8                             _bExecutableListDirty : 1;
        uint8                             _bExternalLaunched    : 1;
        [[maybe_unused]] uint8            _reserved             : 5;
    };
} // namespace sw::editor
