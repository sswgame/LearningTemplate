#include "pch.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Process/Process.h"
#include "Core/Process/ProcessEnvironment.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Foundation/PlatformOSHeaders.h"

    #include <cerrno>
    #include <csignal>
    #include <fcntl.h>
    #include <sys/syscall.h>
    #include <unistd.h>

namespace sw
{
    SW_LOG_CALLER( "PosixProcess" );

    namespace
    {
        /**
         * @brief 자식 프로세스의 출력 파이프를 읽는 스트림입니다.
         * @details `_pStdOutRead` 에는 `fdopen` 한 `FILE*` 이 들어갑니다. 줄 단위로 읽으므로 버퍼가 있는 편이 낫고, `fgets`
         *          하나로 끝납니다.
         */
        FILE* asOutputStream( void* pHandle )
        {
            return static_cast<FILE*>( pHandle );
        }

        /** @brief 자식에서 닫을 서술자의 상한 — **fork 전에** 구한다(자식에서는 async-signal-safe 함수만 부른다). */
        int32 getDescriptorLimit()
        {
            const int64 openMax = static_cast<int64>( sysconf( _SC_OPEN_MAX ) );
            return ( openMax <= 0 || openMax > 65536 ) ? 65536 : static_cast<int32>( openMax );
        }

        /**
         * @brief (자식에서) 표준 입출력 밖의 서술자를 전부 닫습니다. async-signal-safe 한 호출만 쓴다.
         * @details CLOEXEC 없이 열린 것(로그 파일 · 남의 파이프 · 소켓)이 exec 를 넘어 자식과 그 손자에게 그대로 간다 — 파이썬
         *          subprocess 의 기본값(close_fds)과 같은 이유다.
         */
        void closeDescriptorsAboveStandardInChild( int32 descriptorMax )
        {
            int32 closedFrom = 3;
    #if defined( SYS_close_range )
            if ( syscall( SYS_close_range, 3U, ~0U, 0U ) == 0 )
                closedFrom = descriptorMax;
    #endif
            for ( int32 descriptor = closedFrom; descriptor < descriptorMax; ++descriptor )
                close( descriptor );
        }

        /** @brief `waitpid` 가 준 상태를 종료 코드로 바꿉니다. 시그널로 죽었으면 셸 규약대로 128 + 시그널 번호입니다. */
        int32 exitCodeFromStatus( int32 status )
        {
            if ( WIFEXITED( status ) )
                return static_cast<int32>( WEXITSTATUS( status ) );
            if ( WIFSIGNALED( status ) )
                return 128 + static_cast<int32>( WTERMSIG( status ) );
            return -1;
        }

        /** @brief **fork 전에** 만들어 두는 exec 재료입니다 — 찾은 실행 파일 경로와 `execv` 에 줄 인자 포인터 배열(nullptr 로 끝난다). 자식에서는 할당하지 않는다. */
        struct PosixExecImage
        {
            string              _path;
            vector<const utf8*> _listArgumentPointer;            ///< `ProcessCommand` 의 글을 가리킨다 — 그 명령이 exec 까지 살아 있어야 한다
            vector<string>      _listEnvironmentText;            ///< 덮어쓴 환경의 `이름=값` 글(`_bOverridesEnvironment` 일 때만 쓴다)
            vector<const utf8*> _listEnvironmentPointer;         ///< `_listEnvironmentText` 를 가리키는 `execve` 의 envp(nullptr 로 끝난다)
            bool                _bOverridesEnvironment{ false }; ///< false 면 `execv` 로 이 프로세스의 환경을 그대로 물려준다
        };

