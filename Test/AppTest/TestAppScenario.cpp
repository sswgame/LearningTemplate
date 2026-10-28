#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#include "Engine/Config/GameSelection.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scenario/AutomationScenario.h"
#include "Engine/Scenario/ScenarioRecorder.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/CookContract.gen.h"

// 실제 App.exe 를 시나리오마다 · 백엔드마다 띄운다 — GPU · 창 · 셰이더가 필요하다. CI 러너엔 없다.
SW_TEST_REQUIRES_HOST( AppScenarioTest, "launches App.exe per automation scenario and backend - needs a GPU and a window" );

// ------------------------------------------------------------------------------
// AppScenarioTest — 자동화 시나리오(`Resource/<영역>/automation/*.scenario.xml`)를 백엔드마다 돌려 종료 코드를 본다
//
// 시나리오 파일을 놓기만 하면 돈다(파일 목록을 CMake 에 적지 않는다 — 실행 시작에 폴더를 훑어 케이스를 만든다). 게임은 프리셋마다 하나(`SW_ACTIVE_GAME`)이므로 그 프리셋의
// `AppTest_HostOnly` 가 **엔진 시나리오 + 그 게임 팩의 시나리오**를 돈다. 에디터 시나리오(`engine/automation/editor/`)는 `-EnableEditor` 로 돈다.
// 판정은 `AppTestUtil::runScenario` 가 한다 — 배포본에서 `<Launch>` 의 `gv_` 개발 변수가 필요한 시나리오는 띄우지 않고 건너뛴다(보고에 `[ PARTSKIP ]` 줄과 요약 개수가 남는다), 13(시나리오의 건너뜀 — 전경 창을 못 얻음 등)과 요구하지 않은 백엔드의 77 은 건너뛰고, 요구 백엔드(`--required_backends=`)의 77 은 실패다. 언리얼 Gauntlet 이 프로세스를 띄워 결과 코드 · 로그를 모으는 것과 같은 자리다.
// ------------------------------------------------------------------------------

namespace
{
    struct AppScenarioTestInternal
    {
        /** @brief @p listFolder(리소스 경로) 바로 아래의 시나리오 — 리소스 경로로, 폴더마다 이름순입니다(하위 폴더는 내려가지 않는다). */
        static sw::vector<sw::string> collectScenarioPaths( const sw::vector<sw::string>& listFolder )
        {
            sw::vector<sw::string> listPath;
            if ( sw::ResourcePaths::initialize() == false )
                return listPath;
            const sw::string& resourceRoot = sw::ResourcePaths::getRootFolderPath();
            for ( const sw::string& folderID : listFolder )
            {
                sw::vector<sw::string> listFile;
                const sw::string       folder = sw::FileUtil::joinPath( resourceRoot, folderID );
                if ( sw::FileUtil::isDirectory( folder ) == false || sw::FileUtil::collectFiles( folder, ".xml", listFile, false ) == false )
                    continue;
                std::sort( listFile.begin(), listFile.end() );
                for ( const sw::string& filePath : listFile )
                {
                    if ( sw::StringUtil::endsWith( filePath, ".scenario.xml", true ) )
                        listPath.push_back( sw::ResourcePaths::toResourceID( filePath ) );
                }
            }
            return listPath;
        }

        /** @brief 엔진(`engine/automation`)과 활성 게임 팩(`<팩>/automation`)의 시나리오입니다. */
        static sw::vector<sw::string> collectGameScenarioPaths()
        {
            sw::vector<sw::string> listFolder{ "engine/automation" };
            const sw::string       packRoot = test::AppTestUtil::readPackRoot( sw::GameSelection::getName() );
            if ( packRoot.empty() == false )
                listFolder.push_back( packRoot + "/automation" );
            return collectScenarioPaths( listFolder );
        }

        /** @brief 돌릴 백엔드 스위치(`-dx12` …)입니다. 배포본은 백엔드를 하나만 링크하므로 스위치 없이 "이 빌드가 가진 것" 하나입니다(AppSmokeTest 와 같다). */
        static sw::vector<const utf8*> collectBackendSwitches()
        {
#if defined( SW_SHIPPING )
            return sw::vector<const utf8*>{ "" };
#else
    #define SW_APP_SCENARIO_BACKEND_SWITCH( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) "-" CommandLineName,
            return sw::vector<const utf8*>{ SW_RHI_BACKEND_TABLE( SW_APP_SCENARIO_BACKEND_SWITCH ) };
    #undef SW_APP_SCENARIO_BACKEND_SWITCH
#endif
        }

        /** @brief `collectBackendSwitches` 를 매개변수 묶음(`SW_TEST_CASE_PER_PARAMETER`)의 목록 모양(글)으로 바꾼 것입니다. */
        static sw::vector<sw::string> collectBackendParameters()
        {
            sw::vector<sw::string> listParameter;
            for ( const utf8* pSwitch : collectBackendSwitches() )
            {
                listParameter.emplace_back( pSwitch );
            }
            return listParameter;
        }

        /** @brief 시나리오 경로의 묶음 이름(파일 이름의 첫 `.` 앞 — `vfxgpulights`, 이름 줄기와 같다)입니다. 케이스 이름과 에디터 묶음 이름에 쓴다. */
        static sw::string getGroupName( const sw::string& scenarioPath )
        {
            return sw::getAutomationScenarioStem( scenarioPath );
        }

        /** @brief 백엔드 스위치(`-gl`)의 이름(`gl`)입니다. 스위치가 없는 배포본은 `default` 입니다. */
        static sw::string getBackendName( const utf8* pSwitch )
        {
            return pSwitch[0] == '-' ? sw::string( pSwitch + 1 ) : sw::string( "default" );
        }

