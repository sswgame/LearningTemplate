#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Connection/NetHostThread.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/UDPNetTransport.h"
#include "Core/Process/Process.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/DebugChannel.h"
#include "Engine/Scene/AuthorityGhost.h"
#include "Engine/Scene/SceneStateHash.h"

#include "TestFramework/Harness/TestProcessRun.h"
#include "TestFramework/TestFramework.h"

#include <chrono>
#include <cmath>
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
    constexpr uint32 kRunTimeoutSeconds = 90;

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

    /** @brief Server 실행 파일의 절대 경로 — 작업 폴더(Bin), 없으면 산출물 폴더(TestBin 옆의 Bin). 없으면 빈 글. */
    string findServerPath()
    {
        string serverPath = FileUtil::joinPath( FileUtil::getCurrentPath(), kServerExecutableName );
        if ( FileUtil::exists( serverPath ) == false )
        {
            const string besideTest = FileUtil::joinPath( FileUtil::getBinaryDirectory(), kServerExecutableName );
            serverPath              = FileUtil::exists( besideTest ) ? besideTest : string{};
        }
        return serverPath;
    }

    /**
     * @brief Server 를 @p arguments 로 띄우고 끝날 때까지 출력을 읽습니다. "ready" 줄을 처음 보면 @p onReady 를 한 번 부릅니다.
     * @details 시한은 공용 하네스(`test::runProcessWithDeadline`)가 지킨다 — 넘으면 서버를 죽이고 `_bTimedOut` 이 선다.
     */
    template <typename TOnReady>
    ServerRun runServer( string_view arguments, const ProcessOptions& options, TOnReady&& onReady )
    {
        ServerRun    run;
        const string serverPath = findServerPath();
        if ( serverPath.empty() )
            return run;
#if defined( SW_PLATFORM_WINDOWS )
        const string command = "\"" + serverPath + "\" " + string( arguments );
#else
        // POSIX 는 `sh -c` 가 명령을 띄운다. `exec` 가 없으면(dash) sh 가 부모로 남아 SIGTERM 에 서버보다 먼저 죽고 종료 코드가 143 이 된다.
        const string command = "exec \"" + serverPath + "\" " + string( arguments );
#endif
        const test::ProcessRunResult result =
            test::runProcessWithDeadline( command, options, kRunTimeoutSeconds, [&run, &onReady]( Process& process, const string& line )
        {
            if ( line.find( "[Error]" ) != string::npos && run._errorCount++ == 0 )
                run._firstErrorLine = line;
            if ( run._bReadySeen == false && line.find( kReadyLine ) != string::npos )
            {
                run._bReadySeen = true;
                onReady( process );
            }
        } );
        run._bLaunched = result._bLaunched;
        run._bTimedOut = result._bTimedOut;
        run._exitCode  = result._exitCode;
        run._output    = result._output;
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

#if !defined( SW_SHIPPING ) // 서비스 포트 테스트만 쓴다 — Shipping 은 그 테스트를 건너뛴다
    /** @brief 아무것도 하지 않는 스트림 처리기 — 빈 TCP 포트를 고르려고 잠깐 여는 전송에 준다. */
    struct NullStreamHandler final : IStreamHandler
    {
        void onStreamOpened( StreamConnectionHandle, const NetAddress&, bool ) override {}
        void onStreamReceived( StreamConnectionHandle, const uint8*, int32 ) override {}
        void onStreamClosed( StreamConnectionHandle, StreamCloseReason ) override {}
    };

    /**
     * @brief 지금 비어 있는 루프백 TCP 포트 번호입니다(받기를 열어 OS 가 고른 번호를 읽고 닫는다). 못 열면 0 입니다.
     * @note UDP 로 고른 번호를 TCP 에 쓰지 말 것 — Windows 는 TCP 포트 구간을 따로 예약해(Hyper-V · WinNAT) 그 번호의 TCP listen 이 10013(WSAEACCES)으로 진다.
     */
    uint16 findFreeStreamPort()
    {
        unique_ptr<IStreamTransport> probe = StreamTransportFactory::createPlatformTransport();
        NullStreamHandler            handler;
        if ( probe == nullptr || probe->initialize( &handler, StreamTransportConfig{} ) == false )
            return 0;
        uint16 port = 0;
        if ( probe->listen( NetAddress::makeLoopback( 0 ) ) )
            port = probe->getListenPort();
        probe->shutdown();
        return port;
    }
#endif
} // namespace

