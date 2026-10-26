#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/AutomationExitCode.h"
#include "Engine/Automation/AutomationLogPolicy.h"
#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ConfigConstants.h"

#include <chrono>
#include <thread>

namespace test
{
    namespace
    {
        struct AppTestUtilInternal
        {
            /** @brief 종료 코드 숫자는 `AutomationExitCode.h` 표가 정본이다 — 여기서 숫자를 다시 쓰지 않는다. */
            static constexpr int32 kSkippedExitCode = sw::getAutomationExitCode( sw::AutomationOutcome::Skipped );
            /** @brief 시나리오의 단언은 모두 맞았지만 어림값 문턱이 들어 있다. 실패가 아니라 로그로 미확정을 알리고 통과로 넘긴다. */
            static constexpr int32 kUnconfirmedExitCode = sw::getAutomationExitCode( sw::AutomationOutcome::Unconfirmed );
            /** @brief 시나리오는 통과했지만 출력에 처리하지 않은 Warning · Error 가 있다. 실행기 안의 로그 게이트와 같은 코드로 끝난다. */
            static constexpr int32 kUnhandledLogExitCode = sw::getAutomationExitCode( sw::AutomationOutcome::UnhandledLog );

            /**
             * @brief 명령줄 @p arguments 의 인자가 요구하는 구성 기능이 이 빌드에 있는가 — 없으면 실패를 기록하고 false 를 돌려준다.
             * @details 구성 차이는 케이스가 `SW_TEST_REQUIRES_FEATURE` 로 먼저 선언해야 한다. 선언 없이 이 구성에 없는 기능을 쓰는 인자로 App 을 띄우면
             *          조용한 건너뜀(`SW_TEST_SKIP`)이나 모호한 실행 실패 대신 "선언이 빠졌다" 로 바로 진다. 새 인자는 이 표에 한 줄을 더한다.
             */
            static bool hasFeaturesForArguments( sw::string_view arguments )
            {
                struct ArgumentFeature
                {
                    sw::CommandLineArgument _argument;
                    bool                    _bTakesValue; // `-이름=값` 인자는 `=` 까지 찾는다(철자가 앞머리인 다른 인자와 가르려고)
                    test::BuildFeature      _feature;
                };
                static constexpr ArgumentFeature kArrArgumentFeature[] = {
                    {sw::CommandLineArgument::ENABLE_EDITOR, false, test::BuildFeature::DevelopmentBuild}, // 배포본에는 에디터 모듈이 없다
                    {         sw::CommandLineArgument::GAME,  true,  test::BuildFeature::SourceResources}, // 활성이 아닌 게임 팩은 구운 팩에 없다
                };
                for ( const ArgumentFeature& entry : kArrArgumentFeature )
                {
                    const sw::string token = entry._bTakesValue ? AppTestUtil::makeValueArgument( entry._argument, {} ) : AppTestUtil::makeSwitchArgument( entry._argument );
                    if ( arguments.find( token.c_str() ) == sw::string_view::npos || test::isBuildFeatureAvailable( entry._feature ) )
                        continue;
                    const sw::string message = sw::string( "App 인자 " ) + token + " 는 이 구성에 없는 기능(" + test::getBuildFeatureName( entry._feature ) +
                                               ")이 필요하다 — 케이스 첫 줄에 SW_TEST_REQUIRES_FEATURE 로 선언한다";
                    SW_EXPECT_TRUE_MSG( false, message.c_str() );
                    return false;
                }
                return true;
            }

            /**
             * @brief 명령줄 @p arguments 가 고른 백엔드(`-dx11` · `-dx12` · `-vk` · `-gl` 토큰)입니다. 스위치가 없으면(빌드 기본 · 배포본) false 다.
             * @details 스위치 철자는 쿠킹 표의 명령줄 이름이다(`TestRunOptions::getBackendName`).
             */
            static bool findBackendSwitch( sw::string_view arguments, sw::RHIBackend& outBackend )
            {
                for ( const sw::RHIBackend backend : { sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL } )
                {
                    const sw::string switchText = sw::string( "-" ) + TestRunOptions::getBackendName( backend );
                    size_t           position   = arguments.find( switchText.c_str() );
                    while ( position != sw::string_view::npos )
                    {
                        const size_t end         = position + switchText.size();
                        const bool   bTokenStart = position == 0 || arguments[position - 1] == ' ';
                        const bool   bTokenEnd   = end == arguments.size() || arguments[end] == ' ';
                        if ( bTokenStart && bTokenEnd )
                        {
                            outBackend = backend;
                            return true;
                        }
                        position = arguments.find( switchText.c_str(), end );
                    }
                }
                return false;
            }