        /** @brief 시나리오가 `<Record>` 단계를 가졌으면 true 입니다(백엔드끼리 기록 줄을 견주는 시나리오). 못 읽으면 false — 그 시나리오가 제 실행에서 읽기 오류로 진다. */
        static bool declaresRecord( const sw::ResourceID& resourceID )
        {
            sw::AutomationScenario scenario;
            sw::string             error;
            if ( scenario.loadFromPath( resourceID, error ) == false )
                return false;
            for ( const sw::AutomationStep& step : scenario.getSteps() )
            {
                if ( step._kind == "Record" )
                    return true;
            }
            return false;
        }

        /** @brief 시나리오가 루트에 `editor="true"` 를 적었으면 true 입니다. 못 읽으면 false — 그 시나리오가 제 실행에서 읽기 오류로 진다. */
        static bool requiresEditor( const sw::ResourceID& resourceID )
        {
            sw::AutomationScenario scenario;
            sw::string             error;
            return scenario.loadFromPath( resourceID, error ) && scenario.getTestOptions()._bRequiresEditor;
        }

        /** @brief 시나리오가 `<Launch>` 로 선언한 기동 인자(`-gv_firstScene=…`)입니다. 못 읽으면 빈 글 — 그 시나리오가 제 실행에서 읽기 오류로 진다. */
        static sw::string readLaunchArguments( const sw::ResourceID& resourceID )
        {
            sw::AutomationScenario scenario;
            sw::string             error;
            return scenario.loadFromPath( resourceID, error ) ? scenario.getTestOptions().makeLaunchArguments() : sw::string{};
        }

        /** @brief 실행 쓰기 루트(`AppTestUtil::getRunFolder`) 기준 에디터 상태 폴더 경로(`Saved/Automation/EditorState/<이름>`)입니다 — 글은 `AutomationScenario.h` 상수이고 스크립트 `scenario` 가 같은 상수를 읽는다. */
        static sw::string makeEditorStateRelativePath( const sw::string& name )
        {
            return sw::string( sw::kAutomationOutputFolder ) + "/" + sw::kAutomationEditorStateFolder + "/" + name;
        }

        /** @brief 에디터를 켠 실행의 인자(`-EnableEditor -gv_editorStateDir=<폴더>`)입니다. */
        static sw::string makeEditorStateArguments( const sw::string& stateFolder )
        {
            return test::AppTestUtil::makeSwitchArgument( sw::CommandLineArgument::ENABLE_EDITOR ) + " -" + sw::kAutomationEditorStateVariable + "=" + stateFolder;
        }

        /**
         * @brief 에디터를 켠 실행의 인자 — `-EnableEditor` 와 시나리오 · 백엔드마다 비운 에디터 상태 폴더입니다(에디터 시나리오 체인과 같은 규칙).
         * @details 사용자의 `Saved/Editor`(레이아웃 · 최근 씬)를 읽으면 실행마다 시작 화면이 달라진다.
         */
        static sw::string makeEditorArguments( const sw::string& scenarioPath, const utf8* pSwitch )
        {
            const sw::string backend = pSwitch[0] == '-' ? sw::string( pSwitch + 1 ) : sw::string( "default" );
            // 실행 쓰기 루트(케이스마다 다른 폴더) 아래다 — 테스트의 작업 폴더(`Bin`) 기준으로 풀면 같은 시나리오를 도는 다른 조각과 폴더가 겹친다.
            const sw::string stateFolder = test::AppTestUtil::makeRunPath( makeEditorStateRelativePath( sw::getAutomationScenarioStem( scenarioPath ) + "_" + backend ) );
            SW_EXPECT_TRUE( stateFolder.empty() == false && sw::FileUtil::removeDirectory( stateFolder ) );
            return makeEditorStateArguments( stateFolder );
        }

        /**
         * @brief 시나리오 하나를 백엔드 하나(@p pSwitch)로 돌려 판정합니다 — 케이스 하나의 본문입니다. 실패는 `runScenario` 가 시나리오 · 백엔드 · 종료 코드와 뜻 · 보고의
         *        `failures[]` · `unhandledLogs[]` 줄과 함께 기록한다. 루트에 `editor="true"` 를 적은 시나리오는 에디터를 켜고 돕니다.
         */
        static test::AppRunOutcome runScenarioOnBackend( const sw::ResourceID& resourceID, const utf8* pSwitch, sw::string_view extraArguments )
        {
            const sw::string& scenarioPath = resourceID.str();
            sw::string        scenarioArguments{ extraArguments };
            const sw::string  launchArguments = readLaunchArguments( resourceID );
            if ( launchArguments.empty() == false )
                scenarioArguments += " " + launchArguments;
            if ( requiresEditor( resourceID ) )
                scenarioArguments += " " + makeEditorArguments( scenarioPath, pSwitch );
            sw::string scenarioLines;
            return test::AppTestUtil::runScenario( scenarioPath, pSwitch, scenarioLines, scenarioArguments );
        }

        /** @brief 케이스 `AppScenarioTest.[<게임>.]<시나리오>.<백엔드>` 의 본문입니다. 이 기계에서 돌지 못했으면(건너뜀 · 요구하지 않은 백엔드가 못 섬) 케이스를 건너뜁니다. */
        static void runScenarioCase( const sw::ResourceID& resourceID, const utf8* pSwitch, const sw::string& extraArguments )
        {
            const test::AppRunOutcome outcome = runScenarioOnBackend( resourceID, pSwitch, extraArguments );
            if ( test::hasRunHere( outcome ) == false )
                SW_TEST_SKIP( outcome == test::AppRunOutcome::BackendUnusable ? "the backend cannot stand up on this machine and is not required (--required_backends)" : "the scenario skipped itself or needs a development build" );
        }