        /**
         * @brief 이 프로세스의 환경에 @p listEntry 를 덮어쓴 envp 를 **fork 전에** @p inoutImage 에 만듭니다. 이름이 비었거나 `=` 를 품으면 false 입니다.
         * @details `environ` 을 읽는 곳은 여기뿐이다(`getenv` 계열은 `ProcessEnvironment` 가 맡는다) — 환경 전체를 복사해야 하는 일이라 이름 하나를 묻는 그쪽 API 로는 못 한다.
         */
        [[nodiscard]] bool prepareEnvironmentOverride( const vector<ProcessEnvironmentEntry>& listEntry, PosixExecImage& inoutImage )
        {
            inoutImage._bOverridesEnvironment = false;
            if ( listEntry.empty() )
                return true;
            for ( const ProcessEnvironmentEntry& entry : listEntry )
            {
                if ( entry._name.empty() || entry._name.find( '=' ) != string::npos )
                {
                    SW_LOG_ERROR_ID( "Process.InvalidEnvironmentName", "Process launch got an environment entry with an empty name or '=' in the name" );
                    return false;
                }
            }
            inoutImage._listEnvironmentText.clear();
            for ( const utf8* const* ppLine = environ; ppLine != nullptr && *ppLine != nullptr; ++ppLine )
            {
                const string line( *ppLine );
                bool         bOverridden = false;
                for ( const ProcessEnvironmentEntry& entry : listEntry )
                {
                    const size_t nameLength   = entry._name.size();
                    const bool   bLongEnough  = line.size() > nameLength;
                    const bool   bNameMatches = bLongEnough && strncmp( line.c_str(), entry._name.c_str(), nameLength ) == 0;
                    if ( bNameMatches && line[nameLength] == '=' )
                        bOverridden = true;
                }
                if ( bOverridden == false )
                    inoutImage._listEnvironmentText.push_back( line );
            }
            for ( const ProcessEnvironmentEntry& entry : listEntry )
            {
                inoutImage._listEnvironmentText.push_back( entry._name + "=" + entry._value );
            }
            inoutImage._listEnvironmentPointer.clear();
            for ( const string& line : inoutImage._listEnvironmentText )
            {
                inoutImage._listEnvironmentPointer.push_back( line.c_str() );
            }
            inoutImage._listEnvironmentPointer.push_back( nullptr );
            inoutImage._bOverridesEnvironment = true;
            return true;
        }

        /** @brief (자식에서) @p image 를 exec 합니다 — 환경을 덮어썼으면 `execve`, 아니면 `execv`. 성공하면 돌아오지 않고 실패하면 errno 를 남기고 돌아옵니다. */
        void execImage( const PosixExecImage& image )
        {
            utf8* const* ppArgument = const_cast<utf8* const*>( image._listArgumentPointer.data() );
            if ( image._bOverridesEnvironment )
                execve( image._path.c_str(), ppArgument, const_cast<utf8* const*>( image._listEnvironmentPointer.data() ) );
            else
                execv( image._path.c_str(), ppArgument );
        }

        /**
         * @brief @p command 를 exec 재료로 바꿉니다. 실행 파일이 비었거나 PATH 에서 찾지 못하면 오류를 남기고 false 입니다.
         * @details 셸을 거치지 않으므로 PATH 검색도 여기서 한다(`execvp` 는 async-signal-safe 목록에 없어 fork 뒤에 부르지 않는다).
         *          경로 명령(`ProcessCommand::at`)은 그대로 쓰고, 이름 명령(`fromSearchPath`)만 PATH 에서 찾는다. PATH 가 없으면 찾지 않고 진다(기본 PATH 로 대신하지 않는다).
         */
        [[nodiscard]] bool preparePosixExec( const ProcessCommand& command, PosixExecImage& outImage )
        {
            outImage._listArgumentPointer.clear();
            if ( command._executable.empty() )
            {
                SW_LOG_ERROR_ID( "Process.NoExecutable", "Process launch needs an executable" );
                return false;
            }
            outImage._path.clear();
            if ( command._bSearchPath == false )
            {
                outImage._path = command._executable;
            }
            else if ( command._executable.find( '/' ) != string::npos )
            {
                SW_LOG_ERROR( "Search-path command '%#' holds a path - use ProcessCommand::at", command._executable );
                return false;
            }
            else
            {
                string searchPath;
                if ( ProcessEnvironment::tryReadNonEmpty( "PATH", searchPath ) )
                {
                    size_t begin = 0;
                    while ( begin <= searchPath.size() && outImage._path.empty() )
                    {
                        const size_t end       = MathUtil::min( searchPath.find( ':', begin ), searchPath.size() );
                        const string directory = end == begin ? string( "." ) : searchPath.substr( begin, end - begin ); // 빈 칸은 현재 폴더(POSIX)
                        const string candidate = directory + "/" + command._executable;
                        if ( access( candidate.c_str(), X_OK ) == 0 )
                            outImage._path = candidate;
                        begin = end + 1;
                    }
                }
                if ( outImage._path.empty() )
                {
                    SW_LOG_ERROR_ID( "Process.LaunchFailed", "Executable '%#' was not found in PATH", command._executable );
                    return false;
                }
            }
            outImage._listArgumentPointer.reserve( command._listArgument.size() + 2 );
            outImage._listArgumentPointer.push_back( command._executable.c_str() );
            for ( const string& argument : command._listArgument )
            {
                outImage._listArgumentPointer.push_back( argument.c_str() );
            }
            outImage._listArgumentPointer.push_back( nullptr );
            return true;
        }

