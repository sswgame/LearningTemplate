#include "pch.h"

#include "Editor/SelfTest/TestRunnerPanel.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/EngineChildProcess.h"
#include "Core/String/fixed_string.h"
#include "Core/Testing/TestRunner.h"

#include "Editor/Common/Commands/Asset/EditorContentScan.h"
#include "Editor/Common/EditorExtensionRegistry.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Task/EditorExternalToolJob.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct TestRunnerPanelInternal
        {
            /** @brief 외부 실행 출력 가운데 보이는 끝 줄 수입니다. */
            static constexpr uint32 kVisibleTailLineCount = 40;

            static const ImVec4 kPassColor;
            static const ImVec4 kFailColor;

            /** @brief 이 실행 파일이 있는 폴더(Bin)입니다. 테스트 실행 파일 · 시나리오 실행의 작업 폴더입니다. */
            static string getBinDirectory() { return FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ); }

            /** @brief 지금 백엔드의 App 명령줄 플래그(`-dx12` …)입니다. 모르면 빈 글이라 App 이 설정의 기본 백엔드로 뜬다. */
            static string makeBackendArgument()
            {
                const FrameRenderer& renderer = editor::getSubsystem<FrameRenderer>();
                if ( renderer.getDevice() == nullptr )
                    return string{}; // 장치가 아직 없으면 플래그를 모른다
                const utf8* pFlag = RHIBackendInfoTable::findCommandLineName( renderer.getDevice()->getBackendType() );
                return pFlag != nullptr ? string( " -" ) + pFlag : string{};
            }
        };

        const ImVec4 TestRunnerPanelInternal::kPassColor{ 0.35f, 0.85f, 0.45f, 1.0f };
        const ImVec4 TestRunnerPanelInternal::kFailColor{ 1.0f, 0.4f, 0.35f, 1.0f };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( TestRunnerPanel, TestRunnerPanel::kPanelID, EditorPanelCategory::Tool, 2030 );

    TestRunnerPanel::TestRunnerPanel()
        : IEditorPanel( false )
        , _pJob{ make_unique<EditorExternalToolJob>() }
        , _selfTests{}
        , _unitTests{}
        , _scenarios{}
        , _listUnitTestResult{}
        , _listExternalLine{}
        , _externalTitle{}
        , _arrFilter{}
        , _externalExitCode{}
        , _externalErrorCount{ 0 }
        , _selfTestGeneration{ 0 }
        , _externalRunKind{ ExternalRunKind::None }
        , _bScenarioListDirty{ SW_TRUE }
        , _bUnitTestListDirty{ SW_TRUE }
        , _bExternalLaunched{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    TestRunnerPanel::~TestRunnerPanel() = default;

    void TestRunnerPanel::drawContent()
    {
        pollExternalRun();
        // 자체 테스트가 도는 동안은 이 창이 입력을 받지 않는다 — 테스트가 패널을 열고 닫고 누르므로 여기서 누르면 테스트 결과를 해친다.
        const bool bSelfTestRunning = EditorSelfTestRunner::isRunning();
        ImGui::BeginDisabled( bSelfTestRunning );
        if ( ImGui::BeginTabBar( "TestRunnerTabs" ) )
        {
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "EditorSelfTests" ) ) )
            {
                drawSelfTestTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "Scenarios" ) ) )
            {
                drawScenarioTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "UnitTests" ) ) )
            {
                drawUnitTestTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndDisabled();
    }

    void TestRunnerPanel::drawSelfTestTab()
    {
        using Registry          = EditorExtensionRegistry<EditorSelfTestRegistration>;
        const uint32 generation = Registry::getList().getGeneration();
        if ( generation != _selfTestGeneration )
        {
            _selfTestGeneration = generation;
            vector<string> listID;
            for ( uint32 index = 0; index < Registry::getCount(); ++index )
            {
                listID.push_back( Registry::getAt( index )._pID );
            }
            _selfTests.setCases( std::move( listID ) );
        }

        ImGui::SetNextItemWidth( 220.0f * EditorThemeUtil::getDPIScale() );
        (void)EditorWidgets::drawHintField( "##SelfTestFilter", _arrFilter, sizeof( _arrFilter ), "Filter tests...", "testRunner.filter" );
        _selfTests.setFilter( _arrFilter );

        ImGui::SameLine();
        ImGui::BeginDisabled( _selfTests.getSelectedPattern().empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RunSelected" ) ) )
            (void)EditorSelfTestRunner::requestRun( _selfTests.getSelectedPattern(), false ); // 버튼은 실행 중에 회색이다 — 거절될 일이 없다
        EditorSelfTestMarks::record( "testRunner.runSelected" );
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( _selfTests.getVisiblePattern().empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RunAll" ) ) )
            (void)EditorSelfTestRunner::requestRun( _selfTests.getVisiblePattern(), false ); // 위와 같다
        ImGui::EndDisabled();
        ImGui::SameLine();
        if ( EditorSelfTestRunner::isRunning() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "Running" ) );
        else
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Tests", TextArgumentList().addInteger( "registryCount", static_cast<int64>( Registry::getCount() ) ) ).c_str() );

        const vector_reference<const EditorSelfTestResult> listResult = EditorSelfTestRunner::getResults();
        if ( ImGui::BeginChild( "##SelfTestList", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders ) )
        {
            for ( const EditorTestGroup& group : _selfTests.getGroups() )
            {
                if ( ImGui::TreeNodeEx( group._name.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) == false )
                    continue;
                for ( const uint32 index : group._listCaseIndex )
                {
                    const string& id = _selfTests.getCases()[index];
                    // 묶음 아래 줄은 뒷이름만 — 묶음 이름을 되풀이하면("theme" 아래 "theme.palette") 줄이 길고 훑기 어렵다(uiaudit 30). 전체 이름은 툴팁과 ID 에 남는다.
                    const string label     = string( EditorTestSelection::findLeafName( id ) ) + "##" + id;
                    bool         bSelected = _selfTests.isSelected( id );
                    if ( ImGui::Checkbox( label.c_str(), &bSelected ) )
                        _selfTests.setSelected( id, bSelected );
                    EditorWidgets::drawTooltip( id.c_str() );
                    const string markKey = "testRunner.select." + id;
                    EditorSelfTestMarks::record( markKey.c_str() );
                    for ( const EditorSelfTestResult& result : listResult )
                    {
                        if ( result._id != id )
                            continue;
                        ImGui::SameLine();
                        ImGui::TextColored( result._bPassed ? TestRunnerPanelInternal::kPassColor : TestRunnerPanelInternal::kFailColor, "%s", SW_LOCKEYFORMAT( "EdFormat", "Frames2", TextArgumentList().addText( "result", result._bPassed ? "pass" : "fail" ).addInteger( "frameCount", static_cast<int64>( result._frameCount ) ).addText( "hasReason", result._reason.empty() ? "no" : "yes" ).addText( "reason", result._reason ) ).c_str() );
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
    }

    void TestRunnerPanel::drawScenarioTab()
    {
        if ( _bScenarioListDirty == SW_TRUE )
        {
            _bScenarioListDirty = SW_FALSE;
            const string&  root = editor::getSubsystem<AssetPaths>().getRootFolderPath();
            vector<string> listFilePath;
            // 남의 시나리오 사본은 고를 시나리오가 아니다(EditorContentScan 이 뺀다)
            if ( EditorContentScan::collectFiles( root, root, ".xml", listFilePath, true ) == false )
                listFilePath.clear(); // 도중에 멈춘 목록을 전부로 보이지 않는다
            _scenarios.setFromFiles( listFilePath, root );
        }

        const bool bBusy           = _pJob->isPending();
        const bool bScenarioChosen = _scenarios.getChosen().empty() == false;
        ImGui::BeginDisabled( bBusy || bScenarioChosen == false );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RunScenario" ) ) && bScenarioChosen )
        {
            const string& scenarioPath = _scenarios.getChosen();
            // 에디터 상태 폴더는 주지 않는다 — 시나리오 실행은 자식 프로세스가 자기 폴더를 비우고 쓴다(`EditorPaths::prepareAutomationStateDirectory`).
            const string         backendArgument = TestRunnerPanelInternal::makeBackendArgument(); // " -dx12" 꼴 — 앞 공백 · 줄표를 뗀다
            const ProcessCommand command         = EngineChildProcess::makeSelfCommand(
                editor::getSubsystem<CommandLineManager>(), EditorTestOutputParser::makeScenarioArguments( scenarioPath, backendArgument.empty() ? string_view{} : string_view( backendArgument ).substr( 2 ) ) );
            if ( _pJob->request( command, TestRunnerPanelInternal::getBinDirectory() ) )
            {
                _externalRunKind = ExternalRunKind::Scenario;
                _externalTitle   = scenarioPath;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Refresh" ) ) )
            _bScenarioListDirty = SW_TRUE;
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", bBusy ? "Running in a new process..." : "Runs App with -scenario in a new process (-unattended)" );

        if ( ImGui::BeginChild( "##ScenarioList", ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y * 0.5f }, ImGuiChildFlags_Borders ) )
        {
            for ( const string& path : _scenarios.getPaths() )
            {
                if ( ImGui::Selectable( path.c_str(), _scenarios.getChosen() == path ) )
                    _scenarios.choose( path );
            }
        }
        ImGui::EndChild();
        if ( _externalRunKind == ExternalRunKind::Scenario )
            drawExternalOutput();
    }

    void TestRunnerPanel::drawRunSelectedCasesButton( bool bBusy )
    {
        const string& filter = _unitTests.getSelectedPattern();
        ImGui::BeginDisabled( bBusy || filter.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RunSelectedCases" ) ) )
        {
            // 작업 폴더는 Bin 이다 — 테스트는 거기서 위로 올라가며 Content/ 를 찾는다(CLAUDE.md 의 테스트 실행 규칙).
            // 자동화 실행이 부른 테스트 자식도 자동화 실행이다(EngineChildProcess)
            const ProcessCommand command = EngineChildProcess::makeSelfCommand( editor::getSubsystem<CommandLineManager>(), EditorTestOutputParser::makeUnitTestArguments( filter ) );
            if ( _pJob->request( command, TestRunnerPanelInternal::getBinDirectory() ) )
            {
                _externalRunKind = ExternalRunKind::UnitTestRun;
                _externalTitle   = "App -test -test-filter=" + filter;
            }
        }
        ImGui::EndDisabled();
    }

    void TestRunnerPanel::drawUnitTestList()
    {
        if ( ImGui::BeginChild( "##UnitTestList", ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y * 0.5f }, ImGuiChildFlags_Borders ) )
        {
            for ( const EditorTestGroup& group : _unitTests.getGroups() )
            {
                if ( ImGui::TreeNode( group._name.c_str() ) == false )
                    continue;
                for ( const uint32 index : group._listCaseIndex )
                {
                    const string& name      = _unitTests.getCases()[index];
                    bool          bSelected = _unitTests.isSelected( name );
                    if ( ImGui::Checkbox( name.c_str(), &bSelected ) )
                        _unitTests.setSelected( name, bSelected );
                    for ( const EditorTestCaseResult& result : _listUnitTestResult )
                    {
                        if ( result._name != name )
                            continue;
                        ImGui::SameLine();
                        ImGui::TextColored( result.isPassed() ? TestRunnerPanelInternal::kPassColor : TestRunnerPanelInternal::kFailColor, "%s %.1f ms", result.getStateText(), static_cast<float64>( result._milliseconds ) );
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
    }

    void TestRunnerPanel::drawUnitTestTab()
    {
        // 케이스는 이 프로세스의 등록에서 읽는다 — 테스트는 App 안에 있다(`App -test`, 따로 있던 `TestBin/*Test.exe` 는 없앴다).
        // 에디터 · 편집기 플러그인 케이스는 자식 `App -test` 에 등록되지 않는다(자체 테스트 `editor.unitTests` 가 돌린다) — 돌리지 못할 이름은 고르지 않게 뺀다.
        if ( _bUnitTestListDirty == SW_TRUE )
        {
            _bUnitTestListDirty = SW_FALSE;
            _unitTests.setCases( test::TestRunner::collectCaseNamesOutside( { "/Editor/" } ) );
        }

        drawRunSelectedCasesButton( _pJob->isPending() );
        ImGui::SameLine();
        if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Refresh" ) ) )
            _bUnitTestListDirty = SW_TRUE;

        drawUnitTestList();
        if ( _externalRunKind == ExternalRunKind::UnitTestRun )
            drawExternalOutput();
    }

    void TestRunnerPanel::pollExternalRun()
    {
        EditorExternalToolResult result;
        if ( _pJob->take( result ) != EditorTaskPoll::Ready )
            return;
        _listExternalLine   = std::move( result._listLine );
        _externalExitCode   = result._exitCode;
        _bExternalLaunched  = result._bLaunched ? SW_TRUE : SW_FALSE;
        _externalErrorCount = EditorTestOutputParser::countUndeclaredErrorLines( _listExternalLine );
        if ( _externalRunKind == ExternalRunKind::UnitTestRun )
            EditorTestOutputParser::collectResults( _listExternalLine, _listUnitTestResult );
    }

    void TestRunnerPanel::drawExternalOutput()
    {
        ImGui::Separator();
        if ( _pJob->isPending() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Running", TextArgumentList().addText( "externalTitle", _externalTitle.c_str() ) ).c_str() );
            return;
        }
        if ( _externalTitle.empty() )
            return;
        if ( _bExternalLaunched == SW_FALSE )
        {
            ImGui::TextColored( TestRunnerPanelInternal::kFailColor, "%s", SW_LOCKEYFORMAT( "EdFormat", "CouldNotBeLaunched", TextArgumentList().addText( "externalTitle", _externalTitle.c_str() ) ).c_str() );
            return;
        }
        if ( _externalExitCode.has_value() == false )
        {
            // 기다리지 못한 실행은 종료 코드가 없다 — 숫자를 꾸미지 않고 까닭(도구 작업이 남긴 출력 끝 줄)을 실패 색으로 보인다
            ImGui::TextColored( TestRunnerPanelInternal::kFailColor, "%s", _listExternalLine.empty() ? "" : _listExternalLine.back().c_str() );
            return;
        }
        const bool bPassed = *_externalExitCode == 0;
        ImGui::TextColored( bPassed ? TestRunnerPanelInternal::kPassColor : TestRunnerPanelInternal::kFailColor, "%s", SW_LOCKEYFORMAT( "EdFormat", "ExitCodeErrorLines", TextArgumentList().addText( "externalTitle", _externalTitle.c_str() ).addInteger( "externalExitCode", static_cast<int64>( *_externalExitCode ) ).addInteger( "externalErrorCount", static_cast<int64>( _externalErrorCount ) ) ).c_str() );
        if ( ImGui::BeginChild( "##ExternalOutput", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar ) )
        {
            const size_t first = _listExternalLine.size() > TestRunnerPanelInternal::kVisibleTailLineCount
                                   ? _listExternalLine.size() - TestRunnerPanelInternal::kVisibleTailLineCount
                                   : 0;
            for ( size_t index = first; index < _listExternalLine.size(); ++index )
            {
                ImGui::TextUnformatted( _listExternalLine[index].c_str() );
            }
        }
        ImGui::EndChild();
    }
} // namespace sw::editor
