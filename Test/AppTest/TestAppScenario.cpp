#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/CookContract.gen.h"

// 실제 App.exe 를 시나리오마다 · 백엔드마다 띄운다 — GPU · 창 · 셰이더가 필요하다. CI 러너엔 없다.
SW_TEST_REQUIRES_HOST( AppScenarioTest, "launches App.exe per automation scenario and backend - needs a GPU and a window" );

// ------------------------------------------------------------------------------
// AppScenarioTest — 자동화 시나리오(`Resource/<영역>/automation/*.scenario.xml`)를 백엔드마다 돌려 종료 코드를 본다
//
// 시나리오 파일을 놓기만 하면 돈다(파일 목록을 CMake 에 적지 않는다). 게임은 프리셋마다 하나(`SW_ACTIVE_GAME`)이므로 그 프리셋의
// `AppTest_HostOnly` 가 **엔진 시나리오 + 그 게임 팩의 시나리오**를 돈다. 에디터 시나리오(`engine/automation/editor/`)는 `-EnableEditor` 로 돈다.
// 종료 코드 13(건너뜀 — 전경 창을 못 얻음 등) · 77(이 기계에 없는 백엔드)은 건너뛴다. 언리얼 Gauntlet 이 프로세스를 띄워 결과 코드 · 로그를 모으는 것과 같은 자리다.
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

        /** @brief 시나리오를 백엔드마다 돌려 종료 코드 0 을 단언합니다. 돌린(건너뛰지 않은) 수를 돌려줍니다. */
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
                    sw::string       scenarioLines;
                    const int32      exitCode = test::AppTestUtil::runScenario( scenarioPath, pSwitch, scenarioLines, extraArguments );
                    const sw::string label    = scenarioPath + " " + pSwitch + " -> exit " + sw::to_string( exitCode ) + "\n" + scenarioLines;
                    SW_EXPECT_TRUE_MSG( exitCode != test::AppTestUtil::kNotLaunchedExitCode, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)에 App 이 있습니까?" );
                    if ( test::AppTestUtil::isSkippedExitCode( exitCode ) )
                    {
                        SW_LOG_INFO( "[AppScenarioTest] skipped %#", label.c_str() );
                        continue;
                    }
                    SW_EXPECT_TRUE_MSG( exitCode == 0, label.c_str() );
                    ++ranCount;
                    // 결정적 재생: `<Record>` 줄은 백엔드마다 같아야 한다. 처음 돈 백엔드를 기준으로 견준다.
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
    };