/**
 * @brief [ServerBootTest] 전용 서버가 창 · GPU 없이 서고, 정한 틱을 돌고, 정상 종료한다 — 기동 표의 클라이언트 단계(RHI · 렌더러 · 플레이어 설정)는 돌지 않는다
 * @details CI(nogpu)에서 도는 "실제 서버 프로세스" 테스트다. 시작 씬을 읽어도 `[Error]` 가 0 이어야 한다 — 서버는 텍스처 · 셰이더 바이너리 ·
 *          오디오를 읽지 않고 없는 것으로 친다(쿠킹 표 target_excluded_asset_kinds).
 */
SW_TEST_CASE( ServerBootTest, StartsTicksAndExitsWithoutWindowOrGPU )
{
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    const ServerRun run = runServer( "-gv_serverExitAfterTicks=60", ProcessOptions{}, []( Process& ) {} );
    expectCleanExit( run, "TickLimit" );
    const size_t skippedLine = run._output.find( "startup steps not run" );
    SW_ASSERT_TRUE_MSG( skippedLine != string::npos, "the dedicated server did not report the client-only startup steps it skipped" );
    const string skipped = run._output.substr( skippedLine, run._output.find( '\n', skippedLine ) - skippedLine );
    SW_EXPECT_TRUE( skipped.find( "RHI" ) != string::npos && skipped.find( "FrameRenderer" ) != string::npos && skipped.find( "UserSettings" ) != string::npos );
    // 기동 뒤의 줄에는 서버 틱 번호가 붙는다(멀티 프로세스 재생의 Output Log 가 서버 줄을 틱으로 맞춰 본다) — 기동 중의 줄에는 없다.
    SW_EXPECT_TRUE_MSG( run._output.find( "[tick=60] - Dedicated server shutdown requested (TickLimit) after 60 ticks" ) != string::npos,
                        "the shutdown line does not carry the server tick" );
    SW_EXPECT_TRUE_MSG( run._output.substr( 0, skippedLine ).find( "[tick=" ) == string::npos, "a startup line carries a tick tag before the server is ready" );
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
            { (void)process.writeInput( "status\nquit\n" ); } ); // 못 쓰면 서버가 끝나지 않아 아래 expectCleanExit 가 실패한다
    expectCleanExit( run, "ConsoleCommand" );
    SW_EXPECT_TRUE( run._output.find( "Dedicated server status" ) != string::npos );
}

/**
 * @brief [ServerBootTest] Dev 의 pause 는 게임 틱을 멈추고 step N 은 정확히 N 틱만 돌린다(서버 디버깅 S2 — 디버거 없는 중단점)
 * @details 멈춘 1 초 동안 틱이 돌았다면 step 뒤의 틱 번호가 멈춘 틱 + 3 보다 크다. 재개하면 틱 상한까지 돌아 스스로 끝난다.
 */