        /**
         * @brief 케이스 이름에 쓰는 시나리오 이름입니다 — 묶음 이름(`vfxgpulights`). 활성 게임과 엔진의 시나리오 가운데 같은 이름이 또 있으면 폴더까지 넣어 `_` 로 잇습니다
         *        (`game_empty_automation_vfxgpulights`) — 이름이 겹치면 두 케이스가 한 필터에 같이 걸린다.
         */
        static sw::string makeCaseLabel( const sw::string& scenarioPath, const sw::vector<sw::string>& listScenario )
        {
            sw::string label = getGroupName( scenarioPath );
            size_t     count = 0;
            for ( const sw::string& other : listScenario )
            {
                count += getGroupName( other ) == label ? 1 : 0;
            }
            if ( count > 1 )
            {
                label = scenarioPath.substr( 0, scenarioPath.size() - sw::string( ".scenario.xml" ).size() );
                std::replace( label.begin(), label.end(), '/', '_' );
            }
            return label;
        }

        /**
         * @brief 시나리오마다 백엔드마다 케이스 `AppScenarioTest.<이름 머리><시나리오>.<백엔드>` 를 더합니다.
         * @param namePrefix 이름 머리(`HarvestValley.` — 활성 게임은 빈 글)
         * @param extraArguments 모든 판에 붙는 인자(게임 팩은 `-game=<게임>`)
         * @param caseSeconds 케이스 하나의 예상 비용(초) — 샤드 배정이 쓴다(`TestCaseInfo::_expectedSeconds`)
         */
        static void addScenarioCases( test::TestCatalog& catalog, const sw::string& namePrefix, const sw::vector<sw::string>& listScenario, const sw::string& extraArguments,
                                      float64 caseSeconds )
        {
            const sw::vector<const utf8*> listBackendSwitch = collectBackendSwitches();
            for ( const sw::string& scenarioPath : listScenario )
            {
                const sw::string label    = makeCaseLabel( scenarioPath, listScenario );
                const bool       bDesktop = usesDesktop( scenarioPath );
                for ( const utf8* pSwitch : listBackendSwitch )
                {
                    const sw::string           caseName = namePrefix + label + "." + getBackendName( pSwitch );
                    const sw::Delegate<void()> body     = [scenarioPath, pSwitch, extraArguments]()
                    {
                        runScenarioCase( sw::ResourceID{ scenarioPath }, pSwitch, extraArguments );
                    };
                    if ( bDesktop )
                        catalog.addDesktopCase( "AppScenarioTest", caseName.c_str(), body, caseSeconds );
                    else
                        catalog.addCase( "AppScenarioTest", caseName.c_str(), body, caseSeconds );
                }
            }
        }

        /**
         * @brief 시나리오가 데스크톱 전역 상태를 쓰면 true 입니다 — 혼합 입력(`input="mixed"`)이 그 표시다(실제 OS 입력 · 창 포커스 · 커서 가두기를 받는다,
         *        `VirtualInputMode::Mixed`). 그 케이스는 `<타깃>_Desktop` 에서 혼자 돈다(`SuiteKind::Desktop`). 못 읽으면 false — 읽기 오류는 케이스 본문이 낸다.
         */
        static bool usesDesktop( const sw::string& scenarioPath )
        {
            sw::AutomationScenario scenario;
            sw::string             error;
            return scenario.loadFromPath( sw::ResourceID{ scenarioPath }, error ) && scenario.getInputMode() == sw::VirtualInputMode::Mixed;
        }

        /**
         * @brief 케이스를 만듭니다(`SW_TEST_CASE_GENERATOR`) — 엔진 · 활성 게임의 시나리오는 `AppScenarioTest.<시나리오>.<백엔드>`, 같은 빌드가 지은 게임 팩(`Bin/Games/<게임>/`)의
         *        시나리오는 `-game=<게임>` 으로 도는 `AppScenarioTest.<게임>.<시나리오>.<백엔드>` 입니다.
         * @details 시나리오 폴더를 훑어 만들므로 파일을 두면 케이스가 자동으로 생긴다. 케이스는 서로 독립이라 하나가 져도 나머지는 돌고(`--test_filter=AppScenarioTest.vfxgpulights.*` 로 하나만도),
         *          시간은 케이스마다 보고된다. 백엔드 이름은 `dx11` · `dx12` · `vk` · `gl`(배포본은 `default`). 게임 팩 케이스는 Shipping(`SourceResources` 없음)에서는 만들지 않는다.
         *          예상 비용은 옛 한 덩어리 케이스의 비용(정책 표, Debug)을 케이스 수로 나눈 값이다.
         */
        static void generateScenarioCases( test::TestCatalog& catalog )
        {
            // 옛 `EveryScenarioPassesOnEveryBackend` 98 s · `EveryGamePackScenarioPassesOnEveryBackend` 286 s(Test/TestPolicy.json 비용표였다).
            constexpr float64 kActiveScenarioTotalSeconds   = 98.0;
            constexpr float64 kGamePackScenarioTotalSeconds = 286.0;

            const size_t                 backendCount = collectBackendSwitches().size();
            const sw::vector<sw::string> listScenario = collectGameScenarioPaths();
            if ( listScenario.empty() == false )
                addScenarioCases( catalog, {}, listScenario, {}, kActiveScenarioTotalSeconds / static_cast<float64>( listScenario.size() * backendCount ) );

            if ( test::isBuildFeatureAvailable( test::BuildFeature::SourceResources ) == false )
                return; // 게임 팩 프리셋(Config/Game)과 Bin/Games 는 Shipping 에 없다
            sw::vector<sw::pair<sw::string, sw::vector<sw::string>>> listPackScenario;
            size_t                                                   packScenarioCount = 0; // 활성 게임 몫도 센다 — 옛 286 s 가 그 몫을 포함해 쟀으므로 케이스당 비용은 그대로 둔다
            for ( const sw::string& gameName : test::AppTestUtil::collectBuiltGamePacks() )
            {
                const sw::string packRoot = test::AppTestUtil::readPackRoot( gameName );
                if ( packRoot.empty() )
                {
                    // 케이스가 없으면 조용히 빠진다 — 이름이 있는 실패 케이스로 남긴다.
                    catalog.addCase( "AppScenarioTest", ( gameName + ".PackRootIsMissing" ).c_str(), [gameName]()
                    {
                        SW_EXPECT_TRUE_MSG( false, ( "no _packRoot in the preset of " + gameName ).c_str() );
                    } );
                    continue;
                }
                listPackScenario.push_back( { gameName, collectScenarioPaths( { packRoot + "/automation" } ) } );
                packScenarioCount += listPackScenario.back().second.size();
            }
            for ( const auto& [gameName, listPack] : listPackScenario )
            {
                if ( isActiveGame( gameName ) )
                    continue; // 위 목록이 이미 같은 시나리오를 같은 모듈 경로로 돈다(`isActiveGame`)
                addScenarioCases( catalog, gameName + ".", listPack, test::AppTestUtil::makeValueArgument( sw::CommandLineArgument::GAME, gameName ), kGamePackScenarioTotalSeconds / static_cast<float64>( packScenarioCount * backendCount ) );
            }
        }