        /** @brief 파이프 서술자 한 쌍에서 열린 것을 닫습니다(열리지 않은 쌍은 -1 이다). */
        void closeLaunchPipePair( int32 ( &inoutArrFd )[2] )
        {
            if ( inoutArrFd[0] >= 0 )
                close( inoutArrFd[0] );
            if ( inoutArrFd[1] >= 0 )
                close( inoutArrFd[1] );
            inoutArrFd[0] = -1;
            inoutArrFd[1] = -1;
        }

        /**
         * @brief 출력 파이프와 표준 입력 파이프(@p bPipeInput 일 때만)를 엽니다. 실패하면 연 것을 닫고 false 입니다.
         * @details 파이프는 exec 를 넘지 않게 연다. 다른 스레드가 거의 동시에 띄운 자식이 이 파이프를 물려받으면, 이쪽의 읽기는 자기 자식이
         *          끝나도 EOF 를 못 받고 **남의 자식이 끝날 때까지** 막힌다. 자식 쪽에서 쓸 끝은 `runLaunchedChild` 의 `dup2` 가 표준 출력으로 옮기며
         *          CLOEXEC 를 해제하므로 그대로 넘어간다. 표준 입력은 끄면 자식이 /dev/null 을 받는다 — 서버 같은 자식이 이 프로세스의 표준 입력을 가로채지 않게.
         */
        [[nodiscard]] bool openLaunchPipes( bool bPipeInput, const utf8* pCommand, int32 ( &outArrOutputFd )[2], int32 ( &outArrInputFd )[2] )
        {
            outArrOutputFd[0] = -1;
            outArrOutputFd[1] = -1;
            outArrInputFd[0]  = -1;
            outArrInputFd[1]  = -1;
    #if defined( SW_PLATFORM_LINUX )
            const bool bPipeOpened = pipe2( outArrOutputFd, O_CLOEXEC ) == 0;
    #else
            const bool bPipeOpened = pipe( outArrOutputFd ) == 0;
            if ( bPipeOpened )
            {
                fcntl( outArrOutputFd[0], F_SETFD, FD_CLOEXEC );
                fcntl( outArrOutputFd[1], F_SETFD, FD_CLOEXEC );
            }
    #endif
            if ( bPipeOpened == false )
            {
                outArrOutputFd[0] = -1;
                outArrOutputFd[1] = -1;
                SW_LOG_ERROR( "pipe() failed for command: %#", pCommand );
                return false;
            }
            if ( bPipeInput && pipe2( outArrInputFd, O_CLOEXEC ) != 0 )
            {
                closeLaunchPipePair( outArrOutputFd );
                outArrInputFd[0] = -1;
                outArrInputFd[1] = -1;
                SW_LOG_ERROR( "pipe() (stdin) failed for command: %#", pCommand );
                return false;
            }
            return true;
        }