SW_TEST_CASE( ServerBootTest, PauseStopsTicksAndStepRunsExactlyN )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "pause and step are development-build console commands" );
#else
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    ProcessOptions options;
    options._bPipeStandardInput = true;
    const ServerRun run         = runServer( "-gv_serverExitAfterTicks=120", options, []( Process& process )
            {
        (void)process.writeInput( "pause\n" ); // 못 쓰면 멈춘 줄이 없어 아래 단언이 실패한다
        std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
        (void)process.writeInput( "step 3\n" ); // 못 쓰면 한 틱 진행 줄이 없어 아래 단언이 실패한다
        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
        (void)process.writeInput( "status\nresume\n" ); // 못 쓰면 서버가 멈춘 채 끝나지 않아 expectCleanExit 가 실패한다
    } );
    expectCleanExit( run, "TickLimit" );
    const string pausedLine  = "Server paused at tick ";
    const string steppedLine = "Server stepped to tick ";
    const size_t pausedAt    = run._output.find( pausedLine );
    const size_t steppedAt   = run._output.find( steppedLine );
    SW_ASSERT_TRUE_MSG( pausedAt != string::npos && steppedAt != string::npos, "the server did not report pause and step" );
    int64 pausedTick  = -1;
    int64 steppedTick = -1;
    SW_ASSERT_TRUE( StringUtil::parseInt64( StringUtil::trim( run._output.substr( pausedAt + pausedLine.size(), run._output.find( '\n', pausedAt ) - pausedAt - pausedLine.size() ) ),
                                            pausedTick ) );
    const size_t steppedEnd = run._output.find( ' ', steppedAt + steppedLine.size() );
    SW_ASSERT_TRUE( StringUtil::parseInt64( run._output.substr( steppedAt + steppedLine.size(), steppedEnd - steppedAt - steppedLine.size() ), steppedTick ) );
    SW_EXPECT_EQUAL( pausedTick + 3, steppedTick );
    const size_t statusAt = run._output.find( "Dedicated server status" );
    SW_ASSERT_TRUE( statusAt != string::npos );
    SW_EXPECT_TRUE( run._output.substr( statusAt, run._output.find( '\n', statusAt ) - statusAt ).find( "(paused)" ) != string::npos );
    SW_EXPECT_TRUE( run._output.find( "Server resumed at tick" ) != string::npos );
#endif
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
            (void)process.writeInput( "quit\n" ); // 못 쓰면 서버가 끝나지 않아 아래 단언이 실패한다
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
 * @brief [ServerBootTest] `-gv_netListenPort` 로 띄운 서버의 게임 인스턴스가 UDP 포트를 열고, 다른 프로세스(이 테스트)의 클라이언트를 받는다(서버 디버깅 S1 의 선행)
 * @details 키트가 없는 게임(Empty)도 네트워크 서버가 된다 — 역할은 게임 코드가 아니라 실행 인자가 정한다(`GameNetSession`). 접속을 서버 로그 ·
 *          개발 명령 `net.status`(Dev) 로 확인하고, quit 뒤에 세션을 닫은 줄이 남는지 본다.
 */
SW_TEST_CASE( ServerBootTest, NetListenPortAcceptsAClientFromAnotherProcess )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "-gv_netListenPort is a development global variable - the shipping server opens its game port from the server config" );
#else
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    uint16 port = 0;
    {
        UDPNetTransport probe; // 비어 있는 포트를 고른다 — 닫은 뒤 서버가 연다
        SW_ASSERT_TRUE( probe.open( 0, true ) );
        port = probe.getLocalAddress()._port;
        probe.close();
    }
    ProcessOptions options;
    options._bPipeStandardInput = true;
    bool            bConnected  = false;
    const ServerRun run         = runServer( "-gv_serverExitAfterTicks=100000 -gv_netListenPort=" + to_string( port ), options, [port, &bConnected]( Process& process )
            {
        UDPNetTransport transport;
        NetHost         client;
        NetHostThread   thread;
        if ( transport.open( 0, true ) )
        {
            client.initialize( &transport, NetHostConfig{} );
            TaskFuture<NetConnectResult> future = client.connectAsync( NetAddress::makeLoopback( port ) );
            if ( thread.start( &client ) )
            {
                const Deadline deadline = Deadline::afterMilliseconds( 10000 );
                while ( future.isReady() == false && deadline.isExpired() == false )
                {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
                }
                bConnected = future.isReady() && future.get().isConnected();
            }
        }
        (void)process.writeInput( "net.status\nquit\n" );                // 못 쓰면 서버가 끝나지 않아 아래 expectCleanExit 가 실패한다
        std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) ); // 끊기 전에 상태 줄이 연결 1 을 보게
        thread.stop();
        client.disconnectAll();
    } );
    expectCleanExit( run, "ConsoleCommand" );
    SW_EXPECT_TRUE( bConnected );
    SW_EXPECT_TRUE_MSG( run._output.find( "Net session listening on UDP port " + to_string( port ) ) != string::npos, "the game instance did not open the port" );
    SW_EXPECT_TRUE_MSG( run._output.find( "Net session client 0 connected from 127.0.0.1" ) != string::npos, "the server did not log the client" );
    #if !defined( SW_SHIPPING )
    SW_EXPECT_TRUE_MSG( run._output.find( "net session: Server, connected 1, port " + to_string( port ) ) != string::npos, "net.status did not report the client" );
    #endif
    SW_EXPECT_TRUE( run._output.find( "Net session closed (Server)" ) != string::npos );
