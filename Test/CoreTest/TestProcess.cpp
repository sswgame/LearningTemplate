#include "pch.h"

#include "Core/Process/Process.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Process — 프로세스 생성, 라인 읽기, 실행 헬퍼, 강제 종료
// ------------------------------------------------------------------------------
/**
 * @brief [ProcessTest] 프로세스 생성 및 표준 출력 라인 읽기
 */

SW_TEST_CASE( ProcessTest, LaunchAndReadOutput )
{
    sw::Process proc;
#if defined( SW_PLATFORM_WINDOWS )
    const sw::string cmd = "cmd.exe /c echo SW_PROCESS_TEST_OUTPUT";
#else
    const sw::string cmd = "echo SW_PROCESS_TEST_OUTPUT";
#endif

    const bool bLaunched = proc.launch( cmd );
    SW_EXPECT_TRUE( bLaunched );

    sw::string line;
    bool       bFound = false;
    while ( proc.readOutputLine( line ) )
    {
        if ( line.find( "SW_PROCESS_TEST_OUTPUT" ) != sw::string::npos )
            bFound = true;
    }

    const int32 exitCode = proc.waitForExit();
    SW_EXPECT_TRUE( bFound );
    SW_EXPECT_EQUAL( 0, exitCode );
}

/**
 * @brief [ProcessTest] 정적 execute 헬퍼를 통한 출력 콜백 수신
 */
SW_TEST_CASE( ProcessTest, ExecuteHelper )
{
#if defined( SW_PLATFORM_WINDOWS )
    const sw::string cmd = "cmd.exe /c echo LINE1 && echo LINE2";
#else
    const sw::string cmd = "echo LINE1 && echo LINE2";
#endif

    sw::vector<sw::string> listLines;
    const int32            exitCode = sw::Process::execute(
        cmd,
        {},
        sw::ProcessOutputDelegate::create( [&listLines]( sw::string_view line )
    {
        listLines.push_back( sw::string( line ) );
    } ) );

    SW_EXPECT_EQUAL( 0, exitCode );
    SW_EXPECT_TRUE( listLines.size() >= 2 );
}

/**
 * @brief [ProcessTest] 종료 코드 259 로 끝난 프로세스를 "실행 중" 으로 보지 않는다
 * @details `isRunning` 은 `GetExitCodeProcess` 가 준 값을 `STILL_ACTIVE` 와 비교했는데, 그 상수는 259 다.
 *          즉 259 로 끝난 프로세스는 영원히 실행 중으로 보인다. 종료했는지는 종료 코드가 아니라
 *          핸들 자체에 물어야 한다.
 */
SW_TEST_CASE( ProcessTest, ExitCodeStillActiveIsNotMistakenForRunning )
{
#if defined( SW_PLATFORM_WINDOWS )
    sw::Process proc;

    SW_ASSERT_TRUE( proc.launch( "cmd.exe /c exit 259" ) );
    SW_EXPECT_EQUAL( 259, proc.waitForExit() );
    SW_EXPECT_FALSE( proc.isRunning() );
#else
    SW_TEST_SKIP( "STILL_ACTIVE(259) 충돌은 Windows 종료 코드 규약의 문제다" );
#endif
}

/**
 * @brief [ProcessTest] 실행 중인 프로세스 강제 종료
 * @details 예전에는 이 테스트가 POSIX 에서도 돌았고 **통과했다**. 종료해서가 아니라 `pclose` 가
 *          `sleep 10` 이 스스로 끝날 때까지 10초를 기다려 줬기 때문이다 — 이름과 달리 종료를 본 적이
 *          없다. POSIX 구현이 pid 를 들게 되면(백로그) 건너뛰기를 걷어낸다.
 */
SW_TEST_CASE( ProcessTest, TerminateProcess )
{
#if defined( SW_PLATFORM_WINDOWS )
    sw::Process proc;

    SW_ASSERT_TRUE( proc.launch( "ping.exe 127.0.0.1 -n 10" ) );
    SW_EXPECT_TRUE( proc.isRunning() );

    SW_EXPECT_TRUE( proc.terminate( 99 ) );

    // `TerminateProcess` 는 **요청**이라 돌아온 직후에는 아직 죽지 않았을 수 있다. 끝났는지는 핸들이
    // 신호 상태가 되어야 알 수 있으므로 여기서 기다린다. 종료 코드 99 는 우리가 죽였다는 증거다 —
    // `ping` 이 스스로 끝났다면 0 이다.
    SW_EXPECT_EQUAL( 99, proc.waitForExit() );
    SW_EXPECT_FALSE( proc.isRunning() );
#else
    SW_TEST_SKIP( "POSIX 구현은 popen 기반이라 자식 pid 가 없어 강제 종료를 지원하지 않는다" );
#endif
}