        /**
         * @brief (자식에서) 파이프를 표준 입출력에 잇고 @p image 를 셸 없이 exec 합니다. 돌아오지 않습니다.
         * @details 여기서부터 exec 까지는 **async-signal-safe 한 함수만** 부른다. fork 는 부모의 스레드를 하나만 데려오지만
         *          뮤텍스는 통째로 복사되므로, 다른 스레드가 잡고 있던 락(로거의 락이 대표적이다)은 영원히 잠긴 채로 남는다.
         *          여기서 로그를 찍거나 할당을 하면 그대로 멈춘다.
         */
        [[noreturn]] void runLaunchedChild( const int32 ( &arrPipeFd )[2], const int32 ( &arrInputFd )[2], int32 descriptorMax, const utf8* pWorkingDirectory,
                                            const PosixExecImage& image )
        {
            close( arrPipeFd[0] );
            // 표준 에러를 표준 출력에 합친다. `ProcessOutputDelegate` 가 약속한 대로다(빌드 로그는 두 스트림이 원래 순서대로
            // 섞여야 읽을 수 있다).
            dup2( arrPipeFd[1], STDOUT_FILENO );
            dup2( arrPipeFd[1], STDERR_FILENO );
            close( arrPipeFd[1] );
            if ( arrInputFd[0] >= 0 )
            {
                dup2( arrInputFd[0], STDIN_FILENO );
                close( arrInputFd[0] );
                close( arrInputFd[1] );
            }
            else
            {
                const int32 nullFd = open( "/dev/null", O_RDONLY );
                if ( nullFd >= 0 )
                    dup2( nullFd, STDIN_FILENO );
            }

            closeDescriptorsAboveStandardInChild( descriptorMax );

            // 자기만의 프로세스 그룹을 갖게 한다. `terminate` 가 그룹째 죽여야 자식이 띄운 손자 프로세스(cmake 가 띄운 컴파일러)까지
            // 멈춘다. 자식만 죽이면 손자는 계속 돌면서 파이프를 붙들고 있다.
            setpgid( 0, 0 );

            if ( pWorkingDirectory != nullptr && chdir( pWorkingDirectory ) != 0 )
                _exit( 127 );

            execImage( image );
            _exit( 127 ); // exec 가 실패했을 때만 여기에 온다
        }
    } // namespace

    void Process::shutdown()
    {
        closeInput();
        if ( _pStdOutRead != nullptr )
        {
            fclose( asOutputStream( _pStdOutRead ) );
            _pStdOutRead = nullptr;
        }

        // Windows 와 마찬가지로 **분리**한다. 아직 도는 자식을 기다리지 않는다. 이미 끝나 있으면 여기서 거둬 좀비를
        // 남기지 않고, 아직 돌고 있으면 이 프로세스가 끝난 뒤 init 이 거둔다.
        // (`pclose` 처럼 기다리면 소멸자가 자식이 끝날 때까지 막힌다.)
        const pid_t childPid = static_cast<pid_t>( _processID.load() );
        if ( childPid > 0 )
        {
            int32 status = 0;
            waitpid( childPid, &status, WNOHANG );
        }

        _bufferedOutput.clear();
        _pNativeHandle = nullptr;
        _processID.store( 0 );
    }