#if !defined( SW_SHIPPING )
    /**
     * @struct EditorScenarioRunInternal
     * @brief 에디터 시나리오를 빈 에디터 상태 폴더(`-gv_editorStateDir`)로 돌립니다 — 사용자의 `Saved/Editor`(레이아웃 · 테마 · 최근 씬)와 무관하게 시작한다.
     * @details 이름이 `<묶음>.<n>.scenario.xml` 인 파일은 묶음 하나로 같은 상태 폴더를 이어 쓴다(이름순). 첫 실행이 저장한 레이아웃 · 설정을
     *          다음 실행이 읽는 "저장된 상태" 분기를 그렇게 확인한다. 묶음 이름이 없는 파일은 혼자 한 묶음이다.
     */
    struct EditorScenarioRunInternal
    {
        /** @brief 체인에서 시나리오 몇 개마다 모듈 리로드를 끼우는지입니다. */
        static constexpr uint32 kReloadEvery = 6;

        /** @brief 시나리오 경로의 묶음 이름(파일 이름의 첫 `.` 앞)입니다. */
        static sw::string getGroupName( const sw::string& scenarioPath )
        {
            sw::string   fileName = sw::FileUtil::getFileNamePart( scenarioPath );
            const size_t dot      = fileName.find( '.' );
            if ( dot != sw::string::npos )
                fileName.resize( dot );
            return fileName;
        }

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
                sw::string stateFolder;
                const bool bAbsolute = sw::FileUtil::makeAbsolutePath( "Saved/Automation/EditorState/" + group + "_" + ( pSwitch + 1 ), stateFolder );
                SW_EXPECT_TRUE( bAbsolute );
                SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( stateFolder ) );
                const sw::string arguments = "-EnableEditor -gv_editorStateDir=" + stateFolder;
                for ( size_t index = begin; index < end; ++index )
                {
                    sw::string       scenarioLines;
                    const int32      exitCode = test::AppTestUtil::runScenario( listScenario[index], pSwitch, scenarioLines, arguments );
                    const sw::string label    = listScenario[index] + " " + pSwitch + " -> exit " + sw::to_string( exitCode ) + "\n" + scenarioLines;
                    SW_EXPECT_TRUE_MSG( exitCode != test::AppTestUtil::kNotLaunchedExitCode, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)에 App 이 있습니까?" );
                    if ( test::AppTestUtil::isSkippedExitCode( exitCode ) )
                    {
                        SW_LOG_INFO( "[AppScenarioTest] skipped %#", label.c_str() );
                        break; // 묶음의 뒤 실행은 앞 실행이 남긴 상태를 본다 — 앞이 건너뛰면 뒤도 건너뛴다
                    }
                    SW_EXPECT_TRUE_MSG( exitCode == 0, label.c_str() );
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
        static bool isIsolated( const sw::string& scenarioPath )
        {
            sw::AutomationScenario scenario;
            sw::string             error;
            return scenario.loadFromPath( scenarioPath, error ) == false || scenario.isIsolated();
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
            SW_ASSERT_TRUE_MSG( listScenario.empty() == false, "engine/automation/editor 에 에디터 시나리오가 없습니다" );

            // 묶을 수 있는 시나리오는 한 프로세스(실행 시간의 대부분이던 기동 · 모듈 로드를 한 번만)로, 나머지(저장 상태 묶음 · 격리 표식)는 프로세스를 나눠 돈다.
            sw::vector<sw::string> listChained;
            sw::vector<sw::string> listSeparate;
            for ( const sw::string& scenarioPath : listScenario )
            {
                if ( isNumberedGroupMember( scenarioPath ) || isIsolated( scenarioPath ) )
                    listSeparate.push_back( scenarioPath );
                else
                    listChained.push_back( scenarioPath );
            }

            uint32 ranCount = 0;
            if ( listChained.empty() == false )
            {
                const sw::string backend = pSwitch[0] == '-' ? sw::string( pSwitch + 1 ) : sw::string( "default" );
                sw::string       stateFolder;
                SW_EXPECT_TRUE( sw::FileUtil::makeAbsolutePath( "Saved/Automation/EditorState/chain_" + backend, stateFolder ) );
                SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( stateFolder ) );
                const sw::string arguments = "-EnableEditor -scenario-keep-going -gv_editorStateDir=" + stateFolder;
                sw::string       scenarioLines;
                const int32      exitCode = test::AppTestUtil::runScenarioChain( makeChain( listChained, kReloadEvery ), "editor", pSwitch, scenarioLines, arguments );
                const sw::string label    = sw::string( "editor chain " ) + pSwitch + " -> exit " + sw::to_string( exitCode ) + " (log: Saved/Automation/chain_editor_" + backend + ".log)\n" + scenarioLines;
                SW_EXPECT_TRUE_MSG( exitCode != test::AppTestUtil::kNotLaunchedExitCode, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)에 App 이 있습니까?" );
                if ( test::AppTestUtil::isSkippedExitCode( exitCode ) )
                    SW_LOG_INFO( "[AppScenarioTest] skipped %#", label.c_str() );
                else
                {
                    SW_EXPECT_TRUE_MSG( exitCode == 0, label.c_str() );
                    ++ranCount;
                }
            }
            ranCount += runEditorGroups( listSeparate, pSwitch );
            if ( ranCount == 0 )
                SW_TEST_SKIP( "no editor scenario could run on this machine" );
        }
    };
#endif
} // namespace

/**
 * @brief [AppScenarioTest] 엔진 · 활성 게임의 시나리오가 모든 백엔드에서 통과한다(종료 코드 0 — 13 건너뜀 · 77 이 기계에 없는 백엔드)
 * @details 사례가 하나라 실패 메시지에 시나리오 · 백엔드가 같이 나온다. 로그는 `Bin/Saved/Automation/<시나리오>_<백엔드>.log`.
 */
SW_TEST_CASE( AppScenarioTest, EveryScenarioPassesOnEveryBackend )
{
    const sw::vector<sw::string> listScenario = AppScenarioTestInternal::collectGameScenarioPaths();
    if ( listScenario.empty() )
        SW_TEST_SKIP( "no automation scenario for the engine or this game" );
    if ( AppScenarioTestInternal::runEveryBackend( listScenario, {} ) == 0 )
        SW_TEST_SKIP( "no scenario could run on this machine" );
}

/**
 * @brief [AppScenarioTest] 같은 빌드가 함께 지은 게임 팩(`Bin/Games/<게임>/`, `SW_GAMES`)마다 그 팩의 시나리오가 `-game=<게임>` 으로 모든 백엔드에서 통과한다
 * @details 게임마다 프리셋 빌드를 따로 두지 않고 한 빌드에서 게임 전부를 확인한다(언리얼 Gauntlet 이 프로젝트를 명령줄로 받는 것과 같다).
 *          엔진 시나리오는 활성 게임의 사례가 이미 돈다. 게임 팩을 짓지 않은 빌드(Shipping · `SW_GAMES` 비움)는 건너뛴다.
 */
