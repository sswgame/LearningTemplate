#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#if defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <cerrno>
    #include <csignal>

namespace sw
{
    SW_LOG_CALLER( "PosixProcess" );

    namespace
    {
        /**
         * @brief 자식이 만든 파이프 읽기 스트림.
         * @details `_pStdOutRead` 에는 `fdopen` 한 `FILE*` 이 들어간다 — 읽기는 줄 단위라 버퍼가 있는
         *          편이 낫고, `fgets` 하나로 끝난다. 예전에는 같은 자리에 `popen` 의 스트림이 있었다.
         */
        FILE* asOutputStream( void* pHandle )
        {
            return static_cast<FILE*>( pHandle );
        }

        /** @brief `waitpid` 가 준 상태를 종료 코드로 바꿉니다. 신호로 죽었으면 셸 규약대로 128+신호. */
        int32 exitCodeFromStatus( int32 status )
        {
            if ( WIFEXITED( status ) )
                return static_cast<int32>( WEXITSTATUS( status ) );
            if ( WIFSIGNALED( status ) )
                return 128 + static_cast<int32>( WTERMSIG( status ) );
            return -1;
        }
    } // namespace

    void Process::shutdown()
    {
        if ( _pStdOutRead != nullptr )
        {
            fclose( asOutputStream( _pStdOutRead ) );
            _pStdOutRead = nullptr;
        }

        // Windows 와 같이 **분리**한다 — 아직 도는 자식을 기다리지 않는다. 이미 끝나 있으면 여기서
        // 거둬 좀비를 남기지 않고, 아직 돌고 있으면 우리가 죽은 뒤 init 이 거둔다.
        // (예전에는 `pclose` 라 소멸자가 자식이 끝날 때까지 막혔다 — 얼마나 걸릴지 자식이 정했다.)
        const pid_t childPid = static_cast<pid_t>( _processId );
        if ( childPid > 0 )
        {
            int32 status = 0;
            waitpid( childPid, &status, WNOHANG );
        }

        _bufferedOutput.clear();
        _pNativeHandle = nullptr;
        _processId     = 0;
        _bRunning      = false;
    }

    bool Process::launch( string_view command, const ProcessOptions& options )
    {
        shutdown();

        // 자식에게 넘길 문자열은 **fork 전에** 만들어 둔다 — 아래 자식 블록에서는 할당을 하지 않는다.
        const string cmd{ command };
        const utf8*  pCommand          = cmd.c_str();
        const utf8*  pWorkingDirectory = options._workingDirectory.empty() ? nullptr : options._workingDirectory.c_str();

        int32 arrPipeFd[2] = { -1, -1 };
        if ( pipe( arrPipeFd ) != 0 )
        {
            SW_LOG_ERROR( "pipe() failed for command: %#", pCommand );
            return false;
        }

        const pid_t childPid = fork();
        if ( childPid < 0 )
        {
            close( arrPipeFd[0] );
            close( arrPipeFd[1] );
            SW_LOG_ERROR( "fork() failed for command: %#", pCommand );
            return false;
        }

        if ( childPid == 0 )
        {
            // ---- 자식 ----
            // 여기서부터 exec 까지는 **async-signal-safe 한 것만** 부른다. fork 는 부모의 스레드를
            // 하나만 데려오는데 뮤텍스는 통째로 복사되므로, 다른 스레드가 쥐고 있던 락(로거의 것이
            // 대표적이다)은 영원히 잠긴 채다 — 여기서 로그를 찍거나 할당을 하면 그대로 멈춘다.
            close( arrPipeFd[0] );
            // 표준 에러를 표준 출력에 합친다 — `ProcessOutputDelegate` 가 약속한 대로(빌드 로그는
            // 두 스트림이 원래 순서대로 섞여야 읽힌다). 예전 `popen` 구현은 명령 끝에 `2>&1` 을 붙였다.
            dup2( arrPipeFd[1], STDOUT_FILENO );
            dup2( arrPipeFd[1], STDERR_FILENO );
            close( arrPipeFd[1] );

            // 자기 프로세스 그룹을 가진다 — `terminate` 가 그룹째 죽여야 `sh` 가 낳은 손자(진짜
            // 컴파일러 프로세스)까지 멈춘다. 셸만 죽이면 손자는 계속 돌면서 파이프를 붙들고 있다.
            setpgid( 0, 0 );

            if ( pWorkingDirectory != nullptr && chdir( pWorkingDirectory ) != 0 )
                _exit( 127 );

            execl( "/bin/sh", "sh", "-c", pCommand, static_cast<utf8*>( nullptr ) );
            _exit( 127 ); // exec 가 실패했을 때만 여기 온다
        }

        // ---- 부모 ----
        close( arrPipeFd[1] );

        // 자식과 부모 중 누가 먼저 도는지는 정해져 있지 않다 — 그룹 설정은 **양쪽에서** 부른다
        // (먼저 도는 쪽이 이기고, 나중 것은 조용히 실패한다). 이게 POSIX 가 권하는 관용구다.
        setpgid( childPid, childPid );

        FILE* pStream = fdopen( arrPipeFd[0], "r" );
        if ( pStream == nullptr )
        {
            close( arrPipeFd[0] );
            kill( -childPid, SIGKILL );
            int32 status = 0;
            waitpid( childPid, &status, 0 );
            SW_LOG_ERROR( "fdopen() failed for command: %#", pCommand );
            return false;
        }

        _pStdOutRead   = pStream;
        _processId     = static_cast<int32>( childPid );
        _pNativeHandle = reinterpret_cast<void*>( static_cast<uintptr_t>( childPid ) );
        _bRunning      = true;
        return true;
    }