    bool Process::launch( const ProcessCommand& command, const ProcessOptions& options )
    {
        shutdown();

        // 자식에게 넘길 문자열은 **fork 전에** 만들어 둔다. 자식 블록(`runLaunchedChild`)에서는 할당을 하지 않는다.
        PosixExecImage image;
        if ( preparePosixExec( command, image ) == false || prepareEnvironmentOverride( options._listEnvironment, image ) == false )
            return false;
        const string commandLine       = command.toCommandLine(); // 로그용
        const utf8*  pCommand          = commandLine.c_str();
        const utf8*  pWorkingDirectory = options._workingDirectory.empty() ? nullptr : options._workingDirectory.c_str();

        int32 arrPipeFd[2];
        int32 arrInputFd[2];
        if ( openLaunchPipes( options._bPipeStandardInput, pCommand, arrPipeFd, arrInputFd ) == false )
            return false;

        const int32 descriptorMax = getDescriptorLimit();

        const pid_t childPid = fork();
        if ( childPid < 0 )
        {
            closeLaunchPipePair( arrPipeFd );
            closeLaunchPipePair( arrInputFd );
            SW_LOG_ERROR( "fork() failed for command: %#", pCommand );
            return false;
        }

        if ( childPid == 0 )
            runLaunchedChild( arrPipeFd, arrInputFd, descriptorMax, pWorkingDirectory, image );

        // ---- 부모 ----
        close( arrPipeFd[1] );
        if ( arrInputFd[0] >= 0 )
        {
            close( arrInputFd[0] );
            _pStdInWrite = reinterpret_cast<void*>( static_cast<intptr_t>( arrInputFd[1] ) + 1 );
        }

        // 자식과 부모 중 누가 먼저 도는지는 정해져 있지 않다. 그래서 그룹 설정은 **양쪽에서** 부른다(먼저 도는 쪽이
        // 이기고, 나중 것은 조용히 실패한다). POSIX 가 권하는 관용구다.
        setpgid( childPid, childPid );

        FILE* pStream = fdopen( arrPipeFd[0], "r" );
        if ( pStream == nullptr )
        {
            close( arrPipeFd[0] );
            closeInput();
            kill( -childPid, SIGKILL );
            int32 status = 0;
            waitpid( childPid, &status, 0 );
            SW_LOG_ERROR( "fdopen() failed for command: %#", pCommand );
            return false;
        }

        _pStdOutRead   = pStream;
        _pNativeHandle = reinterpret_cast<void*>( static_cast<uintptr_t>( childPid ) );
        _processID.store( static_cast<int32>( childPid ) );
        return true;
    }

    bool Process::launchDetached( const ProcessCommand& command, const ProcessOptions& options )
    {
        if ( options._listEnvironment.empty() == false )
        {
            SW_LOG_ERROR_ID( "Process.DetachedEnvironment", "Detached process launch does not take environment overrides - use Process::launch" );
            return false;
        }
        PosixExecImage image;
        if ( preparePosixExec( command, image ) == false )
            return false;
        const string commandLine       = command.toCommandLine(); // 로그용
        const utf8*  pCommand          = commandLine.c_str();
        const utf8*  pWorkingDirectory = options._workingDirectory.empty() ? nullptr : options._workingDirectory.c_str();
        const int32  descriptorMax     = getDescriptorLimit();

        // 두 번 fork 한다 — 가운데 프로세스는 손자를 띄우자마자 끝나고 여기서 바로 거둔다. 손자의 부모는 init 이 되어 끝나도
        // 좀비로 남지 않고, 이 프로세스는 손자가 언제 끝나든 기다리지 않는다.
        const pid_t middlePid = fork();
        if ( middlePid < 0 )
        {
            SW_LOG_ERROR( "fork() failed for detached command: %#", pCommand );
            return false;
        }

        if ( middlePid == 0 )
        {
            // ---- 가운데 · 손자: exec 까지 async-signal-safe 한 함수만 ----
            setsid();
            const pid_t grandchildPid = fork();
            if ( grandchildPid != 0 )
                _exit( grandchildPid < 0 ? 127 : 0 );

            const int32 nullFd = open( "/dev/null", O_RDWR );
            if ( nullFd >= 0 )
            {
                dup2( nullFd, STDIN_FILENO );
                dup2( nullFd, STDOUT_FILENO );
                dup2( nullFd, STDERR_FILENO );
            }
            closeDescriptorsAboveStandardInChild( descriptorMax );

            if ( pWorkingDirectory != nullptr && chdir( pWorkingDirectory ) != 0 )
                _exit( 127 );
            execv( image._path.c_str(), const_cast<utf8* const*>( image._listArgumentPointer.data() ) );
            _exit( 127 );
        }

        int32 status = 0;
        pid_t reaped = 0;
        do
        {
            reaped = waitpid( middlePid, &status, 0 );
        } while ( reaped < 0 && errno == EINTR );

        const bool bLaunched = reaped == middlePid && WIFEXITED( status ) && WEXITSTATUS( status ) == 0;
        if ( bLaunched == false )
            SW_LOG_ERROR( "Failed to launch detached command: %#", pCommand );
        return bLaunched;
    }

