#include "pch.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

#if !defined( SW_PLATFORM_WINDOWS )
    #include <csignal>
    #include <unistd.h>
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

/**
 * @brief [ProcessTest] 자식은 자기 출력 파이프 말고는 이 프로세스의 핸들 · 서술자를 물려받지 않는다
 * @details 다른 스레드가 거의 동시에 띄운 자식의 파이프(여기서는 테스트가 만든 상속 가능한 파이프로 흉내 낸다)를 새 자식이 물려받으면,
 *          그 파이프의 읽기는 **새 자식이 끝날 때까지** EOF 를 못 받는다. 예전에는 Windows 가 `bInheritHandles = TRUE` 만 줘서 상속
 *          가능한 핸들이 전부 넘어갔고, POSIX 는 CLOEXEC 없는 서술자가 exec 를 넘어갔다. 그래서 여기서는 오래 사는 자식을 띄운 뒤
 *          흉내 낸 파이프의 쓰기 끝을 닫고 읽는다 — 바로 EOF 여야 한다(물려받았다면 자식이 끝나는 3 초 뒤에야 온다).
 */
SW_TEST_CASE( ProcessTest, ChildInheritsOnlyItsOwnPipe )
{
#if defined( SW_PLATFORM_WINDOWS )
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength        = sizeof( SECURITY_ATTRIBUTES );
    inheritable.bInheritHandle = TRUE;
    HANDLE hForeignRead        = nullptr;
    HANDLE hForeignWrite       = nullptr;
    SW_ASSERT_TRUE( CreatePipe( &hForeignRead, &hForeignWrite, &inheritable, 0 ) != FALSE );
    const sw::string longCommand = "ping.exe 127.0.0.1 -n 4";
#else
    int32 arrForeignFd[2] = { -1, -1 };
    SW_ASSERT_TRUE( pipe( arrForeignFd ) == 0 );
    const sw::string longCommand = "sleep 3";
#endif

    sw::Process child;
    SW_ASSERT_TRUE( child.launch( longCommand ) );

    // 흉내 낸 "남의 파이프" 의 쓰기 끝을 닫고 읽는다. 아무도 물려받지 않았다면 바로 끝(EOF)이다.
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
#if defined( SW_PLATFORM_WINDOWS )
    CloseHandle( hForeignWrite );
    utf8       byte      = 0;
    DWORD      readCount = 0;
    const BOOL bRead     = ReadFile( hForeignRead, &byte, 1, &readCount, nullptr );
    CloseHandle( hForeignRead );
    SW_EXPECT_TRUE( bRead == FALSE || readCount == 0 );
#else
    close( arrForeignFd[1] );
    utf8          byte      = 0;
    const ssize_t readCount = read( arrForeignFd[0], &byte, 1 );
    close( arrForeignFd[0] );
    SW_EXPECT_EQUAL( static_cast<ssize_t>( 0 ), readCount );
#endif
    const int64 elapsedMilli = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start ).count();

    child.terminate( 1 );
    child.waitForExit();

    SW_EXPECT_TRUE_MSG( elapsedMilli < 1500, "새 자식이 남의 파이프를 물려받았다 — 그 파이프의 읽기가 자식이 끝날 때까지 막혔다" );
}

/**
 * @brief [ProcessTest] 분리 실행은 기다리지 않고 돌아오고, 명령은 실제로 돈다
 * @details "탐색기에서 보기" 같은 자리 — 사용자가 닫을 때까지 사는 프로그램을 띄운다. 예전에는 `execute` 라 부른 스레드(에디터 UI)가
 *          그 프로그램이 출력 파이프를 놓을 때까지 멈췄다. 여기서는 1 초쯤 걸리는 명령을 띄워 **곧바로** 돌아오는지, 그리고 명령이
 *          끝에 남기는 표식으로 실제로 돌았는지를 본다.
 */
SW_TEST_CASE( ProcessTest, DetachedLaunchReturnsWithoutWaiting )
{
    const sw::string markerPath = test::makeTempPath( "detached_marker.txt" );
#if defined( SW_PLATFORM_WINDOWS )
    sw::string windowsMarkerPath = markerPath;
    for ( utf8& ch : windowsMarkerPath )
    {
        if ( ch == '/' )
            ch = '\\';
    }
    const sw::string command = "cmd.exe /c ping.exe 127.0.0.1 -n 2 > nul & echo done> \"" + windowsMarkerPath + "\"";
#else
    const sw::string command = "sleep 1; echo done > '" + markerPath + "'";
#endif

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    SW_ASSERT_TRUE( sw::Process::launchDetached( command ) );
    const int64 launchMilli = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start ).count();
    SW_EXPECT_TRUE_MSG( launchMilli < 700, "분리 실행이 명령이 끝나기를 기다렸다" );

    // 명령이 끝에 남기는 표식 — 다 쓸 때까지 기다린다(케이스 폴더는 케이스가 끝나면 지워지므로 쓰는 중이면 안 된다).
    bool bFinished = false;
    for ( uint32 attempt = 0; attempt < 200 && bFinished == false; ++attempt )
    {
        sw::string text;
        bFinished = sw::FileUtil::readTextFile( markerPath, text ) && text.find( "done" ) != sw::string::npos;
        if ( bFinished == false )
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }
    SW_EXPECT_TRUE_MSG( bFinished, "분리 실행한 명령이 10 초 안에 표식을 남기지 않았다 — 띄우지 못했거나 돌지 않았다" );
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
}