#endif
}

/**
 * @brief [ServerBootTest] 서버 설정 옆 `<이름>.online.json` 이 있으면 서버 서비스가 온라인 서비스(계정 · 디렉터리 · 채팅)를 서비스 포트에서 띄우고, 종료 때 내린다
 * @details 서버 설정과 온라인 설정을 임시 폴더에 쓰고 `-server-config` 로 준다. 서비스 포트는 TLS 1.3 이고 Dev 는 개발용 인증서를 쓴다(배포 서버는 인증서 경로가
 *          필수라 건너뛴다). 연결 없이 기동 · 종료와 로그 줄, status 의 서비스 수를 본다 — 요청 흐름은 OnlineLoadBotTest 가 같은 조립으로 돈다.
 */
SW_TEST_CASE( ServerBootTest, OnlineServicesStartFromTheOnlineConfigBesideTheServerConfig )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the shipping server needs real TLS certificate and key paths for the service port" );
#else
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    const uint16 port = findFreeStreamPort(); // 서비스 포트는 TCP 다 — TCP 로 고른다
    SW_ASSERT_TRUE( port != 0 );
    const string folder = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_server_online_" + to_string( Process::getCurrentProcessID() ) );
    SW_ASSERT_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    const string serverConfigPath = FileUtil::joinPath( folder, "online_test.json" );
    const string onlineConfigPath = FileUtil::joinPath( folder, "online_test.online.json" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( serverConfigPath, R"({ "_listenAddress": "127.0.0.1", "_servicePort": )" + to_string( port ) + " }" ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( onlineConfigPath, R"({
        "_listKit": [
            { "_name": "account", "_listOption": [ { "_key": "lightPasswordHash", "_value": "1" } ] },
            { "_name": "directory" },
            { "_name": "chat", "_listOption": [ { "_key": "bannedWords", "_value": "none" } ] }
        ],
        "_ioThreadCount": 1
    })" ) );
    ProcessOptions options;
    options._bPipeStandardInput = true;
    const ServerRun run         = runServer( "-server-config=\"" + serverConfigPath + "\" -gv_serverExitAfterTicks=60", options, []( Process& process )
            { (void)process.writeInput( "status\n" ); } ); // 못 쓰면 상태 줄이 없어 아래 단언이 실패한다
    (void)FileUtil::removeDirectory( folder );             // 임시 폴더 — 못 지워도 테스트 결과와 상관없다
    expectCleanExit( run, "TickLimit" );
    SW_EXPECT_TRUE_MSG( run._output.find( "Online services listening on port " + to_string( port ) ) != string::npos, "the online services did not start" );
    SW_EXPECT_TRUE_MSG( run._output.find( "kits: account, directory, chat" ) != string::npos, "the kits were not attached in their start order" );
    SW_EXPECT_TRUE_MSG( run._output.find( "services 1 (connections 0)" ) != string::npos, "status did not count the online services" );
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

#if !defined( SW_SHIPPING )
namespace
{
    constexpr const utf8* kDebugToken = "test-token-4711";

    /** @brief 디버그 채널을 연 서버를 띄우는 실행 인자입니다(루프백 · 토큰 · 상태 해시 이력). */
    string makeDebugServerArguments( uint16 port )
    {
        return "-gv_serverExitAfterTicks=100000 -gv_stateHashHistory=2000 -gv_debugChannelPort=" + to_string( port ) + " -gv_debugChannelToken=" + string( kDebugToken );
    }

    /** @brief 서버가 시작 씬을 올릴 때까지(`ready` 는 씬 교체보다 먼저다) @p command 를 되풀이해 성공하면 true. */
    bool requestUntilOk( DebugChannelClient& client, string_view command, string& outBody )
    {
        const int64 deadline = MonotonicClock::nowNanoseconds() + 10000LL * 1000000LL;
        while ( MonotonicClock::nowNanoseconds() < deadline )
        {
            if ( client.request( command, 5000, outBody ) )
                return true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
        return false;
    }

    /** @brief 서버의 `scene.poses` 답에서 첫 줄의 오브젝트 이름입니다(없으면 빈 글). */
    string findFirstPoseName( DebugChannelClient& client )
    {
        string body;
        if ( requestUntilOk( client, "scene.poses", body ) == false )
            return {};
        return body.substr( 0, body.find( ' ' ) );
    }

    /** @brief 서버가 @p tick 틱까지 돌아 해시를 남길 때까지 기다립니다. 시한 안에 못 하면 false. */
    bool waitForTick( DebugChannelClient& client, uint64 tick, int32 timeoutMilli )
    {
        const int64 deadline = MonotonicClock::nowNanoseconds() + static_cast<int64>( timeoutMilli ) * 1000000LL;
        string      body;
        while ( MonotonicClock::nowNanoseconds() < deadline )
        {
            if ( client.request( "state.hashes " + to_string( tick ) + " 1", 5000, body ) && body.empty() == false )
                return true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        }
        return false;
    }

    /** @brief `paused tick N` 답의 N 입니다(못 읽으면 0). */
    uint64 parsePausedTick( const string& reply )
    {
        const size_t pos  = reply.rfind( ' ' );
        uint64       tick = 0;
        if ( pos == string::npos || StringUtil::parseUint64( reply.substr( pos + 1 ), tick ) == false )
            return 0;
        return tick;
    }
} // namespace
#endif

/**
 * @brief [ServerBootTest] 디버그 채널로 붙어 서버의 오브젝트 트리 · 컴포넌트 값을 읽고, 서버에서 바꾼 값이 검사기에 보이고, 잘못된 토큰은 거절된다(서버 디버깅 S3)
 * @details 확인 시나리오: 서버를 띄움 -> 채널에 연결 -> `scene.tree` · `object.dump` 로 읽음 -> 멈춘(`pause`) 서버에서 `teleport` 로 값을 바꿈 -> `scene.poses` · `object.dump` 에 새 값이 보임 -> `help` 가 받는 줄을 알림.
 *          채널의 `pause` 는 서버 루프의 일시정지 명령이라 멈춘 동안에도 요청을 받는다(중단점에서 값을 보는 쓰임).
 */
SW_TEST_CASE( ServerBootTest, DebugChannelReadsObjectValuesAndRejectsAWrongToken )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the debug channel is a development-build feature" );
#else
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    const uint16 port = findFreeStreamPort();
    SW_ASSERT_TRUE( port != 0 );
    ProcessOptions options;
    options._bPipeStandardInput = true;
    string          treeBody;
    string          dumpBefore;
    string          dumpAfter;
    string          posesAfter;
    string          wrongTokenBody;
    string          unknownBody;
    bool            bWrongTokenRejected = false;
    bool            bUnknownFailed      = false;
    bool            bConnected          = false;
    bool            bTeleported         = false;
    string          objectName;
    const ServerRun run = runServer( makeDebugServerArguments( port ), options, [&]( Process& process )
    {
        DebugChannelClient client;
        bConnected = client.connect( NetAddress::makeLoopback( port ), kDebugToken, 10000 );
        if ( bConnected )
        {
            string reply;
            (void)requestUntilOk( client, "scene.tree", treeBody );
            objectName = findFirstPoseName( client );
            (void)client.request( "object.dump " + objectName, 5000, dumpBefore );
            bool bPaused = client.request( "pause", 5000, reply ) && reply.find( "paused tick" ) == 0;
            bTeleported  = bPaused && client.request( "teleport " + objectName + " 11 22 33", 5000, reply );
            (void)client.request( "scene.poses", 5000, posesAfter );
            (void)client.request( "object.dump " + objectName, 5000, dumpAfter );
            bUnknownFailed = client.request( "no.such.command", 5000, unknownBody ) == false;
        }
        DebugChannelClient intruder;
        if ( intruder.connect( NetAddress::makeLoopback( port ), "wrong-token", 10000 ) )
            bWrongTokenRejected = intruder.request( "scene.tree", 5000, wrongTokenBody ) == false && wrongTokenBody == "unauthorized";
        (void)process.writeInput( "quit\n" ); // 못 쓰면 서버가 끝나지 않아 아래 expectCleanExit 가 실패한다
    } );
    expectCleanExit( run, "ConsoleCommand" );
    SW_ASSERT_TRUE_MSG( bConnected, "the debug channel did not accept the client" );
    string          helpBody;
    SW_EXPECT_TRUE_MSG( run._output.find( "Debug channel listening on 127.0.0.1:" + to_string( port ) ) != string::npos, "the server did not log the debug channel" );
    SW_EXPECT_TRUE_MSG( treeBody.find( "objects 8" ) == 0 && treeBody.find( "components=" ) != string::npos, treeBody.c_str() );
    SW_ASSERT_TRUE_MSG( objectName.empty() == false, "scene.poses named no object" );
    SW_EXPECT_TRUE_MSG( dumpBefore.find( "[" ) != string::npos && dumpBefore.find( "object " + objectName ) == 0, dumpBefore.c_str() );
    SW_EXPECT_TRUE_MSG( bTeleported, "pause or teleport through the channel failed" );
    SW_EXPECT_TRUE_MSG( posesAfter.find( objectName + " 11 22 33" ) != string::npos, posesAfter.c_str() );
    SW_EXPECT_TRUE_MSG( dumpAfter != dumpBefore && dumpAfter.find( "11" ) != string::npos, "the inspector did not show the changed position" );
    SW_EXPECT_TRUE_MSG( bUnknownFailed, unknownBody.c_str() );
    SW_EXPECT_TRUE_MSG( bWrongTokenRejected, wrongTokenBody.c_str() );
    SW_EXPECT_TRUE( run._output.find( "Debug channel rejected a connection" ) != string::npos );
#endif
}

/**
 * @brief [ServerBootTest] 디버그 채널은 토큰 없이는 열리지 않는다 - 서버는 기동에 실패한다
 */
SW_TEST_CASE( ServerBootTest, DebugChannelRefusesToStartWithoutAToken )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the debug channel is a development-build feature" );
            (void)client.request( "help", 5000, helpBody );
#else
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    const uint16 port = findFreeStreamPort();
    SW_ASSERT_TRUE( port != 0 );
    const ServerRun run = runServer( "-gv_serverExitAfterTicks=100000 -gv_debugChannelPort=" + to_string( port ), ProcessOptions{}, []( Process& ) {} );
    SW_EXPECT_TRUE( run._bLaunched );
    SW_EXPECT_FALSE( run._bTimedOut );
    SW_EXPECT_TRUE( run._exitCode != 0 );
    SW_EXPECT_FALSE( run._bReadySeen );
    SW_EXPECT_TRUE_MSG( run._output.find( "needs a token" ) != string::npos, "the server did not say why the channel was refused" );
#endif
}

