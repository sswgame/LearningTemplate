#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/Time/MonotonicClock.h"

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
            /** @brief 시나리오가 건너뜀으로 끝났다(전경 창을 못 얻음 등 — `AutomationResult::Skipped`). */
            static constexpr int32 kSkippedExitCode = 13;
            /** @brief 시나리오의 단언은 모두 맞았지만 어림값 문턱이 들어 있다(`AutomationResult::Unconfirmed`). 실패가 아니라 로그로 미확정을 알리고 통과로 넘긴다. */
            static constexpr int32 kUnconfirmedExitCode = 14;
            /** @brief 시나리오는 통과했지만 출력에 허용하지 않은 Warning · Error 가 있다(`AutomationResult::Failed` 와 같은 값). */
            static constexpr int32 kUnexpectedLogExitCode = 10;

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

            /** @brief 시나리오 이름 조각(`weaponswitch`)입니다 — 로그 · 보고 파일 이름에 쓴다. */
            static sw::string getScenarioStem( sw::string_view path )
            {
                const size_t slash = path.find_last_of( '/' );
                sw::string   stem{ path.substr( slash == sw::string_view::npos ? 0 : slash + 1 ) };
                return stem.substr( 0, stem.find( '.' ) );
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

    sw::string AppTestUtil::makeAppCommand( sw::string_view arguments )
    {
        const sw::string executablePath = findAppExecutablePath();
        if ( executablePath.empty() )
            return {};

        sw::string command{ "\"" };
        command += executablePath;
        command += "\" -unattended ";
        command += arguments;
        return command;
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
        return runScenarioArgument( scenarioPath, AppTestUtilInternal::getScenarioStem( scenarioPath ), pBackendSwitch, outScenarioLines, extraArguments,
                                    kScenarioTimeoutSeconds );
    }

    sw::string AppTestUtil::makeReportPath( const sw::string& scenarioPath, const utf8* pBackendSwitch )
    {
        const sw::string backend = pBackendSwitch[0] == '-' ? sw::string( pBackendSwitch + 1 ) : sw::string( "default" );
        return "Saved/Automation/" + AppTestUtilInternal::getScenarioStem( scenarioPath ) + "_" + backend + ".json";
    }

    sw::vector<sw::string> AppTestUtil::readReportRecords( const sw::string& reportPath )
    {
        sw::vector<sw::string> listRecord;
        sw::string             json;
        if ( sw::FileUtil::readTextFile( reportPath, json ) == false )
            return listRecord;
        const size_t key = json.find( "\"records\"" );
        if ( key == sw::string::npos )
            return listRecord;
        const size_t end = json.find( ']', key );
        size_t       at  = json.find( '[', key );
        // 줄은 `"…"` 이고 안의 `\"` · `\\` 만 풀면 된다(보고 작성기가 그 둘과 줄바꿈만 바꾼다).
        while ( at != sw::string::npos && at < end )
        {
            const size_t open = json.find( '"', at + 1 );
            if ( open == sw::string::npos || open > end )
                break;
            sw::string line;
            size_t     cursor = open + 1;
            while ( cursor < json.size() && json[cursor] != '"' )
            {
                if ( json[cursor] == '\\' && cursor + 1 < json.size() )
                    ++cursor;
                line.push_back( json[cursor] );
                ++cursor;
            }
            listRecord.push_back( std::move( line ) );
            at = cursor;
        }
        return listRecord;
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
        const uint32 stageTimeoutSeconds = static_cast<uint32>( listScenarioPath.size() ) * kChainStageTimeoutSeconds;
        const uint32 timeoutSeconds      = stageTimeoutSeconds > kScenarioTimeoutSeconds ? stageTimeoutSeconds : kScenarioTimeoutSeconds;
        return runScenarioArgument( argument, "chain_" + chainName, pBackendSwitch, outScenarioLines, extraArguments, timeoutSeconds );
    }

    AppRunOutcome AppTestUtil::runScenarioArgument( const sw::string& scenarioArgument, const sw::string& outputStem, const utf8* pBackendSwitch,
                                                    sw::string& outScenarioLines, sw::string_view extraArguments, uint32 timeoutSeconds )
    {
        const sw::string backend    = pBackendSwitch[0] == '-' ? sw::string( pBackendSwitch + 1 ) : sw::string( "default" );
        const sw::string outputBase = "Saved/Automation/" + outputStem + "_" + backend;
        (void)sw::FileUtil::ensureDirectoryExists( "Saved/Automation" );
        sw::string arguments = "-scenario=" + scenarioArgument + " -scenario-report=" + outputBase + ".json " + pBackendSwitch;
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
                outScenarioLines += "unexpected log (outside the scenario window): " + line + "\n";
            }
            if ( listUnexpected.empty() == false )
                result._exitCode = AppTestUtilInternal::kUnexpectedLogExitCode;
        }
        return judgeRun( result, arguments, "log: " + outputBase + ".log\n" + outScenarioLines );
    }

    AppRunOutcome AppTestUtil::judgeRun( const ProcessRunResult& run, sw::string_view arguments, const sw::string& detail )
    {
        sw::string label = "App " + sw::string( arguments );
        if ( run._bLaunched == false )
        {
            reportFailure( "App is launched", __FILE__, __LINE__, label + " -> not launched (is App in the working folder Bin or next to the test binary?)" );
            return AppRunOutcome::Failed;
        }
        label += run._bTimedOut ? sw::string( " -> timed out and killed" ) : " -> exit " + sw::to_string( run._exitCode );
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
            for ( const sw::AutomationStep& step : scenario.getSteps() )
            {
                const sw::string* pContains = step._kind == "ExpectLog" ? step.findAttribute( "contains" ) : nullptr;
                if ( pContains != nullptr )
                    policy.addAllowed( *pContains, "declared by <ExpectLog>" );
            }
        } );
        sw::StringUtil::forEachLine( output, [&policy, &listLine]( sw::string_view line )
        {
            // 콘솔 줄 모양: `[시각] [태그] [호출자] [Warning] - 본문`
            size_t levelAt = line.find( "] [Warning] - " );
            if ( levelAt == sw::string_view::npos )
                levelAt = line.find( "] [Error] - " );
            if ( levelAt == sw::string_view::npos )
                return;
            const sw::string_view message  = line.substr( line.find( " - ", levelAt ) + 3 );
            const bool            bIgnored = sw::AutomationLogPolicy::isExemptMessage( message ) || policy.isAllowed( line );
            if ( bIgnored == false )
                listLine.push_back( sw::string( line ) );
        } );
        return listLine;
    }
} // namespace test