            /** @brief 플랫폼별 실행 파일 이름입니다. */
            static const utf8* getAppExecutableName()
            {
#if defined( SW_PLATFORM_WINDOWS )
                return "App.exe";
#else
                return "App";
#endif
            }

            /**
             * @brief 개발 빌드만 아는 `gv_` 전역 변수를 쓰는 실행이면 그 이유를 돌려줍니다. 아니면 빈 글입니다.
             * @details 배포본은 `gv_` 개발 변수를 몰라 명령줄에서 만나면 읽기 오류(11)로 끝난다. 쓰는 곳은 둘이다 — 시나리오의 `<Launch variable="gv_…">`
             *          (파서가 `gv_` 가 아니면 거절하므로 선언이 있으면 곧 개발 변수다)와 호출자가 붙인 추가 인자의 `-gv_`.
             *          판정이 데이터(선언)에서 나오므로 시나리오를 새로 써도 "개발 변수 필요" 표시를 따로 빠뜨릴 수 없다.
             */
            static sw::string findDevelopmentVariableReason( const sw::string& scenarioArgument, sw::string_view extraArguments )
            {
                sw::string reason;
                if ( extraArguments.find( "-gv_" ) != sw::string_view::npos )
                    reason = "the command line passes a -gv_ development variable";
                if ( sw::ResourcePaths::initialize() == false )
                    return reason;
                sw::StringUtil::forEachToken( scenarioArgument, ",", [&reason]( sw::string_view scenarioPath )
                {
                    sw::AutomationScenario scenario;
                    sw::string             error;
                    if ( reason.empty() == false )
                        return;
                    const bool bDeclares = scenario.loadFromPath( scenarioPath, error ) && scenario.getTestOptions()._listLaunchVariable.empty() == false;
                    if ( bDeclares == false )
                        return; // 못 읽은 시나리오는 App 이 읽기 오류(11)로 끝내 제 실행에서 진다
                    reason = sw::string( "<Launch> declares the development variable " ) + scenario.getTestOptions()._listLaunchVariable.front()._name;
                } );
                return reason;
            }

            /** @brief 실행 쓰기 루트들을 모으는 폴더 이름(`build/<프리셋>/AppRuns`) — `Bin` 밖, `GoldenDiff` 옆이다. */
            static constexpr const utf8* kRunsFolderName = "AppRuns";

            /** @brief 폴더 이름에 넣어도 되는 글자만 남깁니다(케이스 이름의 `.` · `/` 는 `_`). */
            static sw::string toFolderName( sw::string_view text )
            {
                sw::string name;
                name.reserve( text.size() );
                for ( const utf8 ch : text )
                {
                    const bool bSafe = ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' ) || ( '0' <= ch && ch <= '9' );
                    name.push_back( bSafe ? ch : '_' );
                }
                return name;
            }

            /**
             * @brief `-EnableEditor` 가 있는데 에디터 상태 폴더를 주지 않은 실행에 붙일 인자(따옴표로 감싼 `-gv_editorStateDir=<쓰기 루트>/Saved/Automation/EditorState/default`)입니다. 붙일 것이 없으면 빈 글입니다.
             * @details 주지 않으면 에디터가 프로젝트 루트의 `Saved/Editor` 를 읽고 써(레이아웃 · 테마 · 설정) 동시에 도는 실행끼리 같은 JSON 을 고친다.
             *          폴더는 붙일 때마다 비운다 — 지난 실행의 레이아웃 · 설정을 읽지 않고 늘 같은 상태로 시작한다(이어 쓸 상태가 필요한 케이스는 폴더를 직접 준다).
             *          배포본은 에디터가 없고 `gv_` 를 몰라 읽기 오류(11)로 끝난다 — `-EnableEditor` 는 `hasFeaturesForArguments` 가 먼저 막는다.
             */
            static sw::string makeDefaultEditorStateArgument( sw::string_view arguments, const sw::string& runFolder )
            {
                sw::string argument;
#if !defined( SW_SHIPPING )
                const sw::string editorSwitch = AppTestUtil::makeSwitchArgument( sw::CommandLineArgument::ENABLE_EDITOR );
                const sw::string statePrefix  = sw::string( "-" ) + sw::kAutomationEditorStateVariable + "=";
                const bool       bNeedsState  = arguments.find( editorSwitch.c_str() ) != sw::string_view::npos && arguments.find( statePrefix.c_str() ) == sw::string_view::npos;
                if ( bNeedsState == false )
                    return argument;
                const sw::string stateFolder = sw::FileUtil::joinPath( runFolder, sw::string( sw::kAutomationOutputFolder ) + "/" + sw::kAutomationEditorStateFolder + "/default" );
                SW_EXPECT_TRUE_MSG( sw::FileUtil::removeDirectory( stateFolder ), ( "에디터 상태 폴더를 비우지 못했다: " + stateFolder ).c_str() );
                argument = " \"" + statePrefix + stateFolder + "\"";
#else
                (void)arguments;
                (void)runFolder;
#endif
                return argument;
            }

