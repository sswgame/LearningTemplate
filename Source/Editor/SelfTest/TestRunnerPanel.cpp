#include "pch.h"

#include "Editor/SelfTest/TestRunnerPanel.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Task/EditorExternalToolJob.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationScratch.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Renderer/Capture/BugItReport.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Resource/ResourcePaths.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct TestRunnerPanelInternal
        {
            /** @brief 외부 실행 출력 가운데 보이는 끝 줄 수입니다. */
            static constexpr uint32 kVisibleTailLineCount = 40;
            /** @brief 시나리오 파일 꼬리입니다. */
            static constexpr const utf8* kScenarioSuffix = ".scenario.xml";
            /** @brief 시나리오가 있는 폴더 이름입니다(`<영역>/automation/`). */
            static constexpr const utf8* kAutomationFolder = "/automation/";
            /** @brief 에디터 시나리오 폴더입니다 — 이 아래는 `-EnableEditor` 로 띄운다. */
            static constexpr const utf8* kEditorScenarioFolder = "/automation/editor/";

            static const ImVec4 kPassColor;
            static const ImVec4 kFailColor;

            /** @brief 이 실행 파일이 있는 폴더(Bin)입니다. 테스트 실행 파일 · 시나리오 실행의 작업 폴더입니다. */
            static string getBinDirectory() { return FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ); }

            /** @brief 출력의 `[Error]` 줄 수입니다. */
            static uint32 countErrorLines( const vector<string>& listLine )
            {
                uint32 count{ 0 };
                for ( const string& line : listLine )
                {
                    if ( line.find( "[Error]" ) != string::npos )
                        ++count;
                }
                return count;
            }

            /** @brief 지금 백엔드의 App 명령줄 플래그(`-dx12` …)입니다. 모르면 빈 글이라 App 이 설정의 기본 백엔드로 뜬다. */
            static string makeBackendArgument()
            {
                const FrameRenderer& renderer = editor::getSubsystem<FrameRenderer>();
                if ( renderer.getDevice() == nullptr )
                    return string{}; // 장치가 아직 없으면 플래그를 모른다
                const utf8* pFlag = BugItReport::findBackendFlag( renderer.getDevice()->getBackendName() );
                return pFlag[0] != '\0' ? string( " -" ) + pFlag : string{};
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
        , _uniqueSelectedSelfTest{}
        , _uniqueSelectedUnitTest{}
        , _listScenarioPath{}
        , _listTestExecutablePath{}
        , _listUnitTestName{}
        , _listUnitTestResult{}
        , _listExternalLine{}
        , _externalTitle{}
        , _arrFilter{}
        , _externalExitCode{ 0 }
        , _externalErrorCount{ 0 }
        , _selectedScenarioIndex{ kNoSelection }
        , _selectedExecutableIndex{ kNoSelection }
        , _externalRunKind{ ExternalRunKind::None }
        , _bScenarioListDirty{ SW_TRUE }
        , _bExecutableListDirty{ SW_TRUE }
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
            if ( ImGui::BeginTabItem( "Editor Self Tests" ) )
            {
                drawSelfTestTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "Scenarios" ) )
            {
                drawScenarioTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "Unit Tests" ) )
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
        using Registry = EditorExtensionRegistry<EditorSelfTestRegistration>;
        ImGui::SetNextItemWidth( 220.0f * EditorThemeUtil::getDpiScale() );
        ImGui::InputTextWithHint( "##SelfTestFilter", "Filter tests...", _arrFilter, sizeof( _arrFilter ) );
        EditorSelfTestMarks::note( "testRunner.filter" );
        const string_view filter( _arrFilter );

        // 고른 것 · 맞는 것 모두를 패턴(쉼표로 이은 id)으로 만든다 — 실행기는 패턴만 받는다.
        string selectedPattern;
        string allPattern;
        for ( uint32 index = 0; index < Registry::getCount(); ++index )
        {
            const utf8* pID = Registry::getAt( index )._pID;
            if ( filter.empty() == false && StringUtil::contains( pID, filter, true ) == false )
                continue;
            allPattern += ( allPattern.empty() ? "" : "," ) + string( pID );
            if ( _uniqueSelectedSelfTest.count( pID ) != 0 )
                selectedPattern += ( selectedPattern.empty() ? "" : "," ) + string( pID );
        }

        ImGui::SameLine();
        ImGui::BeginDisabled( selectedPattern.empty() );
        if ( ImGui::Button( "Run Selected" ) )
            (void)EditorSelfTestRunner::requestRun( selectedPattern, false ); // 버튼은 실행 중에 회색이다 — 거절될 일이 없다
        EditorSelfTestMarks::note( "testRunner.runSelected" );
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( allPattern.empty() );
        if ( ImGui::Button( "Run All" ) )
            (void)EditorSelfTestRunner::requestRun( allPattern, false ); // 위와 같다
        ImGui::EndDisabled();
        ImGui::SameLine();
        if ( EditorSelfTestRunner::isRunning() )
            ImGui::TextDisabled( "Running..." );
        else
            ImGui::TextDisabled( "%u tests", Registry::getCount() );

        const vector_reference<const EditorSelfTestResult> listResult = EditorSelfTestRunner::getResults();
        if ( ImGui::BeginChild( "##SelfTestList", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders ) )
        {
            string_view openGroup;
            bool        bGroupOpen{ false };
            for ( uint32 index = 0; index < Registry::getCount(); ++index )
            {
                const utf8*       pID   = Registry::getAt( index )._pID;
                const string_view group = EditorTestOutputParser::findSuiteName( pID );
                if ( filter.empty() == false && StringUtil::contains( pID, filter, true ) == false )
                    continue;
                if ( group != openGroup || index == 0 )
                {
                    if ( bGroupOpen )
                        ImGui::TreePop();
                    openGroup  = group;
                    bGroupOpen = ImGui::TreeNodeEx( string( group ).c_str(), ImGuiTreeNodeFlags_DefaultOpen );
                }
                if ( bGroupOpen == false )
                    continue;

                bool bSelected = _uniqueSelectedSelfTest.count( pID ) != 0;
                if ( ImGui::Checkbox( pID, &bSelected ) )
                {
                    if ( bSelected )
                        _uniqueSelectedSelfTest.insert( pID );
                    else
                        _uniqueSelectedSelfTest.erase( pID );
                }
                const string markKey = string( "testRunner.select." ) + pID;
                EditorSelfTestMarks::note( markKey.c_str() );
                for ( const EditorSelfTestResult& result : listResult )
                {
                    if ( result._id != pID )
                        continue;
                    ImGui::SameLine();
                    ImGui::TextColored( result._bPassed ? TestRunnerPanelInternal::kPassColor : TestRunnerPanelInternal::kFailColor, "%s (%u frames)%s%s",
                                        result._bPassed ? "PASS" : "FAIL", result._frameCount, result._reason.empty() ? "" : " - ", result._reason.c_str() );
                }
            }
            if ( bGroupOpen )
                ImGui::TreePop();
        }
        ImGui::EndChild();
    }

    void TestRunnerPanel::drawScenarioTab()
    {
        if ( _bScenarioListDirty == SW_TRUE )
        {
            _bScenarioListDirty = SW_FALSE;
            _listScenarioPath.clear();
            const string&  root = ResourcePaths::getRootFolderPath();
            vector<string> listFilePath;
            if ( FileUtil::collectFiles( root, ".xml", listFilePath, true ) )
            {
                for ( const string& filePath : listFilePath )
                {
                    string relative = filePath;
                    if ( StringUtil::startsWith( relative, root ) )
                        relative.erase( 0, root.size() );
                    StringUtil::replaceChar( relative, '\\', '/' );
                    while ( relative.empty() == false && relative[0] == '/' )
                    {
                        relative.erase( 0, 1 );
                    }
                    const bool bScenario = StringUtil::endsWith( relative, TestRunnerPanelInternal::kScenarioSuffix ) &&
                                           relative.find( TestRunnerPanelInternal::kAutomationFolder ) != string::npos;
                    if ( bScenario )
                        _listScenarioPath.push_back( relative );
                }
            }
            std::sort( _listScenarioPath.begin(), _listScenarioPath.end() );
        }

        const bool bBusy           = _pJob->isPending();
        const bool bScenarioChosen = _selectedScenarioIndex < _listScenarioPath.size();
        ImGui::BeginDisabled( bBusy || bScenarioChosen == false );
        if ( ImGui::Button( "Run Scenario" ) && bScenarioChosen )
        {
            const string& scenarioPath = _listScenarioPath[_selectedScenarioIndex];
            const bool    bEditor      = ( "/" + scenarioPath ).find( TestRunnerPanelInternal::kEditorScenarioFolder ) != string::npos;
            // 상태 폴더는 이 에디터 프로세스의 것이고 띄울 때마다 비운다 — 고정 이름이면 같은 Bin 의 다른 에디터 · 지난 실행의 레이아웃 · 설정을 읽는다.
            const string stateDir = FileUtil::joinPath( TestRunnerPanelInternal::getBinDirectory(),
                                                        string( kAutomationOutputFolder ) + "/" + AutomationScratch::makeInstanceID( "TestRunnerEditorState" ) );
            if ( bEditor )
                (void)FileUtil::removeDirectory( stateDir ); // 못 비우면 FileUtil 이 경고를 남기고 남은 상태로 띄운다 — 사람이 보는 손 실행이다
            ProcessCommand command{ FileUtil::getExecutablePath(), {} };
            const string   backendArgument = TestRunnerPanelInternal::makeBackendArgument(); // " -dx12" 꼴 — 앞 공백을 뗀 글이 인자 하나다
            if ( backendArgument.empty() == false )
                command._listArgument.push_back( backendArgument.substr( 1 ) );
            if ( bEditor )
            {
                command._listArgument.push_back( "-" + string( getCommandLineArgumentSpelling( CommandLineArgument::ENABLE_EDITOR ) ) );
                command._listArgument.push_back( string( "-" ) + kAutomationEditorStateVariable + "=" + stateDir );
            }
            command._listArgument.push_back( "-" + string( getCommandLineArgumentSpelling( CommandLineArgument::SCENARIO ) ) + "=" + scenarioPath );
            command._listArgument.push_back( "-" + string( getCommandLineArgumentSpelling( CommandLineArgument::UNATTENDED ) ) );
            if ( _pJob->request( command, TestRunnerPanelInternal::getBinDirectory() ) )
            {
                _externalRunKind = ExternalRunKind::Scenario;
                _externalTitle   = scenarioPath;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Refresh" ) )
            _bScenarioListDirty = SW_TRUE;
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", bBusy ? "Running in a new process..." : "Runs App with -scenario in a new process (-unattended)" );

        if ( ImGui::BeginChild( "##ScenarioList", ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y * 0.5f }, ImGuiChildFlags_Borders ) )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( _listScenarioPath.size() ); ++index )
            {
                if ( ImGui::Selectable( _listScenarioPath[index].c_str(), _selectedScenarioIndex == index ) )
                    _selectedScenarioIndex = index;
            }
        }
        ImGui::EndChild();
        if ( _externalRunKind == ExternalRunKind::Scenario )
            drawExternalOutput();
    }

    void TestRunnerPanel::refreshExecutableList( const string& binDirectory )
    {
        if ( _bExecutableListDirty != SW_TRUE )
            return;
        _bExecutableListDirty = SW_FALSE;
        _listTestExecutablePath.clear();
        vector<string> listFilePath;
        if ( FileUtil::collectFiles( FileUtil::joinPath( binDirectory, "../TestBin" ), ".exe", listFilePath, false ) )
        {
            for ( const string& filePath : listFilePath )
            {
                if ( StringUtil::endsWith( FileUtil::getFileNamePart( filePath ), "Test.exe" ) )
                    _listTestExecutablePath.push_back( filePath );
            }
        }
        std::sort( _listTestExecutablePath.begin(), _listTestExecutablePath.end() );
    }

    void TestRunnerPanel::drawExecutableCombo( const string& binDirectory, bool bBusy, bool bExecutableChosen )
    {
        ImGui::SetNextItemWidth( 220.0f * EditorThemeUtil::getDpiScale() );
        const utf8* pPreview = bExecutableChosen ? _listTestExecutablePath[_selectedExecutableIndex].c_str() : "Choose a test executable";
        if ( ImGui::BeginCombo( "##TestExecutable", pPreview ) == false )
            return;
        for ( uint32 index = 0; index < static_cast<uint32>( _listTestExecutablePath.size() ); ++index )
        {
            const string fileName = FileUtil::getFileNamePart( _listTestExecutablePath[index] );
            if ( ImGui::Selectable( fileName.c_str(), _selectedExecutableIndex == index ) && bBusy == false )
            {
                // 케이스 목록은 실행 파일에 묻는다(--test_list) — 필터를 적용한 목록이라 지금 고를 수 있는 것과 같다.
                _selectedExecutableIndex = index;
                _listUnitTestName.clear();
                _uniqueSelectedUnitTest.clear();
                if ( _pJob->request( ProcessCommand{ _listTestExecutablePath[index], { "--test_list" } }, binDirectory ) )
                {
                    _externalRunKind = ExternalRunKind::UnitTestList;
                    _externalTitle   = fileName + " --test_list";
                }
            }
        }
        ImGui::EndCombo();
    }

    void TestRunnerPanel::drawRunSelectedCasesButton( const string& binDirectory, bool bBusy, bool bExecutableChosen )
    {
        ImGui::BeginDisabled( bBusy || _uniqueSelectedUnitTest.empty() || bExecutableChosen == false );
        if ( ImGui::Button( "Run Selected Cases" ) && bExecutableChosen )
        {
            vector<string> listCase;
            for ( const string& name : _listUnitTestName )
            {
                if ( _uniqueSelectedUnitTest.count( name ) != 0 )
                    listCase.push_back( name );
            }
            const string filter = EditorTestOutputParser::makeFilter( listCase );
            // 작업 폴더는 Bin 이다 — 테스트는 거기서 위로 올라가며 Resource/ 를 찾는다(CLAUDE.md 의 테스트 실행 규칙).
            if ( _pJob->request( ProcessCommand{ _listTestExecutablePath[_selectedExecutableIndex], { "--test_filter=" + filter } }, binDirectory ) )
            {
                _externalRunKind = ExternalRunKind::UnitTestRun;
                _externalTitle   = FileUtil::getFileNamePart( _listTestExecutablePath[_selectedExecutableIndex] ) + " --test_filter=" + filter;
            }
        }
        ImGui::EndDisabled();
    }

    void TestRunnerPanel::drawUnitTestList()
    {
        if ( ImGui::BeginChild( "##UnitTestList", ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y * 0.5f }, ImGuiChildFlags_Borders ) )
        {
            string_view openSuite;
            bool        bSuiteOpen{ false };
            for ( size_t index = 0; index < _listUnitTestName.size(); ++index )
            {
                const string&     name  = _listUnitTestName[index];
                const string_view suite = EditorTestOutputParser::findSuiteName( name );
                if ( suite != openSuite || index == 0 )
                {
                    if ( bSuiteOpen )
                        ImGui::TreePop();
                    openSuite  = suite;
                    bSuiteOpen = ImGui::TreeNode( string( suite ).c_str() );
                }
                if ( bSuiteOpen == false )
                    continue;
                bool bSelected = _uniqueSelectedUnitTest.count( name ) != 0;
                if ( ImGui::Checkbox( name.c_str(), &bSelected ) )
                {
                    if ( bSelected )
                        _uniqueSelectedUnitTest.insert( name );
                    else
                        _uniqueSelectedUnitTest.erase( name );
                }
                for ( const EditorTestCaseResult& result : _listUnitTestResult )
                {
                    if ( result._name != name )
                        continue;
                    const bool bPassed = result._state == EditorTestCaseState::Passed || result._state == EditorTestCaseState::Skipped;
                    ImGui::SameLine();
                    ImGui::TextColored( bPassed ? TestRunnerPanelInternal::kPassColor : TestRunnerPanelInternal::kFailColor, "%s %.1f ms",
                                        result._state == EditorTestCaseState::Passed    ? "OK"
                                        : result._state == EditorTestCaseState::Skipped ? "SKIPPED"
                                        : result._state == EditorTestCaseState::Failed  ? "FAILED"
                                                                                        : "DID NOT FINISH",
                                        static_cast<float64>( result._milliseconds ) );
                }
            }
            if ( bSuiteOpen )
                ImGui::TreePop();
        }
        ImGui::EndChild();
    }

    void TestRunnerPanel::drawUnitTestTab()
    {
        const string binDirectory = TestRunnerPanelInternal::getBinDirectory();
        refreshExecutableList( binDirectory );

        const bool bBusy             = _pJob->isPending();
        const bool bExecutableChosen = _selectedExecutableIndex < _listTestExecutablePath.size();
        drawExecutableCombo( binDirectory, bBusy, bExecutableChosen );
        ImGui::SameLine();
        drawRunSelectedCasesButton( binDirectory, bBusy, bExecutableChosen );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Refresh" ) )
            _bExecutableListDirty = SW_TRUE;
        if ( _listTestExecutablePath.empty() )
            ImGui::TextDisabled( "No *Test.exe next to this build (build the AllTests target)" );

        drawUnitTestList();
        if ( _externalRunKind == ExternalRunKind::UnitTestRun || _externalRunKind == ExternalRunKind::UnitTestList )
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
        _externalErrorCount = TestRunnerPanelInternal::countErrorLines( _listExternalLine );
        if ( _externalRunKind == ExternalRunKind::UnitTestList )
            EditorTestOutputParser::collectTestNames( _listExternalLine, _listUnitTestName );
        else if ( _externalRunKind == ExternalRunKind::UnitTestRun )
            EditorTestOutputParser::collectResults( _listExternalLine, _listUnitTestResult );
    }

    void TestRunnerPanel::drawExternalOutput()
    {
        ImGui::Separator();
        if ( _pJob->isPending() )
        {
            ImGui::TextDisabled( "%s — running...", _externalTitle.c_str() );
            return;
        }
        if ( _externalTitle.empty() )
            return;
        if ( _bExternalLaunched == SW_FALSE )
        {
            ImGui::TextColored( TestRunnerPanelInternal::kFailColor, "%s — could not be launched", _externalTitle.c_str() );
            return;
        }
        const bool bPassed = _externalExitCode == 0;
        ImGui::TextColored( bPassed ? TestRunnerPanelInternal::kPassColor : TestRunnerPanelInternal::kFailColor, "%s — exit code %d, %u [Error] lines",
                            _externalTitle.c_str(), _externalExitCode, _externalErrorCount );
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