/**
 * @brief [ServerBootTest] 두 서버의 틱별 상태 해시를 비교해 처음 어긋난 틱을 맞게 짚는다(서버 디버깅 S7)
    // 채널 help 는 채널이 받는 줄 전부 — 서버 루프 명령과 개발 명령 이름(에디터 Inspector 탭의 자동완성 원천)을 함께 답한다.
    SW_EXPECT_TRUE_MSG( helpBody.find( "server loop: pause | resume | step [N]" ) == 0 && helpBody.find( "scene.tree" ) != string::npos, helpBody.c_str() );
 * @details 서버 A 는 그대로 두고, 서버 B 는 멈춘 틱 T 에서 4 틱 진행한 뒤 오브젝트를 옮기고 6 틱을 더 돈다. 어긋난 첫 틱은 T + 5 여야 하고, 그 앞 틱들은 두 서버가 같아야 한다
 *          (씬은 결정적이다). 채널의 `pause` · `step` 으로 틱을 정확히 세므로 시간에 기대지 않는다.
 */
SW_TEST_CASE( ServerBootTest, StateHashHistoryFindsTheFirstDivergentTick )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the debug channel is a development-build feature" );
#else
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    ProcessOptions options;
    options._bPipeStandardInput = true;
    struct Result
    {
        string _hashes;
        uint64 _pausedTick{ 0 };
        bool   _bOk{ false };
    };
    // 한 서버를 돌려 멈춘 틱과 이력을 얻는다. @p bPerturb 이면 4 틱 뒤 오브젝트를 옮긴다.
    auto runOne = [&options]( bool bPerturb, uint32 totalSteps ) -> Result
    {
        Result          result;
        const uint16    port = findFreeStreamPort();
        const ServerRun run  = runServer( makeDebugServerArguments( port ), options, [&]( Process& process )
         {
            DebugChannelClient client;
            if ( client.connect( NetAddress::makeLoopback( port ), kDebugToken, 10000 ) )
            {
                string reply;
                // 씬이 선 뒤에 멈춘다 - 그 전의 틱은 해시를 남기지 않는다
                if ( requestUntilOk( client, "scene.tree", reply ) && client.request( "pause", 5000, reply ) )
                {
                    result._pausedTick      = parsePausedTick( reply );
                    const uint32 firstSteps = bPerturb ? 4 : totalSteps;
                    bool         bGood      = client.request( "step " + to_string( firstSteps ), 5000, reply ) && waitForTick( client, result._pausedTick + firstSteps, 10000 );
                    if ( bGood && bPerturb )
                    {
                        const string name = findFirstPoseName( client );
                        bGood             = client.request( "teleport " + name + " 50 0 0", 5000, reply ) && client.request( "step " + to_string( totalSteps - firstSteps ), 5000, reply ) &&
                                waitForTick( client, result._pausedTick + totalSteps, 10000 );
                    }
                    result._bOk = bGood && client.request( "state.hashes 0 100000", 5000, result._hashes );
                }
            }
            (void)process.writeInput( "quit\n" ); // 못 쓰면 서버가 끝나지 않아 아래 expectCleanExit 가 실패한다
        } );
        expectCleanExit( run, "ConsoleCommand" );
        return result;
    };
    const Result serverA = runOne( false, 40 );
    const Result serverB = runOne( true, 12 );
    SW_ASSERT_TRUE_MSG( serverA._bOk && serverB._bOk, "a server run could not be driven through the debug channel" );
    SW_ASSERT_TRUE( serverA._pausedTick > 0 && serverB._pausedTick > 0 );
    vector<StateHashSample> listA;
    vector<StateHashSample> listB;
    StateHashHistory::parseReport( serverA._hashes, listA );
    StateHashHistory::parseReport( serverB._hashes, listB );
    SW_ASSERT_TRUE_MSG( listA.size() >= 40 && listB.size() >= 12, "a server recorded fewer tick hashes than it was stepped" );
    uint64 firstDivergentTick = 0;
    SW_ASSERT_TRUE_MSG( StateHashHistory::findFirstDivergence( listA, listB, firstDivergentTick ), "the two servers never diverged" );
    // 옮긴 뒤 처음 도는 틱이 첫 어긋난 틱이다. 두 서버가 멈춘 틱은 다를 수 있으므로 서버 B 기준 T_B + 5 이다.
    SW_EXPECT_EQUAL( serverB._pausedTick + 5, firstDivergentTick );
    SW_EXPECT_TRUE_MSG( serverB._pausedTick + 5 <= serverA._pausedTick + 40, "server A did not run far enough to compare" );