            /** @brief 체인의 단계 보고 경로입니다(`<base>.<단계>.json` — 단계가 둘 이상일 때 `AutomationScenarioChain::makeStageReportPath` 가 정한다). */
            static sw::string makeStageReportPath( const sw::string& outputBase, size_t stageNumber )
            {
                return outputBase + "." + sw::to_string( static_cast<uint32>( stageNumber ) ) + ".json";
            }

            /** @brief 인자(경로 하나 또는 쉼표 목록)의 시나리오 경로들입니다. */
            static sw::vector<sw::string> splitScenarioArgument( const sw::string& scenarioArgument )
            {
                sw::vector<sw::string> listPath;
                sw::StringUtil::forEachToken( scenarioArgument, ",", [&listPath]( sw::string_view token )
                {
                    listPath.emplace_back( token );
                } );
                return listPath;
            }

            /**
             * @brief 체인의 시나리오별 하위 결과입니다 — 통과하지 못한 단계마다 결과 · 종료 코드 · 보고의 `failures[]` · `unhandledLogs[]` 줄을 나열합니다.
             * @details 체인은 한 프로세스라 종료 코드 하나만 남지만, 단계마다 보고(`<base>.<단계>.json`)가 있다. 돌지 못한 단계(앞 단계가 멈춤 · 프로세스 사망)는 보고가 없다.
             */
            static sw::string describeChainStages( const sw::string& outputBase, const sw::vector<sw::string>& listPath )
            {
                sw::string text;
                size_t     notPassedCount = 0;
                for ( size_t index = 0; index < listPath.size(); ++index )
                {
                    const sw::string reportPath = makeStageReportPath( outputBase, index + 1 );
                    // 돌지 못한 단계는 보고가 없다 — 없는 파일을 읽으면 FileUtil 이 Error 를 남기므로 먼저 본다.
                    const bool       bReported = sw::FileUtil::exists( reportPath );
                    const sw::string result    = bReported ? AppTestUtil::readReportScalar( reportPath, "result" ) : sw::string{};
                    const bool       bHeld     = result == "PASS" || result == "UNCONFIRMED" || result == "SKIP";
                    if ( bHeld )
                        continue;
                    ++notPassedCount;
                    text += "  stage " + sw::to_string( static_cast<uint32>( index + 1 ) ) + "/" + sw::to_string( static_cast<uint32>( listPath.size() ) ) + " " + listPath[index] + ": ";
                    text += bReported == false ? sw::string( "NOT RUN (no report)" ) : result + " (exit " + AppTestUtil::readReportScalar( reportPath, "exitCode" ) + ")";
                    text += "\n";
                    if ( bReported == false )
                        continue;
                    for ( const utf8* pKey : { "failures", "unhandledLogs" } )
                    {
                        for ( const sw::string& line : AppTestUtil::readReportStrings( reportPath, pKey ) )
                        {
                            text += sw::string( "      " ) + pKey + ": " + line + "\n";
                        }
                    }
                }
                return "chain sub-results: " + sw::to_string( static_cast<uint32>( notPassedCount ) ) + " of " + sw::to_string( static_cast<uint32>( listPath.size() ) ) +
                       " stage(s) did not pass\n" + text;
            }
        };
    } // namespace
} // namespace test

