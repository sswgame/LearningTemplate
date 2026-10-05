#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

using namespace sw;

namespace
{
#if defined( SW_PLATFORM_WINDOWS )
    constexpr const utf8* kServerExecutableName = "Server.exe";
#else
    constexpr const utf8* kServerExecutableName = "Server";
#endif
    constexpr const utf8* kReadyLine    = "Dedicated server ready";
    constexpr const utf8* kCompleteLine = "Dedicated server shutdown complete";
    /** @brief 서버 한 판의 시한(초) — 정상 종료 경로가 멈춘 결함을 CTest 시한(180 s) 전에 보이게. 횟수가 아니라 시간이다. */
    constexpr int64 kRunTimeoutSeconds = 90;

    /** @brief 한 판의 결과입니다. */
    struct ServerRun
    {
        int32  _exitCode{ -1 };
        uint32 _errorCount{ 0 };
        string _firstErrorLine;
        string _output;
        bool   _bLaunched{ false };
        bool   _bReadySeen{ false };
        bool   _bTimedOut{ false };
    };

    /** @brief Server 실행 파일의 절대 경로 — 작업 폴더(Bin), 없으면 시험 실행 파일 폴더. 없으면 빈 글. */
    string findServerPath()
    {
        const string inBin = FileUtil::joinPath( FileUtil::getCurrentPath(), kServerExecutableName );
        if ( FileUtil::exists( inBin ) )
            return inBin;
        const string besideTest = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ), kServerExecutableName );
        return FileUtil::exists( besideTest ) ? besideTest : string{};
    }

    /**
     * @brief Server 를 @p arguments 로 띄우고 끝날 때까지 출력을 읽습니다. "ready" 줄을 처음 보면 @p onReady 를 한 번 부릅니다.
     * @details 시한이 지나면 감시 스레드가 강제 종료한다(`Process::terminate` 는 다른 스레드가 읽는 중에 불러도 되는 것이 계약).
     */
    template <typename TOnReady>
    ServerRun runServer( string_view arguments, const ProcessOptions& options, TOnReady&& onReady )
    {
        ServerRun    run;
        const string serverPath = findServerPath();
        Process      process;
        if ( serverPath.empty() || process.launch( "\"" + serverPath + "\" " + string( arguments ), options ) == false )
            return run;
        run._bLaunched = true;
        atomic<uint32> finished{ 0 };
        atomic<uint32> timedOut{ 0 };
        std::thread    watchdog( [&process, &finished, &timedOut]()
        {
            const int64 deadline = MonotonicClock::nowNanoseconds() + kRunTimeoutSeconds * 1000000000LL;
            while ( finished.load() == 0 && MonotonicClock::nowNanoseconds() < deadline )
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            if ( finished.load() == 0 )
            {
                timedOut.store( 1 );
                (void)process.terminate( 124 );
            }
        } );
        string         line;
        while ( process.readOutputLine( line ) )
        {
            run._output += line + "\n";
            if ( line.find( "[Error]" ) != string::npos && run._errorCount++ == 0 )
                run._firstErrorLine = line;
            if ( run._bReadySeen == false && line.find( kReadyLine ) != string::npos )
            {
                run._bReadySeen = true;
                onReady( process );
            }
        }
        run._exitCode = process.waitForExit();
        finished.store( 1 );
        watchdog.join();
        run._bTimedOut = timedOut.load() != 0;
        return run;
    }

    void expectCleanExit( const ServerRun& run, const utf8* pExpectedCause )
    {
        SW_EXPECT_TRUE( run._bLaunched );
        SW_EXPECT_FALSE_MSG( run._bTimedOut, "the server did not stop before the watchdog" );
        SW_EXPECT_EQUAL( 0, run._exitCode );
        SW_EXPECT_TRUE_MSG( run._errorCount == 0, run._firstErrorLine.empty() ? "[Error] in the server log" : run._firstErrorLine.c_str() );
        SW_EXPECT_TRUE( run._bReadySeen );
        const string requested = string( "Dedicated server shutdown requested (" ) + pExpectedCause + ")";
        SW_EXPECT_TRUE_MSG( run._output.find( requested ) != string::npos, requested.c_str() );
        SW_EXPECT_TRUE( run._output.find( kCompleteLine ) != string::npos );
    }
} // namespace

/**
 * @brief [ServerBootTest] 전용 서버가 창 · GPU 없이 서고, 정한 틱을 돌고, 정상 종료한다 — 기동 표의 클라이언트 단계(RHI · 렌더러 · 플레이어 설정)는 돌지 않는다
 * @details CI(nogpu)에서 도는 "실제 서버 프로세스" 시험이다. 시작 씬을 읽어도 `[Error]` 가 0 이어야 한다 — 서버는 텍스처 · 셰이더 바이너리 ·
 *          오디오를 읽지 않고 없는 것으로 친다(쿠킹 표 target_excluded_asset_kinds).
 */
