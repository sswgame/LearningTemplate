#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Editor/Common/Commands/EditorNetPlaySession.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 접속 · 일시정지를 기다리는 시한(초) — 프로세스 셋이 Debug 로 기동한다. 횟수가 아니라 시간이다. */
    constexpr int64 kWaitTimeoutMilliseconds = 60000;

    /** @brief 빌드한 Server 실행 파일 — 작업 폴더(Bin), 없으면 시험 바이너리 옆. 없으면 빈 글. */
    string findServerPath()
    {
#if defined( SW_PLATFORM_WINDOWS )
        constexpr const utf8* kServerExecutableName = "Server.exe";
#else
        constexpr const utf8* kServerExecutableName = "Server";
#endif
        const string inBin = FileUtil::joinPath( FileUtil::getCurrentPath(), kServerExecutableName );
        if ( FileUtil::exists( inBin ) )
            return inBin;
        const string besideTest = FileUtil::joinPath( FileUtil::getBinaryDirectory(), kServerExecutableName );
        return FileUtil::exists( besideTest ) ? besideTest : string{};
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
} // namespace

/**
 * @brief [EditorNetPlaySessionTest] 전용 서버 + 클라이언트 둘을 띄우면 둘 다 붙고, 서버 일시정지가 표준 입력으로 닿고, Stop 이 모두를 정상 종료시키고
 *        프로세스마다의 트레이스를 이름표(Server · Client1 · Client2)를 단 한 파일로 합친다(서버 디버깅 S1)
 * @details 창 · GPU 가 없는 시험이라 클라이언트도 `Server` 실행 파일로 띄운다(헤드리스 클라이언트 — 게임 인스턴스의 네트워크 역할은 실행 인자가 정한다).
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

    const string mergedPath = session.stop();
    SW_EXPECT_FALSE( session.isRunning() );
    SW_ASSERT_TRUE_MSG( mergedPath.empty() == false, "the traces were not merged" );
    string merged;
    SW_ASSERT_TRUE( FileUtil::readTextFile( mergedPath, merged ) );
    SW_EXPECT_TRUE( merged.find( "\"label\":\"Server\"" ) != string::npos );
    SW_EXPECT_TRUE( merged.find( "\"label\":\"Client1\"" ) != string::npos );
    SW_EXPECT_TRUE( merged.find( "\"label\":\"Client2\"" ) != string::npos );
    SW_EXPECT_TRUE_MSG( merged.find( "same host (exact)" ) != string::npos, "the traces were not aligned by the shared monotonic clock" );
    (void)FileUtil::removeDirectory( traceDirectory ); // 임시 폴더 — 못 지워도 시험 결과와 상관없다
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
