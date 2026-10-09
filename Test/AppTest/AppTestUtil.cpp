#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Resource/ResourceUtil.h"

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

    bool AppTestUtil::launchApp( sw::Process& outProcess, sw::string_view arguments )
    {
        const sw::string executablePath = findAppExecutablePath();
        if ( executablePath.empty() )
            return false;

        // 시험이 띄우는 App 은 사람이 지켜보지 않는다 — 에디터를 켜도 단언 대화상자를 걸지 않는다(Debug 단언은 멈춘다).
        sw::string command{ "\"" };
        command += executablePath;
        command += "\" -unattended ";
        command += arguments;
        return outProcess.launch( command );
    }

    sw::string AppTestUtil::readActivePackRoot()
    {
        sw::string packRoot;
        if ( sw::ResourceUtil::initialize() == false )
            return packRoot;
        const sw::string repositoryRoot = sw::FileUtil::getDirectoryPart( sw::ResourceUtil::getRootFolderPath() );
        sw::string       text;
        if ( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( repositoryRoot, sw::config::kFileRuntimeGameConfig ), text ) == false )
            return packRoot;
        const size_t keyIndex = text.find( "\"_packRoot\"" );
        const size_t open     = keyIndex == sw::string::npos ? sw::string::npos : text.find( '"', text.find( ':', keyIndex ) );
        const size_t close    = open == sw::string::npos ? sw::string::npos : text.find( '"', open + 1 );
        if ( close != sw::string::npos )
            packRoot = text.substr( open + 1, close - open - 1 );
        return packRoot;
    }

    int32 AppTestUtil::runScenario( const sw::string& scenarioPath, const utf8* pBackendSwitch, sw::string& outScenarioLines, sw::string_view extraArguments )
    {
        const sw::string backend    = pBackendSwitch[0] == '-' ? sw::string( pBackendSwitch + 1 ) : sw::string( "default" );
        const sw::string outputBase = "Saved/Automation/" + AppTestUtilInternal::getScenarioStem( scenarioPath ) + "_" + backend;
        (void)sw::FileUtil::ensureDirectoryExists( "Saved/Automation" );
        sw::string arguments = "-scenario=" + scenarioPath + " -scenario-report=" + outputBase + ".json " + pBackendSwitch;
        if ( extraArguments.empty() == false )
        {
            arguments += " ";
            arguments += extraArguments;
        }

        sw::Process process;
        if ( launchApp( process, arguments ) == false )
            return kNotLaunchedExitCode;

        // 시한 감시 — 멈춘 App 이 CTest 시한까지 붙잡지 않게 죽인다(`terminate` 는 다른 스레드가 읽는 중에도 안전하다).
        sw::atomic<bool> bDone{ false };
        sw::atomic<bool> bKilled{ false };
        std::thread      watchdog( [&process, &bDone, &bKilled]()
        {
            const int64 deadline = sw::MonotonicClock::nowMicroseconds() + static_cast<int64>( kScenarioTimeoutSeconds ) * 1000000;
            while ( bDone.load() == false && sw::MonotonicClock::nowMicroseconds() < deadline )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            }
            if ( bDone.load() == false )
            {
                bKilled.store( true );
                (void)process.terminate( -1 );
            }
        } );

        sw::string log;
        sw::string line;
        while ( process.readOutputLine( line ) )
        {
            log += line;
            log += "\n";
            if ( line.find( "[Scenario]" ) != sw::string::npos )
                outScenarioLines += line + "\n";
        }
        const int32 exitCode = process.waitForExit();
        bDone.store( true );
        watchdog.join();
        (void)sw::FileUtil::writeTextFile( outputBase + ".log", log ); // 진단용 로그 사본 — 실패는 writeTextFile 이 오류로 남긴다
        return bKilled.load() ? -1 : exitCode;
    }

    bool AppTestUtil::isSkippedExitCode( int32 exitCode )
    {
        return exitCode == kSkippedExitCode || exitCode == sw::kRhiUnusableHereExitCode || exitCode == kNotLaunchedExitCode;
    }
} // namespace test