SW_TEST_CASE( ServerBootTest, StartsTicksAndExitsWithoutWindowOrGpu )
{
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    const ServerRun run = runServer( "-gv_serverExitAfterTicks=60", ProcessOptions{}, []( Process& ) {} );
    expectCleanExit( run, "TickLimit" );
    const size_t skippedLine = run._output.find( "startup steps not run" );
    SW_ASSERT_TRUE_MSG( skippedLine != string::npos, "the dedicated server did not report the client-only startup steps it skipped" );
    const string skipped = run._output.substr( skippedLine, run._output.find( '\n', skippedLine ) - skippedLine );
    SW_EXPECT_TRUE( skipped.find( "RHI" ) != string::npos && skipped.find( "FrameRenderer" ) != string::npos && skipped.find( "UserSettings" ) != string::npos );
}

/**
 * @brief [ServerBootTest] 표준 입력의 quit 명령으로 정상 종료하고, status 가 틱 시간을 알린다
 */
SW_TEST_CASE( ServerBootTest, QuitCommandOnStandardInputStopsTheServer )
{
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    ProcessOptions options;
    options._bPipeStandardInput = true;
    const ServerRun run         = runServer( "-gv_serverExitAfterTicks=100000", options, []( Process& process )
            { (void)process.writeInput( "status\nquit\n" ); } );
    expectCleanExit( run, "ConsoleCommand" );
    SW_EXPECT_TRUE( run._output.find( "Dedicated server status" ) != string::npos );
}

/**
 * @brief [ServerBootTest] 표준 입력이 닫혀도(EOF — systemd · 서비스 · /dev/null) 서버는 내려가지 않는다
 * @details EOF 를 종료로 읽으면 systemd 로 띄운 서버가 기동 직후 내려간다. 입력을 닫은 뒤에도 틱 상한까지 돌아야 한다.
 */
SW_TEST_CASE( ServerBootTest, ClosedStandardInputDoesNotStopTheServer )
{
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    ProcessOptions options;
    options._bPipeStandardInput = true;
    const ServerRun run         = runServer( "-gv_serverExitAfterTicks=60", options, []( Process& process )
            { process.closeInput(); } );
    expectCleanExit( run, "TickLimit" );
}

/**
 * @brief [ServerBootTest] OS 의 정상 종료 요청(POSIX SIGTERM · Windows Ctrl+Break)에 정상 종료한다
 * @details 리눅스는 systemd · docker 의 정지가 이 길이다. Windows 는 이 프로세스가 콘솔에 붙어 있지 않으면(일부 CI 러너) 보낼 수 없어 표준 입력
 *          quit 로 정리한 뒤 건너뛴다 — 같은 깃발 길은 `QuitCommandOnStandardInputStopsTheServer` 와 CoreTest `ShutdownSignalTest` 가 본다.
 */
SW_TEST_CASE( ServerBootTest, StopRequestShutsDownGracefully )
{
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    ProcessOptions options;
    options._bPipeStandardInput = true;
    options._bNewProcessGroup   = true;
    bool            bStopSent   = false;
    const ServerRun run         = runServer( "-gv_serverExitAfterTicks=100000", options, [&bStopSent]( Process& process )
            {
        bStopSent = process.requestStop();
        if ( bStopSent == false )
            (void)process.writeInput( "quit\n" );
    } );
    if ( bStopSent == false )
        SW_TEST_SKIP( "this process has no console to send Ctrl+Break through - covered by ShutdownSignalTest" );
#if defined( SW_PLATFORM_WINDOWS )
    expectCleanExit( run, "Interrupt" );
#else
    expectCleanExit( run, "Terminate" );
#endif
}

/**
 * @brief [ServerBootTest] 서버 설정 파일이 없으면 배포 서버는 서지 않는다(기본 포트로 조용히 뜬 서버는 틀린 DB 에 붙는다). Dev 는 기본값으로 선다.
 */
SW_TEST_CASE( ServerBootTest, MissingServerConfigStopsOnlyTheShippingServer )
{
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    const ServerRun run = runServer( "-server-config=Config/Server/does-not-exist.json -gv_serverExitAfterTicks=10", ProcessOptions{}, []( Process& ) {} );
#if defined( SW_SHIPPING )
    SW_EXPECT_TRUE( run._exitCode != 0 );
    SW_EXPECT_TRUE_MSG( run._firstErrorLine.find( "does-not-exist.json" ) != string::npos, run._firstErrorLine.c_str() );
    SW_EXPECT_FALSE( run._bReadySeen );
#else
    expectCleanExit( run, "TickLimit" );
    SW_EXPECT_TRUE( run._output.find( "using built-in defaults" ) != string::npos );
#endif
}