        /**
         * @brief @p gameName 이 빌드의 활성 게임이면 true 입니다 — 활성 게임의 팩 시나리오는 게임 팩 구간이 아니라 활성 게임 구간(`collectGameScenarioPaths`)이 돈다.
         * @details `GameSelection::select` 는 `-game=<활성 게임>` 을 인자 없는 실행과 같게 푼다(둘 다 `Bin/Modules/SWGame`, 팩 폴더로 가지 않는다). 시나리오 폴더도 같다
         *          (`readPackRoot( getName() )/automation`). 같은 판을 `-game=` 만 붙여 한 번 더 돌면 같은 것을 두 번 확인하는 낭비라 게임 팩 구간에서 뺀다.
         */
        static bool isActiveGame( const sw::string& gameName )
        {
            return gameName == sw::GameSelection::getName();
        }

        /** @brief 시나리오를 백엔드마다 돌려 종료 코드 0 을 단언하고 기록 줄(`<Record>`)을 백엔드끼리 견줍니다. 돌린(건너뛰지 않은) 수를 돌려줍니다. */
        static uint32 runEveryBackend( const sw::vector<sw::string>& listScenario, sw::string_view extraArguments )
        {
            const sw::vector<const utf8*> listBackendSwitch = collectBackendSwitches();
            uint32                        ranCount          = 0;
            for ( const sw::string& scenarioPath : listScenario )
            {
                sw::vector<sw::string> listFirstRecord;
                const utf8*            pFirstSwitch = nullptr;
                for ( const utf8* pSwitch : listBackendSwitch )
                {
                    const test::AppRunOutcome outcome = runScenarioOnBackend( sw::ResourceID{ scenarioPath }, pSwitch, extraArguments );
                    if ( test::hasRunHere( outcome ) == false )
                        continue;
                    ++ranCount;
                    // 결정적 재생: `<Record>` 줄은 백엔드마다 같아야 한다. 처음 돈 백엔드를 기준으로 비교한다.
                    const sw::vector<sw::string> listRecord = test::AppTestUtil::readReportRecords( test::AppTestUtil::makeReportPath( scenarioPath, pSwitch ) );
                    if ( pFirstSwitch == nullptr )
                    {
                        pFirstSwitch    = pSwitch;
                        listFirstRecord = listRecord;
                        continue;
                    }
                    SW_EXPECT_TRUE_MSG( compareRecords( listFirstRecord, listRecord, pFirstSwitch, pSwitch, scenarioPath ),
                                        ( scenarioPath + ": records differ between " + pFirstSwitch + " and " + pSwitch ).c_str() );
                }
            }
            return ranCount;
        }

        /** @brief 두 백엔드의 기록 줄이 같으면 true 입니다. 다르면 다른 줄마다 로그를 남깁니다(어느 프레임의 어느 탐침이 어떻게 달랐는지). */
        static bool compareRecords( const sw::vector<sw::string>& listExpected, const sw::vector<sw::string>& listActual, const utf8* pExpectedSwitch,
                                    const utf8* pActualSwitch, const sw::string& scenarioPath )
        {
            bool         bSame     = listExpected.size() == listActual.size();
            const size_t lineCount = listExpected.size() > listActual.size() ? listExpected.size() : listActual.size();
            for ( size_t index = 0; index < lineCount; ++index )
            {
                const sw::string expected = index < listExpected.size() ? listExpected[index] : sw::string( "(none)" );
                const sw::string actual   = index < listActual.size() ? listActual[index] : sw::string( "(none)" );
                if ( expected == actual )
                    continue;
                bSame = false;
                SW_LOG_ERROR( "[AppScenarioTest] %# record %#: %# '%#' vs %# '%#'", scenarioPath.c_str(), static_cast<uint32>( index ), pExpectedSwitch, expected.c_str(),
                              pActualSwitch, actual.c_str() );
            }
            return bSame;
        }

#if !defined( SW_SHIPPING )
        // 아래는 에디터 시나리오를 빈 에디터 상태 폴더(`-gv_editorStateDir`)로 도는 도우미다 — 사용자의 `Saved/Editor`(레이아웃 · 테마 · 최근 씬)와 무관하게 시작한다.
        // 이름이 `<묶음>.<n>.scenario.xml` 인 파일은 묶음 하나로 같은 상태 폴더를 이어 쓴다(이름순). 첫 실행이 저장한 레이아웃 · 설정을
        // 다음 실행이 읽는 "저장된 상태" 분기를 그렇게 확인한다. 묶음 이름이 없는 파일은 혼자 한 묶음이다.
        /** @brief 체인에서 시나리오 몇 개마다 모듈 리로드를 끼우는지입니다. */
        static constexpr uint32 kReloadEvery = 6;