namespace test
{
    sw::string AppTestUtil::findAppExecutablePath()
    {
        // 반환은 이 변수 하나로만 한다 — 분기마다 다른 객체를 돌려주면 NRVO 가 막힌다(-Wnrvo).
        sw::string candidate = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), AppTestUtilInternal::getAppExecutableName() );
        if ( sw::FileUtil::exists( candidate ) )
            return candidate;

        candidate = sw::FileUtil::joinPath( sw::FileUtil::getBinaryDirectory(), AppTestUtilInternal::getAppExecutableName() );
        if ( sw::FileUtil::exists( candidate ) == false )
            candidate.clear();
        return candidate;
    }

    AppLaunch AppTestUtil::makeAppLaunch( sw::string_view arguments )
    {
        AppLaunch launch;
        if ( AppTestUtilInternal::hasFeaturesForArguments( arguments ) == false )
            return launch;
        const sw::string executablePath = findAppExecutablePath();
        const sw::string runFolder      = getRunFolder();
        if ( executablePath.empty() || runFolder.empty() )
            return launch;

        launch._command = "\"";
        launch._command += executablePath;
        launch._command += "\" ";
        launch._command += makeSwitchArgument( sw::CommandLineArgument::UNATTENDED );
        launch._command += " ";
        launch._command += arguments;
        launch._command += AppTestUtilInternal::makeDefaultEditorStateArgument( arguments, runFolder );
        launch._options._workingDirectory = runFolder;
        return launch;
    }

    sw::string AppTestUtil::getRunFolder()
    {
        // 반환은 이 변수 하나로만 한다 — 분기마다 다른 객체를 돌려주면 NRVO 가 막힌다(-Wnrvo).
        sw::string       folder;
        const sw::string testName = TestRegistry::getInstance().getCurrentContext()->getTestName();
        SW_EXPECT_TRUE_MSG( testName.empty() == false, "App 은 케이스 안에서만 띄운다 — 실행 쓰기 루트가 케이스 이름으로 갈린다" );
        if ( testName.empty() )
            return folder;
        // 같은 케이스가 동시에 도는 곳은 다른 ctest 항목뿐이다(호스트 조각과 레이스 묶음이 같은 케이스를 돈다) — 항목마다 조각 번호가 달라 조각으로 가른다.
        const TestRunOptions& options    = TestRegistry::getInstance().getOptions();
        const sw::string      shardName  = "shard" + sw::to_string( options._shardIndex ) + "of" + sw::to_string( options._shardCount );
        const sw::string      runsFolder = sw::FileUtil::joinPath( sw::FileUtil::getDirectoryPart( sw::FileUtil::getBinaryDirectory() ), AppTestUtilInternal::kRunsFolderName );
        folder                           = sw::FileUtil::joinPath( sw::FileUtil::joinPath( runsFolder, shardName ), AppTestUtilInternal::toFolderName( testName ) );
        // 비우지 않는다 — 한 케이스의 뒤 실행이 앞 실행의 산출물을 읽고(녹화 → 재생), 끝난 뒤에도 로그 · 사진 · 보고를 열어 보며 `scenario-logs` 가 읽는다.
        // 지난 실행의 산출물을 읽지 않게 하는 것은 그것을 읽는 자리의 몫이다(보고 · 녹화 · 그림은 실행 전에 지운다, 에디터 상태 폴더는 붙일 때 비운다).
        const bool bReady = sw::FileUtil::ensureDirectoryExists( folder );
        SW_EXPECT_TRUE_MSG( bReady, ( "App 실행 쓰기 루트를 만들지 못했다: " + folder ).c_str() );
        if ( bReady == false )
            folder.clear();
        return folder;
    }

    sw::string AppTestUtil::makeRunPath( sw::string_view relativePath )
    {
        sw::string path;
        const bool bOutside = relativePath.empty() || sw::FileUtil::isAbsolutePath( relativePath ) || sw::ResourcePaths::hasParentDirectoryComponent( relativePath );
        SW_EXPECT_TRUE_MSG( bOutside == false, ( "실행 쓰기 루트 기준 상대 경로만 받는다: " + sw::string( relativePath ) ).c_str() );
        if ( bOutside )
            return path;
        path = getRunFolder();
        if ( path.empty() == false )
            path = sw::FileUtil::joinPath( path, relativePath );
        return path;
    }

    sw::string AppTestUtil::readPackRoot( sw::string_view gameName )
    {
        sw::string packRoot;
        if ( sw::ResourcePaths::initialize() == false )
            return packRoot;
        const sw::string repositoryRoot = sw::FileUtil::getDirectoryPart( sw::ResourcePaths::getRootFolderPath() );
        sw::string       text;
        if ( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( repositoryRoot, sw::FileUtil::joinPath( sw::config::kDirRuntimeGamePreset, sw::string( gameName ) + ".json" ) ), text ) == false )
            return packRoot;
        const size_t keyIndex = text.find( "\"_packRoot\"" );
        const size_t open     = keyIndex == sw::string::npos ? sw::string::npos : text.find( '"', text.find( ':', keyIndex ) );
        const size_t close    = open == sw::string::npos ? sw::string::npos : text.find( '"', open + 1 );
        if ( close != sw::string::npos )
            packRoot = text.substr( open + 1, close - open - 1 );
        return packRoot;
    }

    sw::vector<sw::string> AppTestUtil::collectBuiltGamePacks()
    {
        sw::vector<sw::string> listGame;
        // 게임 팩은 Shipping 에 없다 — 부르는 케이스가 `SW_TEST_REQUIRES_FEATURE( SourceResources )` 로 먼저 선언해야 한다(빈 목록을 "건너뜀" 으로 읽지 못하게 막는다).
        SW_EXPECT_TRUE_MSG( test::isBuildFeatureAvailable( test::BuildFeature::SourceResources ), "collectBuiltGamePacks 는 SourceResources 를 선언한 케이스만 부른다" );
#if !defined( SW_SHIPPING )
        const sw::string       gamesDirectory = sw::FileUtil::joinPath( sw::FileUtil::getBinaryDirectory(), sw::config::kDirBinGames );
        sw::vector<sw::string> listFile;
        if ( sw::FileUtil::isDirectory( gamesDirectory ) == false || sw::FileUtil::collectFiles( gamesDirectory, ".json", listFile, true ) == false )
            return listGame;
        for ( const sw::string& filePath : listFile )
        {
            // `Bin/Games/<게임>/SWGame.module.json` — 폴더 이름이 게임 이름이다.
            const sw::string manifestName = sw::string( sw::config::kTargetGameModule ) + ".module.json";
            if ( sw::StringUtil::equals( sw::FileUtil::getFileNamePart( filePath ).c_str(), manifestName.c_str() ) == false )
                continue;
            listGame.push_back( sw::FileUtil::getFileNamePart( sw::FileUtil::trimTrailingSlashes( sw::FileUtil::getDirectoryPart( filePath ) ) ) );
        }
        std::sort( listGame.begin(), listGame.end() );
#endif
        return listGame;
    }

    AppRunOutcome AppTestUtil::runScenario( const sw::string& scenarioPath, const utf8* pBackendSwitch, sw::string& outScenarioLines, sw::string_view extraArguments )
    {
        return runScenarioArgument( scenarioPath, sw::getAutomationScenarioStem( scenarioPath ), pBackendSwitch, outScenarioLines, extraArguments,
                                    kScenarioTimeoutSeconds );
    }

    sw::string AppTestUtil::makeReportPath( const sw::string& scenarioPath, const utf8* pBackendSwitch )
    {
        const sw::string backend = pBackendSwitch[0] == '-' ? sw::string( pBackendSwitch + 1 ) : sw::string( "default" );
        return makeRunPath( sw::string( sw::kAutomationOutputFolder ) + "/" + sw::getAutomationScenarioStem( scenarioPath ) + "_" + backend + ".json" );
    }

    sw::vector<sw::string> AppTestUtil::readReportRecords( const sw::string& reportPath )
    {
        return readReportStrings( reportPath, "records" );
    }

    sw::vector<sw::string> AppTestUtil::readReportStrings( const sw::string& reportPath, const utf8* pKey )
    {
        sw::vector<sw::string> listLine;
        sw::string             json;
        if ( sw::FileUtil::readTextFile( reportPath, json ) == false )
            return listLine;
        const size_t keyAt = json.find( sw::string( "\"" ) + pKey + "\"" );
        size_t       at    = keyAt == sw::string::npos ? sw::string::npos : json.find( '[', keyAt );
        if ( at == sw::string::npos )
            return listLine;
        // 줄은 `"…"` 이고 안의 `\"` · `\\` · `\n` 만 풀면 된다(보고 작성기가 그 셋만 바꾼다). 줄 안의 `]` 는 배열 끝이 아니므로 따옴표 밖의 `]` 에서 끝낸다.
        ++at;
        while ( at < json.size() )
        {
            while ( at < json.size() && ( json[at] == ' ' || json[at] == ',' || json[at] == '\n' || json[at] == '\r' ) )
            {
                ++at;
            }
            if ( at >= json.size() || json[at] != '"' )
                break;
            sw::string line;
            size_t     cursor = at + 1;
            while ( cursor < json.size() && json[cursor] != '"' )
            {
                if ( json[cursor] == '\\' && cursor + 1 < json.size() )
                {
                    ++cursor;
                    line.push_back( json[cursor] == 'n' ? '\n' : json[cursor] );
                }
                else
                {
                    line.push_back( json[cursor] );
                }
                ++cursor;
            }
            listLine.push_back( std::move( line ) );
            at = cursor + 1;
        }
        return listLine;
    }

    sw::string AppTestUtil::readReportScalar( const sw::string& reportPath, const utf8* pKey )
    {
        sw::string text;
        sw::string json;
        if ( sw::FileUtil::readTextFile( reportPath, json ) == false )
            return text;
        const size_t keyAt = json.find( sw::string( "\"" ) + pKey + "\":" );
        if ( keyAt == sw::string::npos )
            return text;
        size_t at = keyAt + sw::string( pKey ).size() + 3;
        while ( at < json.size() && json[at] == ' ' )
        {
            ++at;
        }
        const bool bQuoted = at < json.size() && json[at] == '"';
        at += bQuoted ? 1 : 0;
        // 문자열 값은 닫는 따옴표까지(보고 작성기가 `\"` · `\\` · `\n` 만 바꾼다), 숫자는 쉼표 · 줄 끝까지다.
        for ( ; at < json.size(); ++at )
        {
            const utf8 ch   = json[at];
            const bool bEnd = bQuoted ? ch == '"' : ( ch == ',' || ch == '\n' );
            if ( bEnd )
                break;
            const bool bEscape = bQuoted && ch == '\\';
            if ( bEscape && at + 1 < json.size() )
                ++at;
            text.push_back( json[at] );
        }
        return text;
    }

    sw::string AppTestUtil::describeExitCode( int32 exitCode )
    {
        sw::string text = exitCode == sw::kRHIUnusableHereExitCode ? "RHI UNUSABLE HERE: the backend could not stand up on this machine"
                                                                   : "not a scenario exit code (crash, kill or startup failure)";
        for ( const sw::AutomationOutcome outcome : { sw::AutomationOutcome::Passed, sw::AutomationOutcome::Failed, sw::AutomationOutcome::LoadError, sw::AutomationOutcome::TimedOut,
                                                      sw::AutomationOutcome::Skipped, sw::AutomationOutcome::Unconfirmed, sw::AutomationOutcome::UnhandledLog } )
        {
            if ( sw::getAutomationExitCode( outcome ) != exitCode )
                continue;
            text = sw::getAutomationOutcomeName( outcome );
            // 10 과 15 는 원인을 찾을 자리가 다르다 — 이름만으로는 헷갈리므로 뜻을 덧붙인다.
            if ( outcome == sw::AutomationOutcome::UnhandledLog )
                text += ": the scenario's checks held but the output has Warning/Error lines that no allow-list entry or <ExpectLog> covers";
            else if ( outcome == sw::AutomationOutcome::Failed )
                text += ": a scenario check failed";
            break;
        }
        return text;
    }

    AppRunOutcome AppTestUtil::runScenarioChain( const sw::vector<sw::string>& listScenarioPath, const sw::string& chainName, const utf8* pBackendSwitch,
                                                 sw::string& outScenarioLines, sw::string_view extraArguments )
    {
        sw::string argument;
        for ( const sw::string& scenarioPath : listScenarioPath )
        {
            argument += argument.empty() ? "" : ",";
            argument += scenarioPath;
        }
        const uint32 timeoutSeconds = sw::AutomationTimeLimits::getChainKillSeconds( static_cast<uint32>( listScenarioPath.size() ) );
        return runScenarioArgument( argument, "chain_" + chainName, pBackendSwitch, outScenarioLines, extraArguments, timeoutSeconds );
    }

    AppRunOutcome AppTestUtil::runScenarioArgument( const sw::string& scenarioArgument, const sw::string& outputStem, const utf8* pBackendSwitch,
                                                    sw::string& outScenarioLines, sw::string_view extraArguments, uint32 timeoutSeconds )
    {
        // 개발 변수(`gv_`)가 필요한 실행은 배포본에서 띄우지 않는다 — 건너뜀을 보고(`[ PARTSKIP ]` + 요약 개수)에 남기고 `Skipped` 로 돌려준다(이 입구 하나라 호출자가 빠뜨릴 수 없다).
        const sw::string developmentReason = AppTestUtilInternal::findDevelopmentVariableReason( scenarioArgument, extraArguments );
        if ( developmentReason.empty() == false && skipPartIfFeatureMissing( BuildFeature::DevelopmentBuild, scenarioArgument + ": " + developmentReason ) )
            return AppRunOutcome::Skipped;

        const sw::string backend    = pBackendSwitch[0] == '-' ? sw::string( pBackendSwitch + 1 ) : sw::string( "default" );
        const sw::string outputBase = makeRunPath( sw::string( sw::kAutomationOutputFolder ) + "/" + outputStem + "_" + backend );
        if ( outputBase.empty() )
            return AppRunOutcome::Failed; // 실행 쓰기 루트를 얻지 못한 이유는 `getRunFolder` 가 기록했다
        (void)sw::FileUtil::ensureDirectoryExists( sw::FileUtil::getDirectoryPart( outputBase ) );
        // 못 띄우거나 죽은 판이 지난 실행의 보고를 남긴 것처럼 읽히지 않게 한다(체인은 단계마다 보고가 있다).
        const sw::vector<sw::string> listStagePath = AppTestUtilInternal::splitScenarioArgument( scenarioArgument );
        (void)sw::FileUtil::tryRemoveFile( outputBase + ".json" ); // 지난 실행이 없으면 지울 것이 없다 — 없는 것이 정상이라 실패를 따지지 않는다
        for ( size_t index = 0; listStagePath.size() > 1 && index < listStagePath.size(); ++index )
        {
            (void)sw::FileUtil::tryRemoveFile( AppTestUtilInternal::makeStageReportPath( outputBase, index + 1 ) ); // 지난 실행의 단계 보고가 없으면 지울 것이 없다 — 실패를 따지지 않는다
        }
        // 보고 경로는 실행 쓰기 루트의 절대 경로라 공백이 들 수 있다 — 따옴표로 감싼다.
        sw::string arguments = makeValueArgument( sw::CommandLineArgument::SCENARIO, scenarioArgument ) + " \"" +
                               makeValueArgument( sw::CommandLineArgument::SCENARIO_REPORT, outputBase + ".json" ) + "\" " + pBackendSwitch;
        if ( extraArguments.empty() == false )
        {
            arguments += " ";
            arguments += extraArguments;
        }

        // 실패 줄은 여러 줄이다(`[Scenario] FAIL …` 다음에 들여 쓴 실패 이유가 이어진다) — 들여 쓴 줄도 함께 모은다.
        bool                   bInScenarioBlock = false;
        test::ProcessRunResult result           = runApp( arguments, timeoutSeconds, [&outScenarioLines, &bInScenarioBlock]( sw::Process&, const sw::string& line )
                  {
            const bool bScenarioLine = line.find( "[Scenario]" ) != sw::string::npos;
            const bool bContinuation = bInScenarioBlock && sw::StringUtil::startsWith( line, "    " );
            if ( bScenarioLine || bContinuation )
                outScenarioLines += line + "\n";
            bInScenarioBlock = bScenarioLine || bContinuation;
        } );
        if ( result._bLaunched )
            (void)sw::FileUtil::writeTextFile( outputBase + ".log", result._output ); // 진단용 로그 사본 — 실패는 writeTextFile 이 오류로 남긴다
        const bool bExitedCleanly = result._bLaunched && result._bTimedOut == false && ( result._exitCode == 0 || result._exitCode == AppTestUtilInternal::kUnconfirmedExitCode );
        if ( bExitedCleanly )
        {
            const sw::vector<sw::string> listUnexpected = collectUnexpectedLogLines( result._output, scenarioArgument );
            for ( const sw::string& line : listUnexpected )
            {
                outScenarioLines += "unhandled log (outside the scenario window): " + line + "\n";
            }
            if ( listUnexpected.empty() == false )
                result._exitCode = AppTestUtilInternal::kUnhandledLogExitCode;
        }
        sw::string detail = "scenario " + scenarioArgument + " on backend " + backend + "\nlog: " + outputBase + ".log\n" + outScenarioLines;
        if ( result._bLaunched && ( result._bTimedOut || result._exitCode != 0 ) )
        {
            // 보고에 적힌 실패 이유와 처리하지 않은 로그 줄 — 로그 파일을 열지 않고도 무엇이 졌는지 보이게 한다. 체인은 단계(시나리오)별로 나열한다.
            // 체인은 단계마다 보고(`<base>.<단계>.json`)를 쓰고 `<base>.json` 은 없다 — 없는 파일을 읽으면 FileUtil 이 Error 를 남겨 호스트 스위트 케이스가 진다.
            if ( listStagePath.size() > 1 )
            {
                detail += AppTestUtilInternal::describeChainStages( outputBase, listStagePath );
            }
            else
            {
                for ( const utf8* pKey : { "failures", "unhandledLogs" } )
                {
                    for ( const sw::string& line : readReportStrings( outputBase + ".json", pKey ) )
                    {
                        detail += sw::string( "report " ) + pKey + ": " + line + "\n";
                    }
                }
            }
        }
        return judgeRun( result, arguments, detail );
    }

    AppRunOutcome AppTestUtil::judgeRun( const ProcessRunResult& run, sw::string_view arguments, const sw::string& detail )
    {
        sw::string label = "App " + sw::string( arguments );
        if ( run._bLaunched == false )
        {
            reportFailure( "App is launched", __FILE__, __LINE__, label + " -> not launched (is App in the working folder Bin or next to the test binary?)" );
            return AppRunOutcome::Failed;
        }
        label += run._bTimedOut ? sw::string( " -> timed out and killed" ) : " -> exit " + sw::to_string( run._exitCode ) + " (" + describeExitCode( run._exitCode ) + ")";
        label += "\n" + detail;
        if ( run._bTimedOut == false && run._exitCode == 0 )
            return AppRunOutcome::Passed;
        if ( run._bTimedOut == false && run._exitCode == AppTestUtilInternal::kUnconfirmedExitCode )
        {
            // 단언은 맞았다. 어림값 문턱(`provisional`)이 들어 있어 확정이 아님을 로그로 남긴다 — 잰 값을 `measured` 로 옮기는 것이 할 일이다.
            SW_LOG_INFO( "[AppTestUtil] UNCONFIRMED (provisional thresholds held, not measured yet): %#", label.c_str() );
            return AppRunOutcome::Passed;
        }
        if ( run._bTimedOut == false && run._exitCode == AppTestUtilInternal::kSkippedExitCode )
        {
            SW_LOG_INFO( "[AppTestUtil] skipped by the scenario: %#", label.c_str() );
            return AppRunOutcome::Skipped;
        }
        if ( run._bTimedOut == false && run._exitCode == sw::kRHIUnusableHereExitCode )
        {
            // 스위치가 없는 실행(빌드 기본 · 배포본)은 백엔드를 고르지 않았으므로 요구 여부를 따질 대상이 없다.
            sw::RHIBackend backend{};
            const bool     bRequired = AppTestUtilInternal::findBackendSwitch( arguments, backend ) &&
                                   reportUnusableBackend( backend, ( "App exited with the RHI-unusable-here code: " + label ).c_str(), __FILE__, __LINE__ );
            if ( bRequired )
                return AppRunOutcome::Failed;
            // 어느 백엔드가 빠졌는지 남긴다. Warning 이 아니라 Info 다 — 케이스의 로그 판정기가 Warning 을 실패로 센다. 그 백엔드를 지켜야 하면 요구 백엔드로 적는다.
            SW_LOG_INFO( "[AppTestUtil] backend not usable on this machine, skipping: %#", label.c_str() );
            return AppRunOutcome::BackendUnusable;
        }
        reportFailure( "App run passes", __FILE__, __LINE__, label );
        return AppRunOutcome::Failed;
    }

    sw::vector<sw::string> AppTestUtil::collectUnexpectedLogLines( const sw::string& output, const sw::string& scenarioArgument )
    {
        sw::vector<sw::string>  listLine;
        sw::AutomationLogPolicy policy;
        sw::string              error;
        if ( sw::ResourcePaths::initialize() == false || policy.loadAllowList( error ) == false )
        {
            listLine.push_back( "log allow list is not loaded: " + error );
            return listLine;
        }
        sw::StringUtil::forEachToken( scenarioArgument, ",", [&policy, &error]( sw::string_view scenarioPath )
        {
            sw::AutomationScenario scenario;
            if ( scenario.loadFromPath( scenarioPath, error ) == false )
                return; // 읽지 못한 시나리오는 App 이 이미 11(읽기 오류)로 끝냈다
            policy.selectScenario( scenario.getName() );
            for ( const sw::AutomationStep& step : scenario.getSteps() )
            {
                if ( step._kind != "ExpectLog" )
                    continue;
                const sw::string* pCount = step.findAttribute( "count" );
                if ( pCount != nullptr && *pCount == "0" )
                    continue; // count="0" 은 "없어야 한다" 는 단언이지 선언이 아니다
                const sw::string* pContains = step.findAttribute( "contains" );
                if ( pContains != nullptr )
                    policy.declareExpected( *pContains );
                const sw::string* pID = step.findAttribute( "id" );
                if ( pID != nullptr )
                    policy.declareExpectedID( *pID );
            }
        } );
        sw::StringUtil::forEachLine( output, [&policy, &listLine]( sw::string_view line )
        {
            // 콘솔 줄 모양: `[시각] [태그] [호출자] [Warning] - [id:영역.사건] 본문` (`[id:…]` 꼬리표는 id 가 있는 줄에만 붙는다 — `AsyncLogSink`)
            size_t       levelAt = line.find( "] [Warning] - " );
            sw::LogLevel level   = sw::LogLevel::Warning;
            if ( levelAt == sw::string_view::npos )
            {
                levelAt = line.find( "] [Error] - " );
                level   = sw::LogLevel::Error;
            }
            if ( levelAt == sw::string_view::npos )
                return;
            sw::string_view message = line.substr( line.find( " - ", levelAt ) + 3 );
            sw::string_view id;
            if ( sw::StringUtil::startsWith( message, sw::LogIDTag::kOpen ) )
            {
                const size_t closeAt = message.find( sw::LogIDTag::kClose );
                if ( closeAt != sw::string_view::npos )
                {
                    const size_t bodyAt = closeAt + sw::LogIDTag::kCloseLength;
                    id                  = message.substr( sw::LogIDTag::kOpenLength, closeAt - sw::LogIDTag::kOpenLength );
                    message             = bodyAt < message.size() ? message.substr( bodyAt ) : sw::string_view{};
                }
            }
            const sw::AutomationLogVerdict verdict    = policy.classify( level, id, message );
            const bool                     bUnhandled = verdict == sw::AutomationLogVerdict::MissingID || verdict == sw::AutomationLogVerdict::NotAllowed;
            if ( bUnhandled )
                listLine.push_back( sw::string( line ) );
        } );
        return listLine;
    }
} // namespace test
