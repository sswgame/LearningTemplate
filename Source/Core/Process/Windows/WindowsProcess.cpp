#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
    SW_LOG_CALLER( "WindowsProcess" );

    namespace
    {
        struct WindowsProcessInternal
        {
            /**
             * @brief 버퍼에 개행이 있으면 첫 줄을 `outLine` 으로 떼어 내고(개행 문자는 뺍니다) true 를 돌려줍니다. CRLF 는 한 개행으로 칩니다.
             * @details `readOutputLine` 이 읽기 전과 파이프를 읽을 때마다 부릅니다.
             */
            static bool takeBufferedLine( string& inoutBuffer, string& outLine )
            {
                const size_t newlinePos = inoutBuffer.find_first_of( "\r\n" );
                if ( newlinePos == string::npos )
                    return false;

                outLine          = inoutBuffer.substr( 0, newlinePos );
                const bool bCrLf = inoutBuffer[newlinePos] == '\r' && newlinePos + 1 < inoutBuffer.length() && inoutBuffer[newlinePos + 1] == '\n';
                inoutBuffer.erase( 0, newlinePos + ( bCrLf ? 2 : 1 ) );
                return true;
            }
        };
    } // namespace

    void Process::shutdown()
    {
        closeInput();
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
        _processID.store( 0 );
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

        // 표준 입력: 파이프(쓰는 끝은 이 프로세스만) 또는 NUL. 비워 두면 자식은 표준 입력 핸들이 없다.
        HANDLE hStdInRead  = nullptr;
        HANDLE hStdInWrite = nullptr;
        if ( options._bPipeStandardInput )
        {
            if ( CreatePipe( &hStdInRead, &hStdInWrite, &saAttr, 0 ) == FALSE )
            {
                CloseHandle( hStdOutRead );
                CloseHandle( hStdOutWrite );
                SW_LOG_ERROR( "CreatePipe (stdin) failed!" );
                return false;
            }
            SetHandleInformation( hStdInWrite, HANDLE_FLAG_INHERIT, 0 );
        }
        else
        {
            hStdInRead = CreateFileW( L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &saAttr, OPEN_EXISTING, 0, nullptr );
            if ( hStdInRead == INVALID_HANDLE_VALUE )
                hStdInRead = nullptr;
        }

        string  cmdStr     = string( command );
        wstring wsCmdLine  = StringUtil::utf8ToUtf16( cmdStr.c_str() );
        wstring wsBuildDir = StringUtil::utf8ToUtf16( options._workingDirectory.c_str() );

        // **자식이 물려받는 핸들을 이 파이프 하나로 못박는다.** `bInheritHandles = TRUE` 만 주면 이 프로세스의 *상속 가능한 핸들
        // 전부* 가 넘어간다. 두 스레드가 거의 동시에 자식을 띄우면 서로의 파이프 쓰기 끝을 물려받고, 그러면 한쪽의 읽기는 자기 자식이
        // 끝나도 EOF 를 못 받고 **남의 자식이 끝날 때까지** 막힌다(Microsoft 가 권하는 방법이 이 속성 목록이다).
        SIZE_T attributeListSize = 0;
        InitializeProcThreadAttributeList( nullptr, 1, 0, &attributeListSize );
        vector<uint8>                attributeListBytes( attributeListSize );
        LPPROC_THREAD_ATTRIBUTE_LIST pAttributeList        = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( attributeListBytes.data() );
        HANDLE                       arrInheritedHandle[2] = { hStdOutWrite, hStdInRead };
        const DWORD                  inheritedHandleSize   = static_cast<DWORD>( sizeof( HANDLE ) * ( hStdInRead != nullptr ? 2 : 1 ) );
        const bool                   bAttributeReady       = InitializeProcThreadAttributeList( pAttributeList, 1, 0, &attributeListSize ) != FALSE;
        if ( bAttributeReady == false ||
             UpdateProcThreadAttribute( pAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, arrInheritedHandle, inheritedHandleSize, nullptr, nullptr ) == FALSE )
        {
            if ( bAttributeReady )
                DeleteProcThreadAttributeList( pAttributeList );
            CloseHandle( hStdOutRead );
            CloseHandle( hStdOutWrite );
            if ( hStdInRead != nullptr )
                CloseHandle( hStdInRead );
            if ( hStdInWrite != nullptr )
                CloseHandle( hStdInWrite );
            SW_LOG_ERROR( "Failed to restrict inherited handles for: %#", cmdStr.c_str() );
            return false;
        }

        STARTUPINFOEXW startupInfo{};
        startupInfo.StartupInfo.cb         = sizeof( STARTUPINFOEXW );
        startupInfo.StartupInfo.hStdError  = hStdOutWrite;
        startupInfo.StartupInfo.hStdOutput = hStdOutWrite;
        startupInfo.StartupInfo.hStdInput  = hStdInRead;
        startupInfo.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startupInfo.lpAttributeList = pAttributeList;

        // 정상 종료 요청(`requestStop` — 콘솔 그룹에 Ctrl+Break)을 받을 자식은 이 프로세스의 콘솔을 나눠 써야 한다. CREATE_NO_WINDOW 는 숨은 새 콘솔을
        // 만들어 이벤트가 닿지 않는다. 이 프로세스에 콘솔이 없으면(서비스 · 창 앱) 나눠 쓸 것이 없으니 그대로 숨긴다 — 새 콘솔 창이 뜨지 않게.
        DWORD       consoleProcessID = 0;
        const bool  bShareConsole    = options._bNewProcessGroup && GetConsoleProcessList( &consoleProcessID, 1 ) != 0;
        const DWORD creationFlags    = ( options._bCreateWindow || bShareConsole ? 0 : CREATE_NO_WINDOW ) | EXTENDED_STARTUPINFO_PRESENT |
                                    ( options._bNewProcessGroup ? CREATE_NEW_PROCESS_GROUP : 0 );

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
            &startupInfo.StartupInfo,
            &pi );

        DeleteProcThreadAttributeList( pAttributeList );
        CloseHandle( hStdOutWrite );
        if ( hStdInRead != nullptr )
            CloseHandle( hStdInRead );

        if ( bCreated == FALSE )
        {
            CloseHandle( hStdOutRead );
            if ( hStdInWrite != nullptr )
                CloseHandle( hStdInWrite );
            SW_LOG_ERROR( "Failed to launch command: %#", cmdStr.c_str() );
            return false;
        }

        _pNativeHandle    = pi.hProcess;
        _pNativeThread    = pi.hThread;
        _pStdOutRead      = hStdOutRead;
        _pStdInWrite      = hStdInWrite;
        _bNewProcessGroup = options._bNewProcessGroup;
        _processID.store( static_cast<int32>( pi.dwProcessId ) );

        return true;
    }

    bool Process::launchDetached( string_view command, const ProcessOptions& options )
    {
        const string  cmdStr      = string( command );
        wstring       wsCmdLine   = StringUtil::utf8ToUtf16( cmdStr.c_str() );
        const wstring wsDirectory = StringUtil::utf8ToUtf16( options._workingDirectory.c_str() );

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof( STARTUPINFOW );

        // 핸들을 하나도 물려주지 않는다(bInheritHandles = FALSE) — 띄운 프로그램이 이 프로세스의 파이프 · 파일을 붙들지 않는다.
        PROCESS_INFORMATION pi{};
        const BOOL          bCreated = CreateProcessW( nullptr, wsCmdLine.data(), nullptr, nullptr, FALSE, options._bCreateWindow ? 0 : CREATE_NO_WINDOW,
                                                       nullptr, options._workingDirectory.empty() ? nullptr : wsDirectory.c_str(), &startupInfo, &pi );
        if ( bCreated == FALSE )
        {
            SW_LOG_ERROR( "Failed to launch detached command: %#", cmdStr.c_str() );
            return false;
        }

        CloseHandle( pi.hThread );
        CloseHandle( pi.hProcess );
        return true;
    }

    bool Process::readOutputLine( string& outLine )
    {
        outLine.clear();

        if ( _pStdOutRead == nullptr )
            return false;

        // 1) 버퍼에 이미 개행 문자가 남아 있는지 확인한다
        if ( WindowsProcessInternal::takeBufferedLine( _bufferedOutput, outLine ) )
            return true;

        // 2) 파이프에서 데이터를 더 읽는다
        utf8  arrReadBuffer[constant::kMaxBuffer4096];
        DWORD bytesRead = 0;

        while ( ReadFile( static_cast<HANDLE>( _pStdOutRead ), arrReadBuffer, sizeof( arrReadBuffer ) - 1, &bytesRead, nullptr ) != FALSE && bytesRead > 0 )
        {
            arrReadBuffer[bytesRead] = '\0';
            _bufferedOutput.append( arrReadBuffer, bytesRead );
            if ( WindowsProcessInternal::takeBufferedLine( _bufferedOutput, outLine ) )
                return true;
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
        return static_cast<int32>( exitCode );
    }

    bool Process::terminate( int32 exitCode )
    {
        if ( _pNativeHandle == nullptr )
            return false;

        return TerminateProcess( static_cast<HANDLE>( _pNativeHandle ), static_cast<UINT>( exitCode ) ) != FALSE;
    }

    bool Process::writeInput( string_view text )
    {
        if ( _pStdInWrite == nullptr )
            return false;
        size_t written = 0;
        while ( written < text.size() )
        {
            DWORD chunk = 0;
            if ( WriteFile( static_cast<HANDLE>( _pStdInWrite ), text.data() + written, static_cast<DWORD>( text.size() - written ), &chunk, nullptr ) == FALSE )
                return false;
            written += chunk;
        }
        return true;
    }

    void Process::closeInput()
    {
        if ( _pStdInWrite != nullptr )
        {
            CloseHandle( static_cast<HANDLE>( _pStdInWrite ) );
            _pStdInWrite = nullptr;
        }
    }

    bool Process::requestStop()
    {
        const int32 processID = _processID.load();
        // Ctrl+Break 는 콘솔 그룹에 간다 — 새 그룹으로 띄운 자식이 아니면 이 프로세스(와 같은 콘솔의 모두)가 받는다.
        return _bNewProcessGroup && processID > 0 && GenerateConsoleCtrlEvent( CTRL_BREAK_EVENT, static_cast<DWORD>( processID ) ) != FALSE;
    }

    bool Process::isRunning() const
    {
        if ( _pNativeHandle == nullptr )
            return false;

        // 종료 코드로 묻지 않는다. `STILL_ACTIVE` 의 값이 **259** 라서, 259 로 끝난 자식은 영원히 실행 중으로 보인다
        // (`cmd /c exit 259` 로 재현된다). 핸들 자체가 신호 상태인지 묻는 것이 올바른 방법이다.
        return WaitForSingleObject( static_cast<HANDLE>( _pNativeHandle ), 0 ) == WAIT_TIMEOUT;
    }

    int32 Process::getCurrentProcessID()
    {
        return static_cast<int32>( GetCurrentProcessId() );
    }

    bool Process::isProcessAlive( int32 processID )
    {
        if ( processID <= 0 )
            return false;

        HANDLE hProcess = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>( processID ) );
        if ( hProcess == nullptr )
            return GetLastError() == ERROR_ACCESS_DENIED; // 있지만 열 수 없다

        // 끝난 프로세스도 누가 핸들을 쥐고 있으면 열린다. 객체가 신호 상태인지로 가른다.
        const bool bAlive = WaitForSingleObject( hProcess, 0 ) == WAIT_TIMEOUT;
        CloseHandle( hProcess );
        return bAlive;
    }
} // namespace sw

#endif