#endif
}

/**
 * @brief [AuthorityGhostTest] 서버 포즈 글을 읽고, 이름이 같은 오브젝트끼리 거리 차이를 구한다(서버 디버깅 S6의 순수 부분)
 * @details 권위 고스트는 개발 빌드 기능이라 Shipping 에는 본문(AuthorityGhost.cpp)이 없다 — 구성 기능으로 선언해 건너뛰고(요약에 세어진다) 본문만 `#if` 로 감싼다.
 */
SW_TEST_CASE( AuthorityGhostTest, ParsesPosesAndComputesDeltasByName )
{
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild );
#if !defined( SW_SHIPPING )
    vector<AuthorityPose> listServer;
    AuthorityGhost::parsePoses( "Hero Knight 1 2 3\nTree 10 0 0\nbad line\nOnlyServer 5 5 5\n", listServer );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listServer.size() ) );
    SW_EXPECT_TRUE( listServer[0]._name == "Hero Knight" ); // 이름은 공백을 품을 수 있다
    vector<AuthorityPose> listLocal;
    AuthorityGhost::parsePoses( "Hero Knight 1 2 3\nTree 10 0 4\nOnlyLocal 0 0 0\n", listLocal );
    vector<AuthorityDelta> listDelta;
    AuthorityGhost::computeDeltas( listLocal, listServer, listDelta );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listDelta.size() ) ); // 한쪽에만 있는 것은 건너뛴다
    SW_EXPECT_TRUE( listDelta[0]._name == "Tree" );                 // 가장 어긋난 것이 먼저
    SW_EXPECT_TRUE( listDelta[0]._distance > 3.999f && listDelta[0]._distance < 4.001f );
    SW_EXPECT_TRUE( listDelta[1]._distance == 0.0f );
