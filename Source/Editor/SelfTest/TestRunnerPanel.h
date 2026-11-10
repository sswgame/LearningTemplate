/**
 * @file TestRunnerPanel.h
 * @brief Test Runner 창 — 에디터 자체 테스트(같은 프로세스에서), 자동화 시나리오 · 단위 테스트(새 프로세스로)을 목록에서 골라 돌리고 결과를 봅니다.
 * @details 언리얼 Session Frontend > Automation · 유니티 Test Runner 에 해당합니다. 자체 테스트는 `EditorSelfTestRunner::requestRun` 으로 이 에디터 안에서,
 *          시나리오는 프로세스 하나를 통째로 쓰므로 `App -scenario … -unattended` 새 프로세스로, 단위 테스트는 `App -test -test-filter=` 새 프로세스로
 *          (작업 폴더 Bin) 돌립니다. 고르기 · 출력 읽기는 ImGui 없는 `Model/` 입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/SelfTest/Model/EditorTestOutputParser.h"
#include "Editor/SelfTest/Model/EditorTestSelection.h"

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

    private:
        /** @brief 에디터 자체 테스트 탭 — 레지스트리 목록(영역별 그룹) · 체크 · 검색 · Run Selected · Run All · 결과 색. */
        void drawSelfTestTab();
        /** @brief 시나리오 탭 — `Content/<영역>/automation/<이름>.scenario.xml` 목록, 고른 것을 새 프로세스로. */
        void drawScenarioTab();
        /** @brief 단위 테스트 탭 — 이 프로세스에 등록된 케이스 나무, 고른 케이스를 새 프로세스로. */
        void drawUnitTestTab();
        /** @brief 고른 케이스만 새 프로세스로 돌리는 단추를 그립니다. */
        void drawRunSelectedCasesButton( bool bBusy );
        /** @brief 케이스 나무(스위트별 접기 · 체크 · 마지막 결과)를 그립니다. */
        void drawUnitTestList();
        /** @brief 외부 실행이 끝났으면 결과를 가져옵니다. */
        void pollExternalRun();
        /** @brief 외부 실행 결과 줄 · 종료 코드를 그립니다(시나리오 · 단위 테스트가 같이 쓴다). */
        void drawExternalOutput();

    private:
        /** @brief 외부 실행이 무엇을 하는 중인지입니다. */
        enum class ExternalRunKind : uint8
        {
            None = 0,
            Scenario,
            UnitTestRun,
        };

        unique_ptr<EditorExternalToolJob> _pJob;
        EditorTestSelection               _selfTests;          ///< 자체 테스트 목록 · 체크 · 검색
        EditorTestSelection               _unitTests;          ///< 단위 테스트 목록 · 체크
        EditorScenarioList                _scenarios;          ///< 시나리오 목록 · 고른 것(리소스 id `engine/automation/…`)
        vector<EditorTestCaseResult>      _listUnitTestResult; ///< 마지막 단위 테스트 실행의 결과
        vector<string>                    _listExternalLine;   ///< 마지막 외부 실행의 출력
        string                            _externalTitle;      ///< 마지막 외부 실행이 무엇이었는지
        utf8                              _arrFilter[constant::buffer::kMax128];
        optional<int32>                   _externalExitCode;   ///< 외부 실행의 종료 코드 — 기다리지 못했으면 없다
        uint32                            _externalErrorCount; ///< 출력의 `[Error]` 줄 수
        uint32                            _selfTestGeneration; ///< `_selfTests` 를 맞춘 자체 테스트 등록 세대(0 은 등록이 한 번도 없었던 때라 빈 목록과 맞다)
        ExternalRunKind                   _externalRunKind;
        uint8                             _bScenarioListDirty : 1;
        uint8                             _bUnitTestListDirty : 1;
        uint8                             _bExternalLaunched  : 1;
        [[maybe_unused]] uint8            _reserved           : 5;
    };
} // namespace sw::editor
