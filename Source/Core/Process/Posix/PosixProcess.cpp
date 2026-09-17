#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#if defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
    #include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
    SW_LOG_CALLER( "PosixProcess" );

    void Process::shutdown()
    {
        if ( _pStdOutRead != nullptr )
        {
            pclose( static_cast<FILE*>( _pStdOutRead ) );
            _pStdOutRead = nullptr;
        }

        _bufferedOutput.clear();
        _processId = 0;
        _bRunning  = false;
    }

    bool Process::launch( string_view command, const ProcessOptions& options )
    {
        shutdown();

        string cmd = string( command );
        if ( options._workingDirectory.empty() == false )
            cmd = "cd \"" + options._workingDirectory + "\" && " + cmd;
        cmd += " 2>&1";

        FILE* pPipe = popen( cmd.c_str(), "r" );
        if ( pPipe == nullptr )
        {
            SW_LOG_ERROR( "popen failed for command: %#", cmd.c_str() );
            return false;
        }

        _pStdOutRead = pPipe;
        _bRunning    = true;
        return true;
    }

    bool Process::readOutputLine( string& outLine )
    {
        outLine.clear();

        if ( _pStdOutRead == nullptr )
            return false;

        utf8 arrLineBuffer[constant::kMaxBuffer4096];
        if ( fgets( arrLineBuffer, sizeof( arrLineBuffer ), static_cast<FILE*>( _pStdOutRead ) ) != nullptr )
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
        if ( _pStdOutRead == nullptr )
            return -1;

        const int32 status = pclose( static_cast<FILE*>( _pStdOutRead ) );
        _pStdOutRead       = nullptr;
        _bRunning          = false;

        return WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
    }

    // 이 구현은 자식을 죽이지 못한다 — `popen` 이 pid 를 주지 않기 때문이다. 예전에는 `pclose` 를
    // 불러 놓고 true 를 돌려줬는데, 그것은 종료가 아니라 **자식이 스스로 끝날 때까지 기다리는 것**이고
    // 게다가 위험하다: 유일한 실제 호출부(`ModuleCompiler::cancel`)는 UI 스레드에서 이것을 부르는데,
    // 그 시각 빌드 스레드는 같은 `FILE*` 위에서 `fgets` 를 돌고 있다 — `pclose` 가 그 스트림을
    // 해제하므로 미정의 동작이고, 그 전에 컴파일이 끝날 때까지 `_mutex` 를 쥔 채 UI 가 멈춘다.
    // 못 하는 일은 못 한다고 말한다. fork/exec 로 바꿔 pid 를 들면 진짜로 죽일 수 있다(백로그).
    bool Process::terminate( int32 exitCode )
    {
        (void)exitCode;
        SW_LOG_WARNING( "terminate() is not supported on this platform (popen gives no child pid)" );
        return false;
    }

    bool Process::isRunning() const
    {
        return _bRunning;
    }
} // namespace sw

#endif