        /** @brief 묶음마다 빈 상태 폴더를 만들어 묶음의 시나리오를 백엔드 @p pSwitch(`-dx12` …)로 차례로 돌립니다. 돌린(건너뛰지 않은) 수를 돌려줍니다. */
        static uint32 runEditorGroups( const sw::vector<sw::string>& listScenario, const utf8* pSwitch )
        {
            uint32 ranCount = 0;
            size_t begin    = 0;
            while ( begin < listScenario.size() )
            {
                const sw::string group = getGroupName( listScenario[begin] );
                size_t           end   = begin + 1;
                while ( end < listScenario.size() && getGroupName( listScenario[end] ) == group )
                {
                    ++end;
                }
                const sw::string stateFolder = test::AppTestUtil::makeRunPath( makeEditorStateRelativePath( group + "_" + ( pSwitch + 1 ) ) );
                SW_EXPECT_TRUE( stateFolder.empty() == false && sw::FileUtil::removeDirectory( stateFolder ) );
                const sw::string arguments = makeEditorStateArguments( stateFolder );
                for ( size_t index = begin; index < end; ++index )
                {
                    sw::string                scenarioLines;
                    const test::AppRunOutcome outcome = test::AppTestUtil::runScenario( listScenario[index], pSwitch, scenarioLines, arguments );
                    if ( test::hasRunHere( outcome ) == false )
                        break; // 묶음의 뒤 실행은 앞 실행이 남긴 상태를 본다 — 앞이 건너뛰면 뒤도 건너뛴다
                    ++ranCount;
                }
                begin = end;
            }
            return ranCount;
        }

        /** @brief 파일 이름이 `<묶음>.<n>.scenario.xml` 이면 true 입니다(앞 실행이 남긴 저장 상태를 이어 읽는 묶음 — 프로세스를 나눠 돌린다). */
        static bool isNumberedGroupMember( const sw::string& scenarioPath )
        {
            const sw::string fileName = sw::FileUtil::getFileNamePart( scenarioPath );
            size_t           dotCount = 0;
            for ( const utf8 ch : fileName )
            {
                dotCount += ch == '.' ? 1 : 0;
            }
            return dotCount > 2;
        }

        /** @brief 시나리오가 루트에 `isolated="true"` 를 적었으면 true 입니다(새 프로세스에서만 돈다). 못 읽으면 격리로 봐 그 시나리오가 제 프로세스에서 읽기 오류를 내게 한다. */
        static bool isIsolated( const sw::ResourceID& resourceID )
        {
            sw::AutomationScenario scenario;
            sw::string             error;
            return scenario.loadFromPath( resourceID, error ) == false || scenario.getTestOptions()._bIsolated;
        }

        /**
         * @brief 한 프로세스로 묶을 시나리오 순서를 만듭니다 — 시나리오마다 앞에 상태 초기화를 두고, @p reloadEvery 개마다 모듈 리로드를 끼웁니다.
         * @details 리로드는 게임 모듈 → 키트(서버 전용 키트 연쇄) → 키트 + 에디터 확장 → 에디터 모듈 순서로 돈다(`chain/reload*.scenario.xml`). 리로드 뒤에도 초기화가 먼저 도므로
         *          다음 시나리오는 새 이미지 위에서 시작한다. 실제 개발에서 편집 중에 모듈을 다시 올리는 것과 같은 조건이다.
         */
        static sw::vector<sw::string> makeChain( const sw::vector<sw::string>& listScenario, uint32 reloadEvery )
        {
            // reloadEvery 가 0 이면 리로드를 끼우지 않는다.
            constexpr const utf8*  kReset       = "engine/automation/editor/chain/reset.scenario.xml";
            constexpr const utf8*  kArrReload[] = { "engine/automation/editor/chain/reloadgame.scenario.xml", "engine/automation/editor/chain/reloadkit.scenario.xml",
                                                    "engine/automation/editor/chain/reloadkitext.scenario.xml", "engine/automation/editor/chain/reloadeditor.scenario.xml" };
            sw::vector<sw::string> listChain;
            uint32                 reloadIndex = 0;
            for ( size_t index = 0; index < listScenario.size(); ++index )
            {
                const bool bInsertReload = index > 0 && reloadEvery > 0 && index % reloadEvery == 0;
                if ( bInsertReload )
                {
                    listChain.push_back( kArrReload[reloadIndex % SW_COUNT_OF( kArrReload )] );
                    ++reloadIndex;
                }
                listChain.push_back( kReset );
                listChain.push_back( listScenario[index] );
            }
            return listChain;
        }