SW_TEST_CASE( AppScenarioTest, EveryGamePackScenarioPassesOnEveryBackend )
{
    const sw::vector<sw::string> listGame = test::AppTestUtil::collectBuiltGamePacks();
    if ( listGame.empty() )
        SW_TEST_SKIP( "no game packs in Bin/Games (Shipping or SW_GAMES is empty)" );
    uint32 ranCount = 0;
    for ( const sw::string& gameName : listGame )
    {
        const sw::string packRoot = test::AppTestUtil::readPackRoot( gameName );
        SW_EXPECT_TRUE_MSG( packRoot.empty() == false, ( "no _packRoot in the preset of " + gameName ).c_str() );
        if ( packRoot.empty() )
            continue;
        const sw::vector<sw::string> listScenario = AppScenarioTestInternal::collectScenarioPaths( { packRoot + "/automation" } );
        ranCount += AppScenarioTestInternal::runEveryBackend( listScenario, "-game=" + gameName );
    }
    if ( ranCount == 0 )
        SW_TEST_SKIP( "no game pack scenario could run on this machine" );
}

/**
 * @brief [AppScenarioTest] 시나리오로 돌린 플레이를 녹화(`-scenario-record`)하고 그 녹화를 다시 돌리면 끝 프레임의 검사점이 같다 — 백엔드마다
 * @details 녹화는 시나리오가 낸 가상 입력과 끝 프레임의 탐침 값(`-scenario-record-probes`)을 담는다. 같은 입력 · 같은 고정 틱이면 같은 상태가 나와야 한다(결정적 재생).
 *          HarvestValley 팩의 `actions` 시나리오를 쓴다(농부 자리 · 간 칸 · 빙의). 팩을 짓지 않은 빌드(Shipping)는 건너뛴다.
 */
SW_TEST_CASE( AppScenarioTest, RecordedPlayReplaysOnEveryBackend )
{
    const sw::vector<sw::string> listGame = test::AppTestUtil::collectBuiltGamePacks();
    if ( std::find( listGame.begin(), listGame.end(), sw::string( "HarvestValley" ) ) == listGame.end() )
        SW_TEST_SKIP( "the HarvestValley game pack is not built (Shipping or SW_GAMES)" );
    constexpr const utf8* kSource  = "game/harvestvalley/automation/actions.scenario.xml";
    constexpr const utf8* kProbes  = "Farm.FarmerX,Farm.TilledCount,Object.Z@Farmer,Pawn.Controller@Farmer";
    uint32                ranCount = 0;
    for ( const utf8* pSwitch : AppScenarioTestInternal::collectBackendSwitches() )
    {
        const sw::string backend      = pSwitch[0] == '-' ? sw::string( pSwitch + 1 ) : sw::string( "default" );
        const sw::string recordedPath = "Saved/Automation/Recorded/roundtrip_" + backend + ".scenario.xml";
        (void)sw::FileUtil::tryRemoveFile( recordedPath ); // 지난 실행의 녹화를 읽지 않게 지운다
        sw::string  sourceLines;
        const int32 sourceExit = test::AppTestUtil::runScenario( kSource, pSwitch, sourceLines,
                                                                 "-game=HarvestValley -scenario-record=" + recordedPath + " -scenario-record-probes=" + kProbes );
        if ( test::AppTestUtil::isSkippedExitCode( sourceExit ) )
            continue;
        SW_EXPECT_TRUE_MSG( sourceExit == 0, ( sw::string( kSource ) + " " + pSwitch + " -> exit " + sw::to_string( sourceExit ) + "\n" + sourceLines ).c_str() );
        SW_EXPECT_TRUE_MSG( sw::FileUtil::exists( recordedPath ), ( recordedPath + " was not written" ).c_str() );
        sw::string  replayLines;
        const int32 replayExit = test::AppTestUtil::runScenario( recordedPath, pSwitch, replayLines, "-game=HarvestValley" );
        SW_EXPECT_TRUE_MSG( replayExit == 0, ( recordedPath + " " + pSwitch + " -> exit " + sw::to_string( replayExit ) + "\n" + replayLines ).c_str() );
        ++ranCount;
    }
    if ( ranCount == 0 )
        SW_TEST_SKIP( "no backend could run the round trip on this machine" );
}

