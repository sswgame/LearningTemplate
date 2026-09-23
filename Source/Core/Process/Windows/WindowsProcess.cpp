#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
    SW_LOG_CALLER( "WindowsProcess" );

    void Process::shutdown()
    {
        if ( _pStdOutRead != nullptr )
        {
            CloseHandle( static_cast<HANDLE>( _pStdOutRead ) );
            _pStdOutRead = nullptr;
        }

        if ( _pNativeThread != nullptr )
        {
            CloseHandle( static_cast<HANDLE>( _pNativeThread ) );
            _pNativeThread = nullptr;
        }

        if ( _pNativeHandle != nullptr )
        {
            CloseHandle( static_cast<HANDLE>( _pNativeHandle ) );
            _pNativeHandle = nullptr;
        }

        _bufferedOutput.clear();
        _processId = 0;
        _bRunning  = false;
    }

    bool Process::launch( string_view command, const ProcessOptions& options )
    {
        shutdown();

        SECURITY_ATTRIBUTES saAttr{};
        saAttr.nLength              = sizeof( SECURITY_ATTRIBUTES );
        saAttr.bInheritHandle       = TRUE;
        saAttr.lpSecurityDescriptor = nullptr;

        HANDLE hStdOutRead  = nullptr;
        HANDLE hStdOutWrite = nullptr;
        if ( CreatePipe( &hStdOutRead, &hStdOutWrite, &saAttr, 0 ) == FALSE )
        {
            SW_LOG_ERROR( "CreatePipe failed!" );
            return false;
        }

        SetHandleInformation( hStdOutRead, HANDLE_FLAG_INHERIT, 0 );

        string  cmdStr     = string( command );
        wstring wsCmdLine  = StringUtil::utf8ToUtf16( cmdStr.c_str() );
        wstring wsBuildDir = StringUtil::utf8ToUtf16( options._workingDirectory.c_str() );

        STARTUPINFOW si{};
        si.cb         = sizeof( STARTUPINFOW );
        si.hStdError  = hStdOutWrite;
        si.hStdOutput = hStdOutWrite;
        si.dwFlags |= STARTF_USESTDHANDLES;

        const DWORD creationFlags = options._bCreateWindow ? 0 : CREATE_NO_WINDOW;

        PROCESS_INFORMATION pi{};
        const BOOL          bCreated = CreateProcessW(
            nullptr,
            wsCmdLine.data(),
            nullptr,
            nullptr,
            TRUE,
            creationFlags,
            nullptr,
            options._workingDirectory.empty() ? nullptr : wsBuildDir.c_str(),
            &si,
            &pi );

        CloseHandle( hStdOutWrite );

        if ( bCreated == FALSE )
        {
            CloseHandle( hStdOutRead );
            SW_LOG_ERROR( "Failed to launch command: %#", cmdStr.c_str() );
            return false;
        }

        _pNativeHandle = pi.hProcess;
        _pNativeThread = pi.hThread;
        _pStdOutRead   = hStdOutRead;
        _processId     = static_cast<int32>( pi.dwProcessId );
        _bRunning      = true;

        return true;
    }

    bool Process::readOutputLine( string& outLine )
    {
        outLine.clear();

        if ( _pStdOutRead == nullptr )
            return false;

        // 1) 버퍼에 이미 개행 문자가 남아 있는지 확인한다
        size_t newlinePos = _bufferedOutput.find_first_of( "\r\n" );
        if ( newlinePos != string::npos )
        {
            outLine = _bufferedOutput.substr( 0, newlinePos );
            if ( newlinePos + 1 < _bufferedOutput.length() && _bufferedOutput[newlinePos] == '\r' && _bufferedOutput[newlinePos + 1] == '\n' )
                _bufferedOutput.erase( 0, newlinePos + 2 );
            else
                _bufferedOutput.erase( 0, newlinePos + 1 );
            return true;
        }

        // 2) 파이프에서 데이터를 더 읽는다
        utf8  arrReadBuffer[constant::kMaxBuffer4096];
        DWORD bytesRead = 0;

        while ( ReadFile( static_cast<HANDLE>( _pStdOutRead ), arrReadBuffer, sizeof( arrReadBuffer ) - 1, &bytesRead, nullptr ) != FALSE && bytesRead > 0 )
        {
            arrReadBuffer[bytesRead] = '\0';
            _bufferedOutput.append( arrReadBuffer, bytesRead );

            newlinePos = _bufferedOutput.find_first_of( "\r\n" );
            if ( newlinePos != string::npos )
            {
                outLine = _bufferedOutput.substr( 0, newlinePos );
                if ( newlinePos + 1 < _bufferedOutput.length() && _bufferedOutput[newlinePos] == '\r' && _bufferedOutput[newlinePos + 1] == '\n' )
                    _bufferedOutput.erase( 0, newlinePos + 2 );
                else
                    _bufferedOutput.erase( 0, newlinePos + 1 );
                return true;
            }
        }

        // 3) EOF 에 도달했으면 버퍼에 남은 문자열을 반환한다
        if ( _bufferedOutput.empty() == false )
        {
            outLine = std::move( _bufferedOutput );
            _bufferedOutput.clear();
            return true;
        }

        return false;
    }

    int32 Process::waitForExit()
    {
        if ( _pNativeHandle == nullptr )
            return -1;

        WaitForSingleObject( static_cast<HANDLE>( _pNativeHandle ), INFINITE );

        DWORD exitCode = 0;
        GetExitCodeProcess( static_cast<HANDLE>( _pNativeHandle ), &exitCode );

        _bRunning = false;
        return static_cast<int32>( exitCode );
    }

    bool Process::terminate( int32 exitCode )
    {
        if ( _pNativeHandle == nullptr )
            return false;

        const BOOL ok = TerminateProcess( static_cast<HANDLE>( _pNativeHandle ), static_cast<UINT>( exitCode ) );
        if ( ok != FALSE )
        {
            _bRunning = false;
            return true;
        }
        return false;
    }

    bool Process::isRunning() const
    {
        if ( _pNativeHandle == nullptr )
            return false;

        // 종료 코드로 묻지 않는다. `STILL_ACTIVE` 의 값이 **259** 라서, 259 로 끝난 자식은 영원히 실행 중으로 보인다
        // (`cmd /c exit 259` 로 재현된다). 핸들 자체가 신호 상태인지 묻는 것이 올바른 방법이다.
        return WaitForSingleObject( static_cast<HANDLE>( _pNativeHandle ), 0 ) == WAIT_TIMEOUT;
    }
} // namespace sw

#endif