        /** @brief `engine/automation/editor` 의 시나리오를 백엔드 하나(@p pSwitch)로 돌립니다 — 케이스 본문입니다. */
        static void runEditorCase( const utf8* pSwitch )
        {
            const sw::vector<sw::string> listScenario = AppScenarioTestInternal::collectScenarioPaths( { "engine/automation/editor" } );
            SW_ASSERT_TRUE_MSG( listScenario.empty() == false, "There are no editor scenarios in engine/automation/editor" );

            // 묶을 수 있는 시나리오는 한 프로세스(실행 시간의 대부분이던 기동 · 모듈 로드를 한 번만)로, 나머지(저장 상태 묶음 · 격리 표식)는 프로세스를 나눠 돈다.
            sw::vector<sw::string> listChained;
            sw::vector<sw::string> listSeparate;
            for ( const sw::string& scenarioPath : listScenario )
            {
                if ( isNumberedGroupMember( scenarioPath ) || isIsolated( sw::ResourceID{ scenarioPath } ) )
                    listSeparate.push_back( scenarioPath );
                else
                    listChained.push_back( scenarioPath );
            }

            uint32 ranCount = 0;
            if ( listChained.empty() == false )
            {
                const sw::string backend     = pSwitch[0] == '-' ? sw::string( pSwitch + 1 ) : sw::string( "default" );
                const sw::string stateFolder = test::AppTestUtil::makeRunPath( makeEditorStateRelativePath( "chain_" + backend ) );
                SW_EXPECT_TRUE( stateFolder.empty() == false && sw::FileUtil::removeDirectory( stateFolder ) );
                const sw::string          arguments = makeEditorStateArguments( stateFolder ) + " -scenario-keep-going";
                sw::string                scenarioLines;
                const test::AppRunOutcome outcome = test::AppTestUtil::runScenarioChain( makeChain( listChained, kReloadEvery ), "editor", pSwitch, scenarioLines, arguments );
                if ( test::hasRunHere( outcome ) )
                    ++ranCount;
            }
            ranCount += runEditorGroups( listSeparate, pSwitch );
            if ( ranCount == 0 )
                SW_TEST_SKIP( "no editor scenario could run on this machine" );
        }
#endif
    };
} // namespace

// 엔진 · 활성 게임의 시나리오는 시나리오 × 백엔드마다 독립 케이스 `AppScenarioTest.<시나리오>.<백엔드>` 다(`generateScenarioCases` — 폴더를 훑어 실행 시작에 만든다).
// 같은 빌드가 지은 게임 팩(`Bin/Games/<게임>/`, `SW_GAMES`)의 시나리오는 `-game=<게임>` 으로 도는 `AppScenarioTest.<게임>.<시나리오>.<백엔드>` 다(Shipping 에는 팩이 없어 만들지 않는다).
// 하나가 져도 나머지는 돌고, `--test_filter=AppScenarioTest.vfxgpulights.*` · `AppScenarioTest.HarvestValley.*` 로 골라 돌릴 수 있다. 종료 코드 0 이 통과이고 13 건너뜀 · 요구하지 않은 백엔드의 77 은 건너뜀이다.
// 로그는 케이스의 실행 쓰기 루트(`build/<프리셋>/AppRuns/shard<i>of<n>/<케이스>/`)의 `Saved/Automation/<시나리오>_<백엔드>.log`.
SW_TEST_CASE_GENERATOR( AppScenarioTest, AppScenarioTestInternal::generateScenarioCases );

/**
 * @brief [AppScenarioTest] 엔진 · 활성 게임의 시나리오 목록이 비지 않는다 — 목록이 비면 시나리오별 케이스가 하나도 생기지 않아 아무것도 안 돌고 초록이 된다
 */
SW_TEST_CASE( AppScenarioTest, ScenarioListIsNotEmpty )
{
    SW_EXPECT_TRUE_MSG( AppScenarioTestInternal::collectGameScenarioPaths().empty() == false, "no automation scenario in engine/automation or the active game pack (is the Resource folder found?)" );
}

/**
 * @brief [AppScenarioTest] 같은 빌드가 지은 게임 팩(`Bin/Games/<게임>/`)의 시나리오 목록이 비지 않는다 — 비면 `AppScenarioTest.<게임>.<시나리오>.<백엔드>` 케이스가 하나도 생기지 않아 아무것도 안 돌고 초록이 된다
 * @details 게임 팩을 짓지 않은 빌드(Shipping · `SW_GAMES` 비움)는 건너뛴다.
 */
SW_TEST_CASE( AppScenarioTest, GamePackScenarioListIsNotEmpty )
{
    SW_TEST_REQUIRES_FEATURE( SourceResources ); // 게임 팩 프리셋(Config/Game)과 Bin/Games 는 Shipping 에 없다
    sw::vector<sw::string> listGame;
    for ( const sw::string& gameName : test::AppTestUtil::collectBuiltGamePacks() )
    {
        if ( AppScenarioTestInternal::isActiveGame( gameName ) == false )
            listGame.push_back( gameName ); // 활성 게임은 활성 게임 구간이 돈다
    }
    if ( listGame.empty() )
        SW_TEST_SKIP( "no game packs besides the active game in Bin/Games (Shipping or SW_GAMES is empty)" );
    size_t scenarioCount = 0;
    for ( const sw::string& gameName : listGame )
    {
        const sw::string packRoot = test::AppTestUtil::readPackRoot( gameName );
        SW_EXPECT_TRUE_MSG( packRoot.empty() == false, ( "no _packRoot in the preset of " + gameName ).c_str() );
        if ( packRoot.empty() == false )
            scenarioCount += AppScenarioTestInternal::collectScenarioPaths( { packRoot + "/automation" } ).size();
    }
    SW_EXPECT_TRUE_MSG( scenarioCount > 0, "game packs are built but none has automation scenarios (the per-game cases would not exist)" );
}

/**
 * @brief [AppScenarioTest] `<Record>` 를 가진 시나리오(엔진 · 활성 게임 · 같은 빌드가 지은 게임 팩)는 모든 백엔드가 같은 기록 줄을 낸다
 * @details 시나리오별 케이스는 백엔드 하나씩이라 백엔드끼리 견주지 못한다. 견줌이 필요한 시나리오만 여기서 한 번 더 모든 백엔드로 돈다(종료 코드 판정은 중복이다).
 *          게임 팩 시나리오는 `-game=<게임>` 으로 돈다(게임 팩이 없는 구성 — Shipping — 은 그 부분만 건너뛴다). 그런 시나리오가 없으면 케이스를 건너뛴다.
 */