/**
 * @brief [AppScenarioTest] 시나리오를 한 프로세스에서 이어 돌린다 — 게임 모듈 리로드 → 렌더 확인 → 다시 리로드가 모든 백엔드에서 통과하고, 단계마다 보고가 남는다
 * @details 체인은 앞 단계의 씬 · 모듈 상태를 이어받는다(`-scenario=a,b,c`). 두 번째 리로드가 첫 번째의 완료 수에 속지 않고(`Module.ReloadPending`) 제 리로드를 기다리는지, 사이의 렌더 시나리오가
 *          리로드 뒤 새 이미지로 그리는지 본다. 프로세스는 백엔드마다 하나다. 개발 명령이 없는 구성(Shipping)은 13 으로 건너뛴다.
 */
SW_TEST_CASE( AppScenarioTest, ChainedScenariosShareOneProcess )
{
    const sw::vector<sw::string> listScenario{ "engine/automation/hotreload.scenario.xml", "engine/automation/runner.scenario.xml", "engine/automation/hotreload.scenario.xml" };
    uint32                       ranCount = 0;
    for ( const utf8* pSwitch : AppScenarioTestInternal::collectBackendSwitches() )
    {
        sw::string       scenarioLines;
        const int32      exitCode = test::AppTestUtil::runScenarioChain( listScenario, "reload_render_reload", pSwitch, scenarioLines );
        const sw::string label    = sw::string( "chain " ) + pSwitch + " -> exit " + sw::to_string( exitCode ) + "\n" + scenarioLines;
        SW_EXPECT_TRUE_MSG( exitCode != test::AppTestUtil::kNotLaunchedExitCode, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)에 App 이 있습니까?" );
        if ( test::AppTestUtil::isSkippedExitCode( exitCode ) )
        {
            SW_LOG_INFO( "[AppScenarioTest] skipped %#", label.c_str() );
            continue;
        }
        SW_EXPECT_TRUE_MSG( exitCode == 0, label.c_str() );
        SW_EXPECT_TRUE_MSG( scenarioLines.find( "chain stage 3/3 PASS" ) != sw::string::npos, label.c_str() );
        SW_EXPECT_TRUE_MSG( scenarioLines.find( "chain PASS (3 stage(s), 0 failed, 0 skipped, 0 not run)" ) != sw::string::npos, label.c_str() );
        ++ranCount;
    }
    if ( ranCount == 0 )
        SW_TEST_SKIP( "no backend could run the chain on this machine" );
}

// 에디터 작업 흐름 시나리오(`engine/automation/editor` 의 `.scenario.xml`)를 에디터를 켠 실행(`-EnableEditor`)으로 돌린다. 에디터 동작을 바꿨으면 이 시나리오로 확인한다.
// 실행마다 빈 에디터 상태 폴더(`Bin/Saved/Automation/EditorState/<묶음>_<백엔드>`)로 시작하므로 사용자의 `Saved/Editor` 를 읽지도 쓰지도 않는다.
// `<묶음>.<n>.scenario.xml` 은 한 폴더를 이어 써 저장된 상태를 읽는 분기를 본다. 배포본에는 에디터가 없다.
// 백엔드마다 한 케이스다 — 시나리오 60 개 × 백엔드 넷을 한 케이스에 두면 그 케이스 하나가 18 분을 넘겨 `AppTest_HostOnly` 시한(20 분)을 채웠다.
// 케이스로 나눠야 `HOST_SHARDS` 가 조각마다 시한을 준다.

/**
 * @brief [AppScenarioTest] 에디터 시나리오가 DX12 에서 통과한다
 */
SW_TEST_CASE( AppScenarioTest, EditorScenariosPassOnDirectX12 )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the shipping build has no editor" );
#else
    EditorScenarioRunInternal::runEditorCase( "-dx12" );
#endif
}

/**
 * @brief [AppScenarioTest] 에디터 시나리오가 DX11 에서 통과한다
 */
SW_TEST_CASE( AppScenarioTest, EditorScenariosPassOnDirectX11 )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the shipping build has no editor" );
#else
    EditorScenarioRunInternal::runEditorCase( "-dx11" );
#endif
}

/**
 * @brief [AppScenarioTest] 에디터 시나리오가 Vulkan 에서 통과한다
 */
SW_TEST_CASE( AppScenarioTest, EditorScenariosPassOnVulkan )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the shipping build has no editor" );
#else
    EditorScenarioRunInternal::runEditorCase( "-vk" );
#endif
}

/**
 * @brief [AppScenarioTest] 에디터 시나리오가 OpenGL 에서 통과한다
 */
SW_TEST_CASE( AppScenarioTest, EditorScenariosPassOnOpenGL )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the shipping build has no editor" );
#else
    EditorScenarioRunInternal::runEditorCase( "-gl" );
#endif
}
