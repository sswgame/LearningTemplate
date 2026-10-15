#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/Time/MonotonicClock.h"

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
        return result._bTimedOut ? -1 : result._exitCode;
    }

    bool AppTestUtil::isSkippedExitCode( int32 exitCode )
    {
        return exitCode == kSkippedExitCode || exitCode == sw::kRHIUnusableHereExitCode || exitCode == kNotLaunchedExitCode;
    }
} // namespace test
