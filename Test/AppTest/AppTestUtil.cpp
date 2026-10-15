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

#include "sw/config/ConfigConstants.h"

#include <chrono>
#include <thread>

namespace test
{
    namespace
    {
        struct AppTestUtilInternal
        {
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
        // 반환은 이 변수 하나로만 한다 — 갈래마다 다른 객체를 돌려주면 NRVO 가 막힌다(-Wnrvo).
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

    int32 AppTestUtil::runScenario( const sw::string& scenarioPath, const utf8* pBackendSwitch, sw::string& outScenarioLines, sw::string_view extraArguments )
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

    int32 AppTestUtil::runScenarioChain( const sw::vector<sw::string>& listScenarioPath, const sw::string& chainName, const utf8* pBackendSwitch,
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

    int32 AppTestUtil::runScenarioArgument( const sw::string& scenarioArgument, const sw::string& outputStem, const utf8* pBackendSwitch, sw::string& outScenarioLines,
                                            sw::string_view extraArguments, uint32 timeoutSeconds )
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

        const test::ProcessRunResult result = runApp( arguments, timeoutSeconds, [&outScenarioLines]( sw::Process&, const sw::string& line )
        {
            if ( line.find( "[Scenario]" ) != sw::string::npos )
                outScenarioLines += line + "\n";
        } );
        if ( result._bLaunched == false )
            return kNotLaunchedExitCode;
        (void)sw::FileUtil::writeTextFile( outputBase + ".log", result._output ); // 진단용 로그 사본 — 실패는 writeTextFile 이 오류로 남긴다
        if ( result._bTimedOut )
            return -1;
        if ( result._exitCode != 0 )
            return result._exitCode;
        const sw::vector<sw::string> listUnexpected = collectUnexpectedLogLines( result._output, scenarioArgument );
        for ( const sw::string& line : listUnexpected )
        {
            outScenarioLines += "unexpected log (outside the scenario window): " + line + "\n";
        }
        return listUnexpected.empty() ? 0 : kUnexpectedLogExitCode;
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

    bool AppTestUtil::isSkippedExitCode( int32 exitCode )
    {
        return exitCode == kSkippedExitCode || exitCode == sw::kRHIUnusableHereExitCode || exitCode == kNotLaunchedExitCode;
    }
} // namespace test