    bool Process::launchURL( string_view url )
    {
        return launchDetached( ProcessCommand::fromSearchPath( "xdg-open", { string( url ) } ) );
    }

    bool Process::showInFileManager( string_view filePath )
    {
        // 리눅스 파일 관리자에는 "선택한 채로 열기" 표준이 없다 — 그 폴더를 연다. Process 는 File 보다 아래 층이라 FileUtil 을 쓰지 않는다.
        const size_t separatorIndex = filePath.find_last_of( '/' );
        const string directory      = separatorIndex == string_view::npos ? string( "." ) : string( filePath.substr( 0, separatorIndex == 0 ? 1 : separatorIndex ) );
        return launchDetached( ProcessCommand::fromSearchPath( "xdg-open", { directory } ) );
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
            {
                outLine.pop_back();
            }
            return true;
        }

        return false;
    }

    int32 Process::waitForExit()
    {
        // pid 는 **한 번만 읽어 지역 변수에 담는다.** 아래 `terminate` 주석과 같은 이유다.
        const pid_t childPid = static_cast<pid_t>( _processID.load() );
        if ( childPid <= 0 )
            return -1;

        int32 status = 0;
        pid_t reaped = 0;
        do
        {
            reaped = waitpid( childPid, &status, 0 );
        } while ( reaped < 0 && errno == EINTR );

        // **거둔 뒤에는 pid 를 놓는다.** 그 번호는 OS 가 곧 다른 프로세스에 준다. 계속 들고 있으면 언젠가 소멸자의
        // `waitpid` 가 다른(정확히는 이 프로세스가 나중에 띄운 다른) 자식을 거둬, 그쪽의 `waitForExit` 에서 종료 코드를
        // 빼앗는다.
        _processID.store( 0 );
        _pNativeHandle = nullptr;

        return ( reaped < 0 ) ? -1 : exitCodeFromStatus( status );
    }

    bool Process::terminate( int32 exitCode )
    {
        // POSIX 는 자식의 종료 코드를 정할 수 없다. 시그널로 죽이고, `waitForExit` 은 128 + 시그널 번호를 준다.
        (void)exitCode;

        // **pid 를 한 번만 읽어 지역 변수에 담는다.** 이 함수는 다른 스레드가 `readOutputLine` · `waitForExit` 을 도는
        // 중에 불린다(`ModuleCompiler::cancel` 이 UI 스레드에서 그렇게 부른다). 확인한 뒤에 `waitForExit` 이 `_processID` 를
        // 0 으로 만들면 아래 `kill( -_processID, … )` 이 `kill( 0, … )` 이 되는데, 그것은 **이 프로세스 자신의 그룹에
        // SIGKILL 을 보내는 것**이다. 에디터가 통째로 죽는다. 지역 변수에 담아 두면 그 틈이 아예 없다.
        const pid_t childPid = static_cast<pid_t>( _processID.load() );
        if ( childPid <= 0 )
            return false;

        // 그룹 전체에 보낸다. 자식이 손자 프로세스를 띄웠다면(cmake → 컴파일러) 그것까지 죽는다.
        if ( kill( -childPid, SIGKILL ) != 0 && kill( childPid, SIGKILL ) != 0 )
        {
            SW_LOG_WARNING( "kill(SIGKILL) failed for pid %#", static_cast<int32>( childPid ) );
            return false;
        }

        // 아직 죽지 않았을 수 있다. Windows 의 `TerminateProcess` 와 같은 **요청**이다. 확실히 하려면 호출하는 쪽이
        // `waitForExit` 으로 기다린다.
        return true;
    }

    bool Process::writeInput( string_view text )
    {
        if ( _pStdInWrite == nullptr )
            return false;
        // 자식이 먼저 끝났으면 SIGPIPE 가 이 프로세스를 죽인다 — 테스트 실행 파일은 신호 처리기를 두지 않으므로 여기서 한 번 무시로 등록한다(프로세스 전역).
        (void)signal( SIGPIPE, SIG_IGN );
        const int32 descriptor = static_cast<int32>( reinterpret_cast<intptr_t>( _pStdInWrite ) - 1 );
        size_t      written    = 0;
        while ( written < text.size() )
        {
            const ssize_t chunk = write( descriptor, text.data() + written, text.size() - written );
            if ( chunk < 0 && errno == EINTR )
                continue;
            if ( chunk <= 0 )
                return false;
            written += static_cast<size_t>( chunk );
        }
        return true;
    }

    void Process::closeInput()
    {
        if ( _pStdInWrite != nullptr )
        {
            close( static_cast<int32>( reinterpret_cast<intptr_t>( _pStdInWrite ) - 1 ) );
            _pStdInWrite = nullptr;
        }
    }

    bool Process::requestStop()
    {
        const pid_t childPid = static_cast<pid_t>( _processID.load() );
        return childPid > 0 && ( kill( -childPid, SIGTERM ) == 0 || kill( childPid, SIGTERM ) == 0 );
    }

    bool Process::isRunning() const
    {
        const pid_t childPid = static_cast<pid_t>( _processID.load() );
        if ( childPid <= 0 )
            return false;

        // 핵심은 `WNOWAIT` 이다. **회수하지 않고** 묻는다. `waitpid(WNOHANG)` 로 물으면 끝난 자식을 그 자리에서 회수해
        // 버려서, 이어서 부르는 `waitForExit` 이 종료 코드를 영영 받지 못한다.
        siginfo_t info{};
        info.si_pid = 0;
        if ( waitid( P_PID, static_cast<id_t>( childPid ), &info, WEXITED | WNOHANG | WNOWAIT ) != 0 )
            return false; // 물을 수 없다. 이미 거뒀거나 이 프로세스의 자식이 아니다

        return info.si_pid == 0; // 0 이면 아직 끝나지 않았다
    }

    int32 Process::getCurrentProcessID()
    {
        return static_cast<int32>( ::getpid() );
    }

    string Process::getHostName()
    {
        utf8 arrBuffer[256]{};
        if ( ::gethostname( arrBuffer, sizeof( arrBuffer ) - 1 ) != 0 )
            return {};
        return string( arrBuffer );
    }

    bool Process::isProcessAlive( int32 processID )
    {
        // 0 이하를 kill 에 넘기면 프로세스 그룹을 가리킨다. 여기서 막는다.
        if ( processID <= 0 )
            return false;

        // 신호 0 은 보내지 않고 대상이 있는지만 본다. EPERM 은 있지만 신호를 보낼 권한이 없다는 뜻이다.
        if ( kill( static_cast<pid_t>( processID ), 0 ) == 0 )
            return true;
        return errno == EPERM;
    }

    bool Process::isDebuggerPresent()
    {
        // ptrace 로 붙은 디버거(gdb · lldb)는 /proc/self/status 의 TracerPid 에 pid 를 남긴다. 0 이면 아무도 붙지 않았다.
        const int32 descriptor = ::open( "/proc/self/status", O_RDONLY | O_CLOEXEC );
        if ( descriptor < 0 )
            return false;
        utf8          arrText[constant::kMaxBuffer4096]{};
        const ssize_t readSize = ::read( descriptor, arrText, sizeof( arrText ) );
        ::close( descriptor );
        if ( readSize <= 0 )
            return false;
        const string_view text( arrText, static_cast<size_t>( readSize ) );
        const string_view kField = "TracerPid:";
        size_t            cursor = text.find( kField );
        if ( cursor == string_view::npos )
            return false;
        cursor = text.find_first_not_of( " \t", cursor + kField.size() );
        return cursor != string_view::npos && text[cursor] != '0' && text[cursor] != '\n';
    }
} // namespace sw

#endif