    bool Process::readOutputLine( string& outLine )
    {
        outLine.clear();

        if ( _pStdOutRead == nullptr )
            return false;

        utf8 arrLineBuffer[constant::kMaxBuffer4096];
        if ( fgets( arrLineBuffer, sizeof( arrLineBuffer ), asOutputStream( _pStdOutRead ) ) != nullptr )
        {
            outLine = arrLineBuffer;
            while ( outLine.empty() == false && ( outLine.back() == '\n' || outLine.back() == '\r' ) )
                outLine.pop_back();
            return true;
        }

        return false;
    }

    int32 Process::waitForExit()
    {
        // pid 는 **한 번만 읽어 지역에 든다** — 아래 `terminate` 주석의 이유와 같다.
        const pid_t childPid = static_cast<pid_t>( _processId );
        if ( childPid <= 0 )
            return -1;

        int32 status = 0;
        pid_t reaped = 0;
        do
        {
            reaped = waitpid( childPid, &status, 0 );
        } while ( reaped < 0 && errno == EINTR );

        _bRunning = false;

        // **거둔 뒤에는 pid 를 놓는다.** 그 번호는 OS 가 곧 다른 프로세스에 준다 — 계속 들고 있으면
        // 소멸자의 `waitpid` 가 어느 날 남의(정확히는 이 프로세스가 나중에 낳은 다른) 자식을 거둬
        // 그쪽의 `waitForExit` 에서 종료 코드를 빼앗는다.
        _processId     = 0;
        _pNativeHandle = nullptr;

        return ( reaped < 0 ) ? -1 : exitCodeFromStatus( status );
    }

    bool Process::terminate( int32 exitCode )
    {
        // POSIX 는 자식의 종료 코드를 정할 수 없다 — 신호로 죽고, `waitForExit` 은 128+신호를 준다.
        (void)exitCode;

        // **pid 를 한 번만 읽어 지역 변수에 든다.** 이 함수는 다른 스레드가 `readOutputLine` ·
        // `waitForExit` 을 도는 중에 불린다(`ModuleCompiler::cancel` 이 UI 스레드에서 그렇게 부른다).
        // 검사한 뒤에 `waitForExit` 이 `_processId` 를 0 으로 만들면 아래 `kill( -_processId, … )` 이
        // `kill( 0, … )` 이 되는데, 그것은 **우리 자신의 프로세스 그룹에 SIGKILL 을 보내는 것**이다 —
        // 에디터가 통째로 죽는다. 지역에 담아 두면 그 창이 아예 없다.
        const pid_t childPid = static_cast<pid_t>( _processId );
        if ( childPid <= 0 )
            return false;

        // 그룹 전체에 보낸다. `sh -c "…"` 가 명령을 직접 exec 해 버리는 경우(대부분의 셸이 그렇게
        // 최적화한다)에도 그룹에는 자식 하나뿐이라 결과가 같고, 손자를 낳았으면 그것까지 죽는다.
        if ( kill( -childPid, SIGKILL ) != 0 && kill( childPid, SIGKILL ) != 0 )
        {
            SW_LOG_WARNING( "kill(SIGKILL) failed for pid %#", static_cast<int32>( childPid ) );
            return false;
        }

        // 아직 죽지 않았을 수 있다 — Windows 의 `TerminateProcess` 와 같은 **요청**이다.
        // 확실히 하려면 호출자가 `waitForExit` 으로 기다린다.
        _bRunning = false;
        return true;
    }

    bool Process::isRunning() const
    {
        const pid_t childPid = static_cast<pid_t>( _processId );
        if ( childPid <= 0 )
            return false;

        // `WNOWAIT` 이 핵심이다 — **거둬들이지 않고** 묻는다. `waitpid(WNOHANG)` 로 물으면 끝난 자식을
        // 그 자리에서 거둬 버려서, 뒤이어 부르는 `waitForExit` 이 종료 코드를 영영 못 받는다.
        siginfo_t info{};
        info.si_pid = 0;
        if ( waitid( P_PID, static_cast<id_t>( childPid ), &info, WEXITED | WNOHANG | WNOWAIT ) != 0 )
            return false; // 물을 수 없다 — 이미 거뒀거나 우리 자식이 아니다

        return info.si_pid == 0; // 0 이면 아직 끝나지 않았다
    }
} // namespace sw

#endif
