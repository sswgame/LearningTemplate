// Process — 띄우기 · 줄 읽기 · execute · 종료 코드 259 · 강제 종료 · isRunning 이 종료 코드를 먹지 않음 · 핸들 상속 제한 · 분리 실행 · 표준 입력 파이프,
// 셸 없는 인자 전달(Windows 인용 왕복 · POSIX 글자 그대로) · 없는 실행 파일 · 자식에만 닿는 환경 덮어쓰기.
#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/Process.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"
#include "Core/Time/MonotonicClock.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"

    #include <shellapi.h>
#elif defined( SW_PLATFORM_POSIX )
    #include <csignal>
    #include <unistd.h>
#else
    #error "Unsupported platform"
#endif

/**
 * @brief 프로세스 생성 및 표준 출력 라인 읽기
 */
SW_TEST_CASE( ProcessTest, LaunchAndReadOutput )
{
    sw::Process proc;
#if defined( SW_PLATFORM_WINDOWS )
    const sw::ProcessCommand cmd = sw::ProcessCommand::fromSearchPath( "cmd.exe", { "/c", "echo", "SW_PROCESS_TEST_OUTPUT" } );
#elif defined( SW_PLATFORM_POSIX )
    const sw::ProcessCommand cmd = sw::ProcessCommand::fromSearchPath( "echo", { "SW_PROCESS_TEST_OUTPUT" } );
#else
    #error "Unsupported platform"
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
 * @brief 정적 execute 헬퍼를 통한 출력 콜백 수신
 */
SW_TEST_CASE( ProcessTest, ExecuteHelper )
{
    // 셸 문법(`&&`)이 필요하면 셸을 실행 파일로 적고 셸 줄을 인자 하나로 준다 — `Process` 는 셸을 거치지 않는다.
#if defined( SW_PLATFORM_WINDOWS )
    const sw::ProcessCommand cmd = sw::ProcessCommand::fromSearchPath( "cmd.exe", { "/c", "echo LINE1 && echo LINE2" } );
#elif defined( SW_PLATFORM_POSIX )
    const sw::ProcessCommand cmd = sw::ProcessCommand::fromSearchPath( "sh", { "-c", "echo LINE1 && echo LINE2" } );
#else
    #error "Unsupported platform"
#endif

    sw::vector<sw::string>    listLines;
    const sw::expected<int32> exitCode = sw::Process::execute(
        cmd,
        {},
        sw::ProcessOutputDelegate::create( [&listLines]( sw::string_view line )
    {
        listLines.push_back( sw::string( line ) );
    } ) );

    SW_REQUIRE_TRUE( exitCode.has_value() );
    SW_EXPECT_EQUAL( 0, *exitCode );
    SW_EXPECT_TRUE( listLines.size() >= 2 );
}

/**
 * @brief 종료 코드 259 로 끝난 프로세스를 "실행 중" 으로 보지 않는다
 * @details `isRunning` 은 `GetExitCodeProcess` 가 준 값을 `STILL_ACTIVE` 와 비교했는데, 그 상수는 259 다.
 *          즉 259 로 끝난 프로세스는 영원히 실행 중으로 보인다. 종료했는지는 종료 코드가 아니라
 *          핸들 자체에 물어야 한다.
 */
#if defined( SW_PLATFORM_WINDOWS ) // STILL_ACTIVE(259) 충돌은 Windows 종료 코드 규약의 문제다
SW_TEST_CASE( ProcessTest, ExitCodeStillActiveIsNotMistakenForRunning )
{
    sw::Process proc;

    SW_REQUIRE_TRUE( proc.launch( sw::ProcessCommand::fromSearchPath( "cmd.exe", { "/c", "exit", "259" } ) ) );
    SW_EXPECT_EQUAL( 259, proc.waitForExit() );
    SW_EXPECT_FALSE( proc.isRunning() );
}
#endif

/**
 * @brief 실행 중인 프로세스 강제 종료 — **양쪽 플랫폼에서**
 * @details 주의: 종료 대신 자식이 스스로 끝날 때까지 기다리는 구현(`pclose` 가 `sleep 10` 을 10초 기다려 주는 식)도
 *          통과할 수 있다 — 그래서 종료 코드가 0 이 아닌지를 본다.
 * @note 종료 코드가 갈리는 유일한 자리다. Windows 는 우리가 준 값(99)을 자식에게 물리고, POSIX 는
 *       신호로 죽이므로 코드를 정해 줄 수 없어 셸 규약대로 `128 + SIGKILL` 이 된다. **어느 쪽이든
 *       0 이 아닌 것이 핵심이다** — 0 이면 자식이 스스로 끝난 것이고, 그러면 이 케이스는 종료를
 *       검증하지 못한 것이다.
 */
SW_TEST_CASE( ProcessTest, TerminateProcess )
{
    sw::Process proc;

#if defined( SW_PLATFORM_WINDOWS )
    const sw::ProcessCommand cmd              = sw::ProcessCommand::fromSearchPath( "ping.exe", { "127.0.0.1", "-n", "10" } );
    const int32              expectedExitCode = 99;
#elif defined( SW_PLATFORM_POSIX )
    const sw::ProcessCommand cmd              = sw::ProcessCommand::fromSearchPath( "sleep", { "10" } );
    const int32              expectedExitCode = 128 + SIGKILL;
#else
    #error "Unsupported platform"
#endif

    SW_REQUIRE_TRUE( proc.launch( cmd ) );
    SW_EXPECT_TRUE( proc.isRunning() );

    SW_EXPECT_TRUE( proc.terminate( 99 ) );

    // 강제 종료는 **요청**이라 돌아온 직후에는 아직 죽지 않았을 수 있다. 끝났는지는 기다려야 알 수
    // 있으므로 여기서 기다린다. 종료 코드가 0 이 아닌 것이 우리가 죽였다는 증거다 — 자식이 스스로
    // 끝났다면 0 이다(그리고 10초를 기다렸다는 뜻이기도 하다).
    SW_EXPECT_EQUAL( expectedExitCode, proc.waitForExit() );
    SW_EXPECT_FALSE( proc.isRunning() );
}

/**
 * @brief 강제 종료는 자식이 띄운 손자까지 끝내 출력 파이프가 닫힌다
 * @details 루트만 죽이면 손자(`git` 이 띄운 `git-lfs`, 여기서는 셸이 띄운 대기)가 파이프의 쓰는 끝을 쥐고 있어 읽는 스레드가 손자가 끝날 때까지 막힌다
 *          (에디터 소스 컨트롤 잡의 종료가 1 초 넘게 걸렸다). Windows 는 Job 객체째, POSIX 는 프로세스 그룹째 죽인다.
 */
SW_TEST_CASE( ProcessTest, TerminateEndsGrandchildren )
{
    sw::Process proc;
#if defined( SW_PLATFORM_WINDOWS )
    const sw::ProcessCommand cmd = sw::ProcessCommand::fromSearchPath( "cmd.exe", { "/c", "ping", "127.0.0.1", "-n", "20" } );
#elif defined( SW_PLATFORM_POSIX )
    const sw::ProcessCommand cmd = sw::ProcessCommand::fromSearchPath( "sh", { "-c", "sleep 20; :" } ); // `; :` — 셸이 sleep 으로 바뀌지(exec) 않고 손자로 띄운다
#else
    #error "Unsupported platform"
#endif
    SW_REQUIRE_TRUE( proc.launch( cmd ) );
    const sw::Stopwatch watch;
    SW_EXPECT_TRUE( proc.terminate( 99 ) );
    sw::string line;
    while ( proc.readOutputLine( line ) )
    {
    }
    (void)proc.waitForExit();
    SW_EXPECT_TRUE( watch.getElapsedMicroseconds() < 5'000'000 ); // 손자가 남으면 20 초 가까이 막힌다
}

/**
 * @brief 자식이 스스로 끝나면 묻는 쪽이 **바로** 안다
 * @details `isRunning` 이 자기 깃발만 보면 자식이 끝나도 `waitForExit` 을 부르기 전까지 true 라, 취소 UI 가
 *          끝난 빌드를 "돌고 있다" 고 보여 준다. OS 에 물어 곧 false 가 되어야 하고, 그러면서도
 *          **종료 코드는 그대로 남아 있어야 한다**(POSIX 에서
 *          묻는 김에 자식을 거둬 버리면 뒤이은 `waitForExit` 이 -1 을 준다 — 실제로 쉬운 함정이다).
 */
SW_TEST_CASE( ProcessTest, IsRunningTurnsFalseWithoutEatingTheExitCode )
{
    sw::Process proc;

#if defined( SW_PLATFORM_WINDOWS )
    const sw::ProcessCommand cmd = sw::ProcessCommand::fromSearchPath( "cmd.exe", { "/c", "exit", "7" } );
#elif defined( SW_PLATFORM_POSIX )
    const sw::ProcessCommand cmd = sw::ProcessCommand::fromSearchPath( "sh", { "-c", "exit 7" } );
#else
    #error "Unsupported platform"
#endif

    SW_REQUIRE_TRUE( proc.launch( cmd ) );

    // 끝날 때까지 묻는다 — 자식이 끝났는데도 영원히 true 면 여기서 걸린다.
    const bool bSawStopped = sw::test::waitUntil( [&proc]()
    { return proc.isRunning() == false; } );
    SW_EXPECT_TRUE_MSG( bSawStopped, "자식이 끝났는데도 isRunning 이 계속 true 다" );

    // 묻는 것이 자식을 거두지는 않았어야 한다.
    SW_EXPECT_EQUAL( 7, proc.waitForExit() );
}

/**
 * @brief 자식은 자기 출력 파이프 말고는 이 프로세스의 핸들 · 서술자를 물려받지 않는다
 * @details 다른 스레드가 거의 동시에 띄운 자식의 파이프(여기서는 테스트가 만든 상속 가능한 파이프로 흉내 낸다)를 새 자식이 물려받으면,
 *          그 파이프의 읽기는 **새 자식이 끝날 때까지** EOF 를 못 받는다. Windows 에서 `bInheritHandles = TRUE` 만 주면 상속
 *          가능한 핸들이 전부 넘어가고, POSIX 에서는 CLOEXEC 없는 서술자가 exec 를 넘어간다. 그래서 여기서는 오래 사는 자식을 띄운 뒤
 *          흉내 낸 파이프의 쓰기 끝을 닫고 읽는다 — 바로 EOF 여야 한다(물려받았다면 자식이 끝나야 온다 — 판정은 읽기가 돌아온 순간 자식이 살아 있는가).
 */
SW_TEST_CASE( ProcessTest, ChildInheritsOnlyItsOwnPipe )
{
#if defined( SW_PLATFORM_WINDOWS )
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength        = sizeof( SECURITY_ATTRIBUTES );
    inheritable.bInheritHandle = TRUE;
    HANDLE hForeignRead        = nullptr;
    HANDLE hForeignWrite       = nullptr;
    SW_REQUIRE_TRUE( CreatePipe( &hForeignRead, &hForeignWrite, &inheritable, 0 ) != FALSE );
    const sw::ProcessCommand longCommand = sw::ProcessCommand::fromSearchPath( "ping.exe", { "127.0.0.1", "-n", "30" } ); // 테스트가 끝낼 때 죽인다 — 바쁜 기계에서도 읽기 전에 끝나지 않게 길게
#elif defined( SW_PLATFORM_POSIX )
    int32 arrForeignFd[2] = { -1, -1 };
    SW_REQUIRE_TRUE( pipe( arrForeignFd ) == 0 );
    const sw::ProcessCommand longCommand = sw::ProcessCommand::fromSearchPath( "sleep", { "30" } );
#else
    #error "Unsupported platform"
#endif

    sw::Process child;
    SW_REQUIRE_TRUE( child.launch( longCommand ) );

    // 흉내 낸 "남의 파이프" 의 쓰기 끝을 닫고 읽는다. 아무도 물려받지 않았다면 바로 끝(EOF)이다.
    // 판정은 시간이 아니라 사건이다: 물려받았다면 읽기는 자식이 끝나야 돌아오므로, 읽기가 돌아온 순간 자식은 이미 끝나 있다.
#if defined( SW_PLATFORM_WINDOWS )
    CloseHandle( hForeignWrite );
    utf8       byte      = 0;
    DWORD      readCount = 0;
    const BOOL bRead     = ReadFile( hForeignRead, &byte, 1, &readCount, nullptr );
    CloseHandle( hForeignRead );
    SW_EXPECT_TRUE( bRead == FALSE || readCount == 0 );
#elif defined( SW_PLATFORM_POSIX )
    close( arrForeignFd[1] );
    utf8          byte      = 0;
    const ssize_t readCount = read( arrForeignFd[0], &byte, 1 );
    close( arrForeignFd[0] );
    SW_EXPECT_EQUAL( static_cast<ssize_t>( 0 ), readCount );
#else
    #error "Unsupported platform"
#endif
    const bool bChildStillRunning = child.isRunning();

    child.terminate( 1 );
    child.waitForExit();

    SW_EXPECT_TRUE_MSG( bChildStillRunning, "새 자식이 남의 파이프를 물려받았다 — 그 파이프의 읽기가 자식이 끝날 때까지 막혔다" );
}

/**
 * @brief 분리 실행은 기다리지 않고 돌아오고, 명령은 실제로 돈다
 * @details "탐색기에서 보기" 같은 자리 — 사용자가 닫을 때까지 사는 프로그램을 띄운다. `execute` 로 띄우면 부른 스레드(에디터 UI)가
 *          그 프로그램이 출력 파이프를 놓을 때까지 멈춘다. 판정은 시간이 아니라 사건이다: 자식은 "놓아 주기" 파일을 기다렸다가 표식에 done 을
 *          쓰고, 그 파일은 `launchDetached` 가 **돌아온 뒤에** 이 케이스가 만든다. 분리 실행이 자식을 기다렸다면 놓아 주기가 영영 오지 않아
 *          자식은 시한 뒤 timeout 을 쓴다. 예전 판정(돌아오기까지 700 ms 미만)은 기계가 바쁘면 2 초가 걸려 졌다(묶음 검증 2026-11-10).
 */
SW_TEST_CASE( ProcessTest, DetachedLaunchReturnsWithoutWaiting )
{
    const sw::string markerPath  = sw::test::makeScratchPath( "detached_marker.txt" );
    const sw::string releasePath = sw::test::makeScratchPath( "detached_release.txt" );
    // 셸 줄에 경로를 넣지 않는다 — 작업 폴더를 표식의 폴더로 두고 파일 이름만 쓴다(셸 인용을 이 테스트가 떠안지 않게).
    sw::ProcessOptions options;
    options._workingDirectory = sw::FileUtil::getDirectoryPart( markerPath );
    // 놓아 주기를 1 초 간격으로 10 번 본다(케이스 시한 30 초 안) — 그 안에 오지 않으면 timeout(분리 실행이 기다렸다는 뜻).
#if defined( SW_PLATFORM_WINDOWS )
    const sw::ProcessCommand command = sw::ProcessCommand::fromSearchPath(
        "cmd.exe", { "/c", "(for /l %i in (1,1,10) do @(if exist detached_release.txt (echo done> detached_marker.txt& exit 0) else ping.exe 127.0.0.1 -n 2 > nul)) & echo timeout> detached_marker.txt" } );
#elif defined( SW_PLATFORM_POSIX )
    const sw::ProcessCommand command = sw::ProcessCommand::fromSearchPath(
        "sh", { "-c", "i=0; while [ $i -lt 10 ]; do if [ -f detached_release.txt ]; then echo done > detached_marker.txt; exit 0; fi; sleep 1; i=$((i+1)); done; echo timeout > detached_marker.txt" } );
#else
    #error "Unsupported platform"
#endif

    SW_REQUIRE_TRUE( sw::Process::launchDetached( command, options ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( releasePath, "go" ) ); // 돌아온 뒤에만 닿는 줄 — 기다렸다면 자식은 이미 timeout 을 썼다

    // 명령이 끝에 남기는 표식 — 다 쓸 때까지 기다린다(케이스 폴더는 케이스가 끝나면 지워지므로 쓰는 중이면 안 된다).
    sw::string text;
    const bool bFinished = sw::test::waitUntil( [&markerPath, &text]()
    {
        // 표식이 생기기 전에는 읽지 않는다 — 없는 파일을 읽으면 읽는 쪽이 Error 를 남기고, 기다리는 동안 그 줄이 쌓인다.
        return sw::FileUtil::exists( markerPath ) && sw::FileUtil::readTextFile( markerPath, text ) && text.find( '\n' ) != sw::string::npos;
    }, 20.0 );
    SW_EXPECT_TRUE_MSG( bFinished, "분리 실행한 명령이 표식을 남기지 않았다 — 띄우지 못했거나 돌지 않았다" );
    SW_EXPECT_TRUE_MSG( text.find( "done" ) != sw::string::npos, "분리 실행이 명령이 끝나기를 기다렸다 — 자식이 놓아 주기를 받지 못하고 timeout 을 썼다" );
    // shortcut: 분리 실행은 기다릴 핸들이 없어 셸이 작업 폴더(케이스 폴더)를 놓기를 사건으로 볼 수 없다 — 표식 뒤의 짧은 유예로 둔다. 분리 실행이 pid 를 돌려주게 되면 그 종료를 기다린다.
    sw::ThreadUtil::sleepForMilliseconds( 200 );
}

/**
 * @brief 표준 입력 파이프로 쓴 줄을 자식이 읽고, 입력을 닫으면 자식이 EOF 로 끝난다
 */
SW_TEST_CASE( ProcessTest, StandardInputPipeReachesTheChild )
{
    sw::ProcessOptions options;
    options._bPipeStandardInput = true;
    sw::Process process;
#if defined( SW_PLATFORM_WINDOWS )
    SW_REQUIRE_TRUE( process.launch( sw::ProcessCommand::fromSearchPath( "findstr.exe", { "x" } ), options ) );
#elif defined( SW_PLATFORM_POSIX )
    SW_REQUIRE_TRUE( process.launch( sw::ProcessCommand::fromSearchPath( "grep", { "x" } ), options ) );
#else
    #error "Unsupported platform"
#endif
    SW_REQUIRE_TRUE( process.writeInput( "axb\nccc\n" ) );
    process.closeInput();
    sw::string line;
    sw::string output;
    while ( process.readOutputLine( line ) )
    {
        output += line;
        output += '\n';
    }
    SW_EXPECT_EQUAL( 0, process.waitForExit() );
    SW_EXPECT_EQUAL( sw::string( "axb\n" ), output );
}

namespace
{
    /** @brief 인용이 틀리기 쉬운 인자들 — 공백 · 따옴표 · 역슬래시(따옴표 앞 · 끝) · 빈 인자 · 비ASCII(한글) · 셸 글자. */
    sw::vector<sw::string> makeTrickyProcessArguments()
    {
        return { "plain",
                 "with space",
                 "quote\"inside",
                 "back\\slash",
                 "trailing\\",
                 "space then slash quote\\\"x",
                 "\\\\server\\share\\",
                 "",
                 "\xED\x95\x9C\xEA\xB8\x80 arg",
                 "$HOME;&|<>^`x`" };
    }
} // namespace

/**
 * @brief Windows 명령줄은 CommandLineToArgvW 로 되읽으면 넣은 인자와 같다
 * @details `CreateProcessW` 에 주는 글이 `toCommandLine` 이고, 자식(MSVC C 런타임)은 그것을 CommandLineToArgvW 와 같은 규칙으로 나눈다.
 *          예전에는 부르는 쪽이 따옴표만 붙여 따옴표 · 끝 역슬래시가 든 경로가 인자를 깨거나 늘렸다.
 */
#if defined( SW_PLATFORM_WINDOWS ) // POSIX 는 인자 배열을 execv 에 그대로 넘긴다 — 아래 케이스가 실제 자식으로 본다
SW_TEST_CASE( ProcessTest, WindowsCommandLineRoundTripsThroughTheArgumentSplitter )
{
    const sw::optional<sw::AbsolutePath> toolPath = sw::AbsolutePath::tryMake( "C:\\Program Files\\Tool\\tool.exe" );
    SW_REQUIRE_TRUE( toolPath.has_value() );
    const sw::ProcessCommand command       = sw::ProcessCommand::at( *toolPath, makeTrickyProcessArguments() );
    const sw::wstring        wideLine      = sw::StringUtil::utf8ToUTF16( command.toCommandLine().c_str() );
    int32                    argumentCount = 0;
    LPWSTR*                  ppArgument    = CommandLineToArgvW( wideLine.c_str(), &argumentCount );
    SW_REQUIRE_TRUE( ppArgument != nullptr );
    sw::vector<sw::string> listParsed;
    for ( int32 index = 0; index < argumentCount; ++index )
    {
        listParsed.push_back( sw::StringUtil::utf16ToUTF8( ppArgument[index] ) );
    }
    LocalFree( ppArgument );

    const sw::string commandLine = command.toCommandLine();
    SW_REQUIRE_TRUE_MSG( command._listArgument.size() + 1 == listParsed.size(), commandLine );
    SW_EXPECT_EQUAL( command._executable, listParsed[0] );
    for ( size_t index = 0; index < command._listArgument.size(); ++index )
    {
        SW_EXPECT_TRUE_MSG( command._listArgument[index] == listParsed[index + 1], commandLine );
    }
}
#endif

/**
 * @brief POSIX 자식은 셸 없이 인자를 한 글자도 바뀌지 않고 받는다 — `$` `;` `&` 도 글자 그대로다
 * @details `printf '%s\n' <인자…>` 를 띄워 한 줄에 인자 하나씩 되읽는다. 예전에는 `sh -c` 가 명령 글을 다시 해석했다.
 */
#if defined( SW_PLATFORM_POSIX ) // Windows 는 위 케이스가 CommandLineToArgvW 로 본다
SW_TEST_CASE( ProcessTest, PosixChildReceivesArgumentsUnchanged )
{
    const sw::vector<sw::string> listArgument = makeTrickyProcessArguments();
    sw::ProcessCommand           command      = sw::ProcessCommand::fromSearchPath( "printf", { "%s\\n" } );
    command._listArgument.insert( command._listArgument.end(), listArgument.begin(), listArgument.end() );

    sw::vector<sw::string>    listLine;
    const sw::expected<int32> exitCode = sw::Process::execute( command, {}, sw::ProcessOutputDelegate::create( [&listLine]( sw::string_view line )
    {
        listLine.push_back( sw::string( line ) );
    } ) );
    SW_REQUIRE_TRUE( exitCode.has_value() );
    SW_EXPECT_EQUAL( 0, *exitCode );
    SW_REQUIRE_EQUAL( listArgument.size(), listLine.size() );
    for ( size_t index = 0; index < listArgument.size(); ++index )
    {
        SW_EXPECT_EQUAL( listArgument[index], listLine[index] );
    }
}
#endif

/**
 * @brief 없는 실행 파일 · 빈 실행 파일은 띄우기에서 진다(POSIX 도 셸의 127 로 넘어가지 않는다)
 */
SW_TEST_CASE( ProcessTest, MissingExecutableFailsToLaunch )
{
    SW_EXPECT_LOG( "Process.LaunchFailed" ); // 검색 경로에 없는 이름
    SW_EXPECT_LOG( "Process.NoExecutable" ); // 빈 명령(띄우기 · 분리 실행)
    sw::Process process;
    SW_EXPECT_FALSE( process.launch( sw::ProcessCommand::fromSearchPath( "sw_no_such_program_7f3a", { "x" } ) ) );
    SW_EXPECT_FALSE( process.launch( sw::ProcessCommand{} ) );
    SW_EXPECT_FALSE( sw::Process::launchDetached( sw::ProcessCommand{} ) );
    // 동기 실행도 띄우기 실패는 종료 코드(-1)가 아니라 오류다 — -1 로 끝난 프로세스와 갈린다.
    const sw::expected<int32> missing = sw::Process::execute( sw::ProcessCommand::fromSearchPath( "sw_no_such_program_7f3a", { "x" } ) );
    SW_REQUIRE_FALSE( missing.has_value() );
    SW_EXPECT_TRUE( missing.error().detail().find( "sw_no_such_program_7f3a" ) != sw::string_view::npos );
}

/**
 * @brief `ProcessOptions::_listEnvironment` 는 자식의 환경에만 닿고 부모의 환경은 그대로다. 잘못된 이름과 분리 실행은 조용히 넘기지 않고 진다
 * @details 변이: 덮어쓰기를 무시하면 첫 단언이 지고, 부모 환경을 바꾸면 둘째 단언이 진다.
 */
SW_TEST_CASE( ProcessTest, EnvironmentOverrideReachesOnlyTheChild )
{
    constexpr const utf8* kName = "SW_PROCESS_TEST_ENV_OVERRIDE";
    sw::ProcessOptions    options;
    options._listEnvironment.push_back( sw::ProcessEnvironmentEntry{ kName, "from-option" } );
#if defined( SW_PLATFORM_WINDOWS )
    const sw::ProcessCommand command = sw::ProcessCommand::fromSearchPath( "cmd.exe", { "/c", "echo", "%SW_PROCESS_TEST_ENV_OVERRIDE%" } );
#elif defined( SW_PLATFORM_POSIX )
    const sw::ProcessCommand command = sw::ProcessCommand::fromSearchPath( "printenv", { kName } );
#else
    #error "Unsupported platform"
#endif
    sw::Process process;
    SW_REQUIRE_TRUE( process.launch( command, options ) );
    sw::string line;
    bool       bFound = false;
    while ( process.readOutputLine( line ) )
    {
        if ( line.find( "from-option" ) != sw::string::npos )
            bFound = true;
    }
    SW_EXPECT_EQUAL( 0, process.waitForExit() );
    SW_EXPECT_TRUE( bFound );
    SW_EXPECT_FALSE( sw::ProcessEnvironment::isSet( kName ) );

    SW_EXPECT_LOG( "Process.InvalidEnvironmentName" );
    SW_EXPECT_LOG( "Process.DetachedEnvironment" );
    sw::ProcessOptions badOptions;
    badOptions._listEnvironment.push_back( sw::ProcessEnvironmentEntry{ "A=B", "x" } );
    sw::Process badProcess;
    SW_EXPECT_FALSE( badProcess.launch( command, badOptions ) );
    SW_EXPECT_FALSE( sw::Process::launchDetached( command, options ) );
}
