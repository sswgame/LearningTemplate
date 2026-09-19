#include "pch.h"

#include "Core/Process/Process.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

#if !defined( SW_PLATFORM_WINDOWS )
    #include <csignal>
#endif

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
 * @brief [ProcessTest] 실행 중인 프로세스 강제 종료 — **양쪽 플랫폼에서**
 * @details 한때 이 케이스는 POSIX 에서도 돌았고 **통과했다**. 종료해서가 아니라 `pclose` 가
 *          `sleep 10` 이 스스로 끝날 때까지 10초를 기다려 줬기 때문이다 — 이름과 달리 종료를 본 적이
 *          없었다. 그 뒤로는 건너뛰었고, POSIX 가 fork/exec 으로 pid 를 들게 된 지금 걷어냈다.
 * @note 종료 코드가 갈리는 유일한 자리다. Windows 는 우리가 준 값(99)을 자식에게 물리고, POSIX 는
 *       신호로 죽이므로 코드를 정해 줄 수 없어 셸 규약대로 `128 + SIGKILL` 이 된다. **어느 쪽이든
 *       0 이 아닌 것이 핵심이다** — 0 이면 자식이 스스로 끝난 것이고, 그러면 이 케이스는 종료를
 *       검증하지 못한 것이다.
 */
SW_TEST_CASE( ProcessTest, TerminateProcess )
{
    sw::Process proc;

#if defined( SW_PLATFORM_WINDOWS )
    const sw::string cmd              = "ping.exe 127.0.0.1 -n 10";
    const int32      expectedExitCode = 99;
#else
    const sw::string cmd              = "sleep 10";
    const int32      expectedExitCode = 128 + SIGKILL;
#endif

    SW_ASSERT_TRUE( proc.launch( cmd ) );
    SW_EXPECT_TRUE( proc.isRunning() );

    SW_EXPECT_TRUE( proc.terminate( 99 ) );

    // 강제 종료는 **요청**이라 돌아온 직후에는 아직 죽지 않았을 수 있다. 끝났는지는 기다려야 알 수
    // 있으므로 여기서 기다린다. 종료 코드가 0 이 아닌 것이 우리가 죽였다는 증거다 — 자식이 스스로
    // 끝났다면 0 이다(그리고 10초를 기다렸다는 뜻이기도 하다).
    SW_EXPECT_EQUAL( expectedExitCode, proc.waitForExit() );
    SW_EXPECT_FALSE( proc.isRunning() );
}

/**
 * @brief [ProcessTest] 자식이 스스로 끝나면 묻는 쪽이 **바로** 안다
 * @details `isRunning` 이 자기 깃발만 보던 시절에는 자식이 끝나도 `waitForExit` 을 부르기 전까지
 *          true 였다 — 취소 UI 가 끝난 빌드를 "돌고 있다" 고 보여 주던 자리다. OS 에 묻게 된 지금은
 *          곧 false 가 되어야 하고, 그러면서도 **종료 코드는 그대로 남아 있어야 한다**(POSIX 에서
 *          묻는 김에 자식을 거둬 버리면 뒤이은 `waitForExit` 이 -1 을 준다 — 실제로 쉬운 함정이다).
 */
SW_TEST_CASE( ProcessTest, IsRunningTurnsFalseWithoutEatingTheExitCode )
{
    sw::Process proc;

#if defined( SW_PLATFORM_WINDOWS )
    const sw::string cmd = "cmd.exe /c exit 7";
#else
    const sw::string cmd = "exit 7";
#endif

    SW_ASSERT_TRUE( proc.launch( cmd ) );

    // 끝날 때까지 묻는다 — 자식이 끝났는데도 영원히 true 면 여기서 걸린다.
    bool bSawStopped = false;
    for ( int32 attemptIndex = 0; attemptIndex < 200; ++attemptIndex )
    {
        if ( proc.isRunning() == false )
        {
            bSawStopped = true;
            break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    SW_EXPECT_TRUE_MSG( bSawStopped, "자식이 끝났는데도 isRunning 이 계속 true 다" );

    // 묻는 것이 자식을 거두지는 않았어야 한다.
    SW_EXPECT_EQUAL( 7, proc.waitForExit() );
}