SW_TEST_CASE( AppScenarioTest, RecordsMatchAcrossBackends )
{
    sw::vector<sw::string> listRecordScenario;
    for ( const sw::string& scenarioPath : AppScenarioTestInternal::collectGameScenarioPaths() )
    {
        if ( AppScenarioTestInternal::declaresRecord( sw::ResourceID{ scenarioPath } ) )
            listRecordScenario.push_back( scenarioPath );
    }
    uint32 ranCount      = listRecordScenario.empty() ? 0 : AppScenarioTestInternal::runEveryBackend( listRecordScenario, {} );
    uint32 scenarioCount = static_cast<uint32>( listRecordScenario.size() );

    if ( test::skipPartIfFeatureMissing( test::BuildFeature::SourceResources, "game pack scenarios that declare <Record>" ) == false )
    {
        for ( const sw::string& gameName : test::AppTestUtil::collectBuiltGamePacks() )
        {
            if ( AppScenarioTestInternal::isActiveGame( gameName ) )
                continue; // 활성 게임의 시나리오는 위 구간이 이미 `<Record>` 를 견줬다
            const sw::string packRoot = test::AppTestUtil::readPackRoot( gameName );
            SW_EXPECT_TRUE_MSG( packRoot.empty() == false, ( "no _packRoot in the preset of " + gameName ).c_str() );
            if ( packRoot.empty() )
                continue;
            sw::vector<sw::string> listPackRecord;
            for ( const sw::string& scenarioPath : AppScenarioTestInternal::collectScenarioPaths( { packRoot + "/automation" } ) )
            {
                if ( AppScenarioTestInternal::declaresRecord( sw::ResourceID{ scenarioPath } ) )
                    listPackRecord.push_back( scenarioPath );
            }
            scenarioCount += static_cast<uint32>( listPackRecord.size() );
            ranCount += listPackRecord.empty() ? 0 : AppScenarioTestInternal::runEveryBackend( listPackRecord, test::AppTestUtil::makeValueArgument( sw::CommandLineArgument::GAME, gameName ) );
        }
    }
    if ( scenarioCount == 0 )
        SW_TEST_SKIP( "no scenario of the engine, this game or a built game pack declares <Record>" );
    if ( ranCount == 0 )
        SW_TEST_SKIP( "no scenario could run on this machine" );
}

/**
 * @brief [AppScenarioTest] 시나리오로 돌린 플레이를 녹화(`-scenario-record`)하고 그 녹화를 다시 돌리면 끝 프레임의 검사점이 같다 — 백엔드마다 케이스 하나(`.dx12` …)
 * @details 녹화는 시나리오가 낸 가상 입력과 끝 프레임의 탐침 값(`-scenario-record-probes`)을 담는다. 같은 입력 · 같은 고정 틱이면 같은 상태가 나와야 한다(결정적 재생).
 *          HarvestValley 팩의 `actions` 시나리오를 쓴다(농부 위치, 간 타일 수, 빙의). 팩을 빌드하지 않은 구성(Shipping)은 건너뛴다. 예상 비용은 옛 한 케이스 24 s 를 넷으로 나눈 값이다.
 */
SW_TEST_CASE_PER_PARAMETER( AppScenarioTest, RecordedPlayReplaysOnEveryBackend, AppScenarioTestInternal::collectBackendParameters, 6.0 )
{
    SW_TEST_REQUIRES_FEATURE( SourceResources ); // 게임 팩 프리셋(Config/Game)과 Bin/Games 는 Shipping 에 없다
    const sw::vector<sw::string> listGame = test::AppTestUtil::collectBuiltGamePacks();
    if ( std::find( listGame.begin(), listGame.end(), sw::string( "HarvestValley" ) ) == listGame.end() )
        SW_TEST_SKIP( "the HarvestValley game pack is not built (Shipping or SW_GAMES)" );
    constexpr const utf8* kSource = "game/harvestvalley/automation/actions.scenario.xml";
    constexpr const utf8* kProbes = "Farm.FarmerX,Farm.TilledCount,Object.Z@Farmer,Pawn.Controller@Farmer";

    // App 에는 작업 폴더(실행 쓰기 루트) 기준 상대 경로를 준다(`-scenario-record` 는 `Saved/Automation/` 아래 상대 경로만 받는다). 테스트가 볼 때는 그 루트의 절대 경로다.
    const sw::string recordedPath     = "Saved/Automation/Recorded/roundtrip_" + AppScenarioTestInternal::getBackendName( pParameter ) + ".scenario.xml";
    const sw::string recordedDiskPath = test::AppTestUtil::makeRunPath( recordedPath );
    (void)sw::FileUtil::tryRemoveFile( recordedDiskPath ); // 지난 실행의 녹화를 읽지 않게 지운다
    sw::string                sourceLines;
    const test::AppRunOutcome sourceOutcome =
        test::AppTestUtil::runScenario( kSource, pParameter, sourceLines, "-game=HarvestValley -scenario-record=" + recordedPath + " -scenario-record-probes=" + kProbes );
    if ( test::hasRunHere( sourceOutcome ) == false )
        SW_TEST_SKIP( "the backend cannot stand up on this machine or the scenario skipped itself" );
    if ( sourceOutcome == test::AppRunOutcome::Failed )
        return; // 실패는 runScenario 가 기록했다 — 녹화가 없으니 다시 돌릴 것이 없다
    SW_EXPECT_TRUE_MSG( sw::FileUtil::exists( recordedDiskPath ), ( recordedDiskPath + " was not written" ).c_str() );
    sw::string replayLines;
    // 쓰기는 디스크 경로, 읽기는 붙인 도메인의 id 다(시나리오 입구는 리소스 id 만 받는다).
    const sw::string          replayID      = sw::ScenarioRecorder::makeReplayID( recordedPath );
    const test::AppRunOutcome replayOutcome = test::AppTestUtil::runScenario( replayID, pParameter, replayLines, "-game=HarvestValley" );
    // 녹화를 낸 백엔드가 다시 돌 때 서지 못하거나 건너뛰면 왕복을 확인하지 못한 것이다.
    SW_EXPECT_TRUE_MSG( test::hasRunHere( replayOutcome ), ( recordedPath + " " + pParameter + " did not run after the recording run did\n" + replayLines ).c_str() );
}

