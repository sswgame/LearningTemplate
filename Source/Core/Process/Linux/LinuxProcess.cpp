#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
    SW_LOG_CALLER( "LinuxProcess" );

    void Process::cleanup()
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
        cleanup();

        string cmd = string( command );
        if ( options._workingDirectory.empty() == false )
        {
            cmd = "cd \"" + options._workingDirectory + "\" && " + cmd;
        }
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

    bool Process::terminate( int32 exitCode )
    {
        (void)exitCode;
        if ( _pStdOutRead != nullptr )
        {
            pclose( static_cast<FILE*>( _pStdOutRead ) );
            _pStdOutRead = nullptr;
            _bRunning    = false;
            return true;
        }
        return false;
    }

    bool Process::isRunning() const
    {
        return _bRunning;
    }
} // namespace sw

#endif
