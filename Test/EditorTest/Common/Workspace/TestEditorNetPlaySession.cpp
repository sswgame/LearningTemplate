#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Editor/Common/Workspace/EditorNetPlaySession.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 접속 · 일시정지를 기다리는 시한(초) — 프로세스 셋이 Debug 로 기동한다. 횟수가 아니라 시간이다. */
    constexpr int64 kWaitTimeoutMilliseconds = 60000;

    /** @brief 빌드한 Server 실행 파일 — 작업 폴더(Bin), 없으면 테스트 바이너리 옆. 없으면 빈 글. */
    string findServerPath()
    {
#if defined( SW_PLATFORM_WINDOWS )
        constexpr const utf8* kServerExecutableName = "Server.exe";
#else
        constexpr const utf8* kServerExecutableName = "Server";
#endif
        string serverPath = FileUtil::joinPath( FileUtil::getCurrentPath(), kServerExecutableName );
        if ( FileUtil::exists( serverPath ) == false )
        {
            const string besideTest = FileUtil::joinPath( FileUtil::getBinaryDirectory(), kServerExecutableName );
            serverPath              = FileUtil::exists( besideTest ) ? besideTest : string{};
        }
        return serverPath;
    }

    /** @brief 모은 줄 가운데 @p processIndex 의 것에 @p pText 가 든 줄 수입니다. */
    int32 countLines( const EditorNetPlaySession& session, int32 processIndex, const utf8* pText )
    {
        int32 count = 0;
        for ( const EditorNetPlayLogLine& line : session.getLogLines() )
        {
            if ( line._processIndex == processIndex && line._text.find( pText ) != string::npos )
                ++count;
        }
        return count;
    }

    /** @brief @p processIndex 의 줄에 @p pText 가 나올 때까지 `update` 를 돌립니다(시한 안에 나오면 true). */
    bool waitForLine( EditorNetPlaySession& session, int32 processIndex, const utf8* pText )
    {
        const Deadline deadline = Deadline::afterMilliseconds( kWaitTimeoutMilliseconds );
        while ( deadline.isExpired() == false )
        {
            session.update();
            if ( countLines( session, processIndex, pText ) > 0 )
                return true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
        return false;
    }

    /** @brief 모은 줄 가운데 @p processIndex 의 것에 붙은 가장 큰 틱 번호(`[tick=N]`)입니다. 없으면 0. */
    int64 highestTick( const EditorNetPlaySession& session, int32 processIndex )
    {
        int64 highest = 0;
        for ( const EditorNetPlayLogLine& line : session.getLogLines() )
        {
            const size_t tagPosition = line._processIndex == processIndex ? line._text.find( "[tick=" ) : string::npos;
            if ( tagPosition == string::npos )
                continue;
            const string_view afterTag = string_view( line._text ).substr( tagPosition + 6 );
            int64             tick     = 0;
            if ( StringUtil::parseInt64( afterTag.substr( 0, afterTag.find( ']' ) ), tick ) )
                highest = std::max<int64>( highest, tick );
        }
        return highest;
    }

    /**
     * @brief @p processIndex 가 틱을 하나 끝낼 때까지 기다립니다(시한 안에 끝내면 true).
     * @details 종료 트레이스는 끝난 프레임이 하나는 있어야 써진다. 접속 줄은 첫 틱 안에서 나오므로 그것만 기다리고 끄면, 느린 구성(TSan)에서 트레이스가 빠진다.
     *          `status` 는 그 프로세스의 현재 틱 번호를 단 줄을 내므로 줄이 없는 동안 계속 보낸다. 틱 번호 N 은 N-1 틱까지 끝났다는 뜻이다.
     */
    bool waitForFinishedTick( EditorNetPlaySession& session, int32 processIndex )
    {
        const Deadline deadline = Deadline::afterMilliseconds( kWaitTimeoutMilliseconds );
        while ( deadline.isExpired() == false )
        {
            (void)session.sendProcessCommand( processIndex, "status" );
            session.update();
            if ( highestTick( session, processIndex ) >= 2 )
                return true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
        return false;
    }
} // namespace

/**
 * @brief [EditorNetPlaySessionTest] 전용 서버 + 클라이언트 둘을 띄우면 둘 다 붙고, 서버 일시정지가 표준 입력으로 닿고, Stop 이 모두를 정상 종료시키고
 *        프로세스마다의 트레이스를 이름(Server · Client1 · Client2)를 단 한 파일로 합친다(서버 디버깅 S1)
 * @details 창 · GPU 가 없는 테스트라 클라이언트도 `Server` 실행 파일로 띄운다(헤드리스 클라이언트 — 게임 인스턴스의 네트워크 역할은 실행 인자가 정한다).
 *          에디터가 `App` 클라이언트로 같은 일을 하는 것은 에디터 시나리오 `netplay_dedicated` 가 본다.
 */
SW_TEST_CASE( EditorNetPlaySessionTest, DedicatedServerAndTwoClientsConnectPauseAndMergeTraces )
{
    const string serverPath = findServerPath();
    if ( serverPath.empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );

    const string traceDirectory =
        FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_netplay_" + to_string( Process::getCurrentProcessID() ) + "_" + to_string( WallClock::nowUnixMilliseconds() ) );
    EditorNetPlaySettings settings;
    settings._mode                 = EditorNetMode::DedicatedServer;
    settings._clientCount          = 2;
    settings._serverExecutablePath = serverPath;
    settings._clientExecutablePath = serverPath;
    settings._traceDirectory       = traceDirectory;
    settings._bRecordTraces        = true;

    EditorNetPlaySession session;
    SW_ASSERT_TRUE( session.start( settings ) );
    SW_EXPECT_EQUAL( 3, session.getProcessCount() );
    SW_EXPECT_TRUE( session.getPort() != 0 );

    SW_EXPECT_TRUE_MSG( waitForLine( session, 1, "Net session connected to" ), "Client1 did not connect" );
    SW_EXPECT_TRUE_MSG( waitForLine( session, 2, "Net session connected to" ), "Client2 did not connect" );
    SW_EXPECT_TRUE_MSG( waitForLine( session, 0, "(2 connected)" ), "the server did not see both clients" );
    SW_EXPECT_EQUAL( 2, session.getConnectedClientCount() );

    // 서버 일시정지 · 재개는 서버 표준 입력의 Dev 명령이다.
    SW_EXPECT_TRUE( session.sendServerCommand( "pause" ) );
    SW_EXPECT_TRUE_MSG( waitForLine( session, 0, "Server paused at tick" ), "pause did not reach the server" );
    SW_EXPECT_TRUE( session.sendServerCommand( "resume" ) );
    SW_EXPECT_TRUE_MSG( waitForLine( session, 0, "Server resumed at tick" ), "resume did not reach the server" );
    // 모은 서버 줄에는 서버 틱 번호가 붙어 있다 — Output Log 에서 클라이언트 줄과 틱으로 맞춰 본다.
    SW_EXPECT_TRUE_MSG( waitForLine( session, 0, "[tick=" ), "the server lines in the session log carry no tick number" );

    // 끄기 전에 프로세스마다 틱 하나는 끝나야 한다 — 아니면 종료 트레이스가 "끝난 프레임 없음" 으로 빠진다(TSan 에서 Client1 이 그랬다).
    for ( int32 processIndex = 0; processIndex < session.getProcessCount(); ++processIndex )
    {
        SW_EXPECT_TRUE_MSG( waitForFinishedTick( session, processIndex ), "a process did not finish a tick before the stop" );
    }

    const string mergedPath = session.stop();
    SW_EXPECT_FALSE( session.isRunning() );
    SW_ASSERT_TRUE_MSG( mergedPath.empty() == false, "the traces were not merged" );
    string merged;
    SW_ASSERT_TRUE( FileUtil::readTextFile( mergedPath, merged ) );
    SW_EXPECT_TRUE( merged.find( "\"label\":\"Server\"" ) != string::npos );
    SW_EXPECT_TRUE( merged.find( "\"label\":\"Client1\"" ) != string::npos );
    SW_EXPECT_TRUE( merged.find( "\"label\":\"Client2\"" ) != string::npos );
    SW_EXPECT_TRUE_MSG( merged.find( "same host (exact)" ) != string::npos, "the traces were not aligned by the shared monotonic clock" );
    (void)FileUtil::removeDirectory( traceDirectory ); // 임시 폴더 — 못 지워도 테스트 결과와 상관없다
}

/**
 * @brief [EditorNetPlaySessionTest] 회선 조절이 표준 입력으로 클라이언트에 닿는다 — 손실 30 % 에서 신뢰 메시지가 다시 보내지고, 보고 줄이 세션에 모이고,
 *        끊기 · 재접속이 그 프로세스의 연결을 끊었다 다시 붙인다(서버 디버깅 S4)
 * @details 클라이언트는 `Server` 실행 파일(헤드리스)이다. 에디터 Conditions 탭과 `netplay.command` 가 보내는 것과 같은 개발 명령(`net.emulation` · `net.traffic` ·
 *          `net.disconnect` · `net.reconnect`)을 보낸다. 에디터가 `App` 클라이언트로 같은 일을 하는 것은 에디터 시나리오 `netconditions` 가 본다.
 */
SW_TEST_CASE( EditorNetPlaySessionTest, LineConditionsReachAClientAndItsReportShowsResends )
{
    const string serverPath = findServerPath();
    if ( serverPath.empty() )
        SW_TEST_SKIP( "Server is not built next to the working directory (run from Bin)" );

    EditorNetPlaySettings settings;
    settings._mode                 = EditorNetMode::DedicatedServer;
    settings._clientCount          = 1;
    settings._serverExecutablePath = serverPath;
    settings._clientExecutablePath = serverPath;
    settings._bRecordTraces        = false;

    EditorNetPlaySession session;
    SW_ASSERT_TRUE( session.start( settings ) );
    SW_ASSERT_TRUE_MSG( waitForLine( session, 1, "Net session connected to" ), "Client1 did not connect" );

    // 두 방향 손실 30 % — 클라이언트가 보낸 것도, 받는 확인도 잃는다. 그 위에 신뢰 메시지를 흘려 재전송을 만든다.
    SW_ASSERT_TRUE( session.sendProcessCommand( 1, "net.emulation both lossPercent=30" ) );
    SW_EXPECT_TRUE_MSG( waitForLine( session, 1, "net emulation up [latencyMs=0 jitterMs=0 lossPercent=30" ), "net.emulation did not reach the client" );
    SW_ASSERT_TRUE( session.sendProcessCommand( 1, "net.traffic 100 1000" ) ); // 메시지 하나가 패킷 하나 — 손실이 반드시 맞는다
    SW_EXPECT_TRUE_MSG( waitForLine( session, 1, "net traffic queued 100 reliable message(s) to 1 connection(s)" ), "net.traffic did not reach the client" );

    // 재전송은 RTT 뒤에 일어난다 — 보고를 다시 물으며 시한 안에 재전송 · 버림이 보일 때까지 기다린다.
    const Deadline     deadline = Deadline::afterMilliseconds( kWaitTimeoutMilliseconds );
    NetEmulationReport report;
    while ( deadline.isExpired() == false )
    {
        const uint32 reportCount = session.getProcessStatus( 1 )._emulationReportCount;
        SW_ASSERT_TRUE( session.sendProcessCommand( 1, "net.emulation stats" ) );
        while ( session.getProcessStatus( 1 )._emulationReportCount == reportCount && deadline.isExpired() == false )
        {
            session.update();
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
        report = session.getProcessStatus( 1 )._emulationReport;
        if ( report._resentMessageCount > 0 && report._droppedPacketCount > 0 )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    }
    SW_EXPECT_TRUE_MSG( session.getProcessStatus( 1 )._emulationReportCount > 0, "the client never answered net.emulation stats" );
    SW_EXPECT_EQUAL( report._connectionCount, 1 );
    SW_EXPECT_TRUE_MSG( report._droppedPacketCount > 0, "30% loss dropped no packet" );
    SW_EXPECT_TRUE_MSG( report._resentMessageCount > 0, "30% loss resent no reliable message" );

    // 끊기 · 재접속 — 조건을 끄고 나서(핸드셰이크를 잃지 않게) 끊고 다시 붙인다.
    SW_ASSERT_TRUE( session.sendProcessCommand( 1, "net.emulation both off" ) );
    SW_ASSERT_TRUE( session.sendProcessCommand( 1, "net.disconnect" ) );
    SW_EXPECT_TRUE_MSG( waitForLine( session, 1, "Net session disconnected its peers on request" ), "net.disconnect did not reach the client" );
    SW_EXPECT_TRUE_MSG( waitForLine( session, 0, "disconnected (" ), "the server did not see the client leave" );
    SW_ASSERT_TRUE( session.sendProcessCommand( 1, "net.reconnect" ) );
    const Deadline reconnectDeadline = Deadline::afterMilliseconds( kWaitTimeoutMilliseconds );
    while ( countLines( session, 1, "Net session connected to" ) < 2 && reconnectDeadline.isExpired() == false )
    {
        session.update();
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }
    SW_EXPECT_EQUAL( countLines( session, 1, "Net session connected to" ), 2 );
    (void)session.stop(); // 트레이스를 끈 세션이라 합친 경로는 없다
}

/**
 * @brief [EditorNetPlaySessionTest] Standalone 은 프로세스를 띄우지 않는다(에디터 안 재생)
 */
SW_TEST_CASE( EditorNetPlaySessionTest, StandaloneLaunchesNothing )
{
    EditorNetPlaySettings settings;
    settings._mode = EditorNetMode::Standalone;
    EditorNetPlaySession session;
    SW_EXPECT_FALSE( session.start( settings ) );
    SW_EXPECT_EQUAL( 0, session.getProcessCount() );
}

/**
 * @brief [EditorNetPlaySessionTest] `App` 창마다 크기와 나란한 위치 인자를 준다 — 한 줄에 열 수만큼 놓고 다음 줄로 내려가며, 칸 사이는 창 틀만큼 띄운다
 * @details 언리얼 PIE 가 클라이언트 창을 겹치지 않게 놓는 자리다. 창 배치를 끈 구성은 인자를 주지 않는다(창은 플랫폼이 놓는다).
 */
SW_TEST_CASE( EditorNetPlaySessionTest, WindowArgumentsTileTheAppWindows )
{
    EditorNetPlaySettings settings;
    settings._windowWidth       = 800;
    settings._windowHeight      = 450;
    settings._windowColumnCount = 2;
    SW_EXPECT_TRUE( ( EditorNetPlaySession::makeWindowArguments( settings, 0 ) == vector<string>{ "-W=800", "-H=450", "-WinX=0", "-WinY=0" } ) );
    SW_EXPECT_TRUE( ( EditorNetPlaySession::makeWindowArguments( settings, 1 ) == vector<string>{ "-W=800", "-H=450", "-WinX=816", "-WinY=0" } ) );
    SW_EXPECT_TRUE( ( EditorNetPlaySession::makeWindowArguments( settings, 2 ) == vector<string>{ "-W=800", "-H=450", "-WinX=0", "-WinY=490" } ) );
    SW_EXPECT_TRUE( ( EditorNetPlaySession::makeWindowArguments( settings, 3 ) == vector<string>{ "-W=800", "-H=450", "-WinX=816", "-WinY=490" } ) );
    settings._bArrangeWindows = false;
    SW_EXPECT_TRUE( EditorNetPlaySession::makeWindowArguments( settings, 1 ).empty() );
}

/**
 * @brief [EditorNetPlaySessionTest] 쓸 수 없는 창 배치(열 0 · 너무 작은 창)는 띄우기 전에 멈춘다 — 조용히 1 열로 고치지 않는다
 */
SW_TEST_CASE( EditorNetPlaySessionTest, UnusableWindowLayoutLaunchesNothing )
{
    SW_TEST_DEFENSIVE_SCOPE( "an unusable window layout stops the launch" );
    EditorNetPlaySettings settings;
    settings._mode              = EditorNetMode::DedicatedServer;
    settings._bRecordTraces     = false;
    settings._windowColumnCount = 0;
    EditorNetPlaySession session;
    SW_EXPECT_FALSE( session.start( settings ) );
    SW_EXPECT_EQUAL( 0, session.getProcessCount() );
    settings._windowColumnCount = 2;
    settings._windowWidth       = 10;
    SW_EXPECT_FALSE( session.start( settings ) );
    SW_EXPECT_EQUAL( 0, session.getProcessCount() );
}

/**
 * @brief [EditorNetPlaySessionTest] 없는 실행 파일이면 띄우지 않고 false — 반쯤 띄운 프로세스를 남기지 않는다
 */
SW_TEST_CASE( EditorNetPlaySessionTest, MissingExecutableFailsWithoutLeavingProcesses )
{
    EditorNetPlaySettings settings;
    settings._mode                 = EditorNetMode::DedicatedServer;
    settings._serverExecutablePath = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_no_such_server.exe" );
    settings._clientExecutablePath = settings._serverExecutablePath;
    settings._bRecordTraces        = false;
    EditorNetPlaySession session;
    SW_EXPECT_FALSE( session.start( settings ) );
    SW_EXPECT_FALSE( session.isRunning() );
    SW_EXPECT_EQUAL( 0, session.getProcessCount() );
}