/**
 * @brief [AppScenarioTest] 시나리오를 한 프로세스에서 이어 돌린다 — 게임 모듈 리로드 → 렌더 확인 → 다시 리로드가 통과하고, 단계마다 보고가 남는다(백엔드마다 케이스 하나)
 * @details 체인은 앞 단계의 씬 · 모듈 상태를 이어받는다(`-scenario=a,b,c`). 두 번째 리로드가 첫 번째의 완료 수에 속지 않고(`Module.ReloadPending`) 제 리로드를 기다리는지, 사이의 렌더 시나리오가
 *          리로드 뒤 새 이미지로 그리는지 본다. 한 프로세스를 공유하는 것은 체인 안의 단계끼리라 백엔드로 나눠도 목적이 그대로다(프로세스는 백엔드마다 하나였다).
 *          개발 명령이 없는 구성(Shipping)은 13 으로 건너뛴다. 예상 비용은 옛 한 케이스 16 s 를 넷으로 나눈 값이다.
 */
SW_TEST_CASE_PER_PARAMETER( AppScenarioTest, ChainedScenariosShareOneProcess, AppScenarioTestInternal::collectBackendParameters, 4.0 )
{
    const sw::vector<sw::string> listScenario{ "engine/automation/hotreload.scenario.xml", "engine/automation/runner.scenario.xml", "engine/automation/hotreload.scenario.xml" };
    sw::string                   scenarioLines;
    const test::AppRunOutcome    outcome = test::AppTestUtil::runScenarioChain( listScenario, "reload_render_reload", pParameter, scenarioLines );
    if ( test::hasRunHere( outcome ) == false )
        SW_TEST_SKIP( "the backend cannot stand up on this machine or the chain skipped itself" );
    const sw::string label = sw::string( "chain " ) + pParameter + "\n" + scenarioLines;
    SW_EXPECT_TRUE_MSG( scenarioLines.find( "chain stage 3/3 PASS" ) != sw::string::npos, label.c_str() );
    // 어림값 문턱이 든 단계(runner)가 있으면 체인은 PASS 대신 UNCONFIRMED 로 끝난다(실패 0 은 같다) — 둘 다 단언이 모두 맞은 끝이다.
    const bool bChainHeld = scenarioLines.find( "chain PASS (3 stage(s), 0 failed, 0 skipped, 0 not run)" ) != sw::string::npos ||
                            scenarioLines.find( "chain UNCONFIRMED (3 stage(s), 0 failed, 0 skipped, 0 not run)" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bChainHeld, label.c_str() );
}

// 에디터 작업 흐름 시나리오(`engine/automation/editor` 의 `.scenario.xml`)를 에디터를 켠 실행(`-EnableEditor`)으로 돌린다. 에디터 동작을 바꿨으면 이 시나리오로 확인한다.
// 실행마다 빈 에디터 상태 폴더(실행 쓰기 루트의 `Saved/Automation/EditorState/<묶음>_<백엔드>`)로 시작하므로 사용자의 `Saved/Editor` 를 읽지도 쓰지도 않는다.
// `<묶음>.<n>.scenario.xml` 은 한 폴더를 이어 써 저장된 상태를 읽는 분기를 본다. 배포본에는 에디터가 없다.
// 백엔드마다 한 케이스다 — 시나리오 60 개 × 백엔드 넷을 한 케이스에 두면 그 케이스 하나가 18 분을 넘겨 `AppTest_HostOnly` 시한(20 분)을 채웠다.
// 케이스로 나눠야 `HOST_SHARDS` 가 조각마다 시한을 준다. 체인은 한 프로세스라 케이스 안에서 시나리오별로 가를 수 없지만, 단계(시나리오)마다 보고(`chain_editor_<백엔드>.<단계>.json`)가 남아
// 실패 메시지가 `chain sub-results:` 로 통과하지 못한 시나리오마다 결과 · 종료 코드 · `failures[]` · `unhandledLogs[]` 를 나열한다(`AppTestUtil::runScenarioArgument`).

/**
 * @brief [AppScenarioTest] 에디터 시나리오가 DX12 에서 통과한다
 */
SW_TEST_CASE( AppScenarioTest, EditorScenariosPassOnDirectX12 )
{
#if defined( SW_SHIPPING )
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // the shipping build has no editor
#else
    AppScenarioTestInternal::runEditorCase( "-dx12" );
#endif
}

/**
 * @brief [AppScenarioTest] 에디터 시나리오가 DX11 에서 통과한다
 */
SW_TEST_CASE( AppScenarioTest, EditorScenariosPassOnDirectX11 )
{
#if defined( SW_SHIPPING )
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // the shipping build has no editor
#else
    AppScenarioTestInternal::runEditorCase( "-dx11" );
#endif
}

/**
 * @brief [AppScenarioTest] 에디터 시나리오가 Vulkan 에서 통과한다
 */
SW_TEST_CASE( AppScenarioTest, EditorScenariosPassOnVulkan )
{
#if defined( SW_SHIPPING )
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // the shipping build has no editor
#else
    AppScenarioTestInternal::runEditorCase( "-vk" );
#endif
}

/**
 * @brief [AppScenarioTest] 에디터 시나리오가 OpenGL 에서 통과한다
 */
SW_TEST_CASE( AppScenarioTest, EditorScenariosPassOnOpenGL )
{
#if defined( SW_SHIPPING )
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // the shipping build has no editor
#else
    AppScenarioTestInternal::runEditorCase( "-gl" );
#endif
}