#endif
}

/**
 * @brief [ServerBootTest] 클라이언트의 로컬 포즈와 서버가 읽어 준 포즈의 차이가 서버에서 옮긴 거리와 같다(서버 디버깅 S6)
 * @details 확인 시나리오: 서버의 `scene.poses` 를 "내 화면"으로 삼고 -> 서버에서 오브젝트를 옮기고 -> 다시 읽은 서버 포즈와 비교해 차이가 옮긴 거리로 나온다.
 */
SW_TEST_CASE( ServerBootTest, AuthorityGhostDeltaMatchesHowFarTheServerMovedAnObject )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the debug channel is a development-build feature" );
#else
    if ( findServerPath().empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );
    const uint16 port = findFreeStreamPort();
    SW_ASSERT_TRUE( port != 0 );
    ProcessOptions options;
    options._bPipeStandardInput = true;
    string          beforeText;
    string          afterText;
    string          objectName;
    bool            bMoved = false;
    const ServerRun run    = runServer( makeDebugServerArguments( port ), options, [&]( Process& process )
       {
        DebugChannelClient client;
        if ( client.connect( NetAddress::makeLoopback( port ), kDebugToken, 10000 ) )
        {
            objectName = findFirstPoseName( client );
            string reply;
            (void)requestUntilOk( client, "scene.poses", beforeText );
            bMoved = client.request( "teleport " + objectName + " 3 4 12", 5000, reply );
            (void)client.request( "scene.poses", 5000, afterText );
        }
        (void)process.writeInput( "quit\n" ); // 못 쓰면 서버가 끝나지 않아 아래 expectCleanExit 가 실패한다
    } );
    expectCleanExit( run, "ConsoleCommand" );
    SW_ASSERT_TRUE_MSG( bMoved, "the server could not move the object" );
    vector<AuthorityPose> listLocal;
    vector<AuthorityPose> listServer;
    AuthorityGhost::parsePoses( beforeText, listLocal );
    AuthorityGhost::parsePoses( afterText, listServer );
    vector<AuthorityDelta> listDelta;
    AuthorityGhost::computeDeltas( listLocal, listServer, listDelta );
    SW_ASSERT_TRUE( listDelta.empty() == false );
    SW_EXPECT_TRUE( listDelta[0]._name == objectName );
    const AuthorityPose* pBefore = nullptr;
    for ( const AuthorityPose& pose : listLocal )
    {
        pBefore = pose._name == objectName ? &pose : pBefore;
    }
    SW_ASSERT_TRUE( pBefore != nullptr );
    const float32 dx       = 3.0f - pBefore->_position._x;
    const float32 dy       = 4.0f - pBefore->_position._y;
    const float32 dz       = 12.0f - pBefore->_position._z;
    const float32 expected = std::sqrt( dx * dx + dy * dy + dz * dz );
    SW_EXPECT_TRUE_MSG( listDelta[0]._distance > expected - 0.001f && listDelta[0]._distance < expected + 0.001f, "the reported difference is not the distance the server moved the object" );
#endif
}
