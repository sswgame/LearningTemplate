#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"

    #include <shellapi.h>
    #include <shlobj.h>

namespace sw
{
    SW_LOG_CALLER( "WindowsProcess" );

    namespace
    {
        struct WindowsProcessInternal
        {
            /**
             * @brief 버퍼에 개행이 있으면 첫 줄을 `outLine` 으로 분리하고(개행 문자는 뺍니다) true 를 돌려줍니다. CRLF 는 한 개행으로 칩니다.
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

            /** @brief 자식의 표준 출력 · 입력에 쓰는 파이프 끝 넷입니다. 쓰는 끝 · 읽는 끝 중 자식에게 줄 끝은 `closeChildEnds` 로 부모가 닫습니다. */
            struct StdPipes
            {
                HANDLE _hOutRead{ nullptr };
                HANDLE _hOutWrite{ nullptr };
                HANDLE _hInRead{ nullptr };
                HANDLE _hInWrite{ nullptr };

                static void closeHandle( HANDLE& inoutHandle )
                {
                    if ( inoutHandle != nullptr )
                    {
                        CloseHandle( inoutHandle );
                        inoutHandle = nullptr;
                    }
                }

                /** @brief 자식에게 넘긴 끝(표준 출력의 쓰는 끝, 표준 입력의 읽는 끝)을 닫습니다. 띄운 뒤 부모는 쓸모가 없다. */
                void closeChildEnds()
                {
                    closeHandle( _hOutWrite );
                    closeHandle( _hInRead );
                }

                void closeAll()
                {
                    closeHandle( _hOutRead );
                    closeHandle( _hOutWrite );
                    closeHandle( _hInRead );
                    closeHandle( _hInWrite );
                }
            };

            /**
             * @brief 이 프로세스의 환경에 @p listEntry 를 덮어쓴 UTF-16 환경 블록(`이름=값\0` 들 뒤에 `\0` 하나 더)을 만듭니다.
             * @details 이름은 대소문자를 가리지 않고 같게 본다(Windows 규칙). 블록은 이름 순(대소문자 무시)으로 정렬한다(`CreateProcessW` 문서가 권하는 모양).
             * @return 이름이 비었거나 `=` 를 품은 항목이 있으면 false 입니다(블록은 건드리지 않는다).
             */
            static bool makeEnvironmentBlock( const vector<ProcessEnvironmentEntry>& listEntry, vector<utf16>& outListBlock )
            {
                vector<wstring> listLine;
                if ( LPWCH pBase = GetEnvironmentStringsW(); pBase != nullptr )
                {
                    for ( const utf16* pLine = pBase; *pLine != L'\0'; pLine += wcslen( pLine ) + 1 )
                    {
                        listLine.emplace_back( pLine );
                    }
                    FreeEnvironmentStringsW( pBase );
                }
                for ( const ProcessEnvironmentEntry& entry : listEntry )
                {
                    if ( entry._name.empty() || entry._name.find( '=' ) != string::npos )
                        return false;
                    const wstring   name = StringUtil::utf8ToUTF16( entry._name.c_str() );
                    vector<wstring> listKept;
                    for ( const wstring& line : listLine )
                    {
                        const bool bLongEnough  = line.size() > name.size();
                        const bool bNameMatches = bLongEnough && _wcsnicmp( line.c_str(), name.c_str(), name.size() ) == 0;
                        const bool bSameName    = bNameMatches && line[name.size()] == L'=';
                        if ( bSameName == false )
                            listKept.push_back( line );
                    }
                    listKept.push_back( StringUtil::utf8ToUTF16( ( entry._name + "=" + entry._value ).c_str() ) );
                    listLine = std::move( listKept );
                }
                VectorUtil::sort( listLine, []( const wstring& left, const wstring& right )
                {
                    return _wcsicmp( left.c_str(), right.c_str() ) < 0;
                } );
                outListBlock.clear();
                for ( const wstring& line : listLine )
                {
                    outListBlock.insert( outListBlock.end(), line.c_str(), line.c_str() + line.size() + 1 );
                }
                if ( outListBlock.empty() )
                    outListBlock.push_back( L'\0' ); // 항목이 하나도 없는 블록도 널 두 개로 끝난다
                outListBlock.push_back( L'\0' );
                return true;
            }

            /**
             * @brief 표준 출력 파이프와, 표준 입력 파이프(@p bPipeStandardInput) 또는 NUL 을 엽니다. 비워 두면 자식은 표준 입력 핸들이 없다.
             *        실패하면 연 것을 닫고 false 입니다.
             */
            [[nodiscard]] static bool createStdPipes( bool bPipeStandardInput, StdPipes& outPipes )
            {
                SECURITY_ATTRIBUTES saAttr{};
                saAttr.nLength              = sizeof( SECURITY_ATTRIBUTES );
                saAttr.bInheritHandle       = TRUE;
                saAttr.lpSecurityDescriptor = nullptr;

                if ( CreatePipe( &outPipes._hOutRead, &outPipes._hOutWrite, &saAttr, 0 ) == FALSE )
                {
                    SW_LOG_ERROR( "CreatePipe failed!" );
                    return false;
                }
                SetHandleInformation( outPipes._hOutRead, HANDLE_FLAG_INHERIT, 0 );

                // 표준 입력: 파이프(쓰는 끝은 이 프로세스만) 또는 NUL.
                if ( bPipeStandardInput )
                {
                    if ( CreatePipe( &outPipes._hInRead, &outPipes._hInWrite, &saAttr, 0 ) == FALSE )
                    {
                        outPipes.closeAll();
                        SW_LOG_ERROR( "CreatePipe (stdin) failed!" );
                        return false;
                    }
                    SetHandleInformation( outPipes._hInWrite, HANDLE_FLAG_INHERIT, 0 );
                }
                else
                {
                    outPipes._hInRead = CreateFileW( L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &saAttr, OPEN_EXISTING, 0, nullptr );
                    if ( outPipes._hInRead == INVALID_HANDLE_VALUE )
                        outPipes._hInRead = nullptr;
                }
                return true;
            }

            /**
             * @brief **자식이 물려받는 핸들을 이 파이프 하나로 못박는** 속성 목록입니다. `bInheritHandles = TRUE` 만 주면 이 프로세스의 *상속 가능한 핸들
             *        전부* 가 넘어간다. 두 스레드가 거의 동시에 자식을 띄우면 서로의 파이프 쓰기 끝을 물려받고, 그러면 한쪽의 읽기는 자기 자식이
             *        끝나도 EOF 를 못 받고 **남의 자식이 끝날 때까지** 막힌다(Microsoft 가 권하는 방법이 이 속성 목록이다).
             * @details 속성 목록은 핸들 배열을 복사하지 않고 **가리킨다** — 배열이 `CreateProcessW` 까지 살아 있어야 한다. 그래서 배열과 목록을 한 객체가
             *          함께 들고 복사 · 이동을 막는다(배열을 만든 함수의 지역 변수로 두었더니 함수가 끝난 뒤 목록이 사라진 배열을 가리켜 모든 자식 띄우기가 졌다).
             */
            struct InheritedHandleList
            {
                HANDLE                       _arrHandle[2]{ nullptr, nullptr };
                vector<uint8>                _attributeBytes;
                LPPROC_THREAD_ATTRIBUTE_LIST _pAttributeList{ nullptr };

                InheritedHandleList()                                        = default;
                InheritedHandleList( const InheritedHandleList& )            = delete;
                InheritedHandleList& operator=( const InheritedHandleList& ) = delete;
                ~InheritedHandleList()
                {
                    if ( _pAttributeList != nullptr )
                        DeleteProcThreadAttributeList( _pAttributeList );
                }

                /** @brief @p pipes 의 자식 쪽 끝만 물려주는 목록을 만듭니다. 만들지 못하면 false 이고 목록은 비어 있습니다. */
                [[nodiscard]] bool initialize( const StdPipes& pipes )
                {
                    _arrHandle[0]                   = pipes._hOutWrite;
                    _arrHandle[1]                   = pipes._hInRead;
                    const DWORD inheritedHandleSize = static_cast<DWORD>( sizeof( HANDLE ) * ( pipes._hInRead != nullptr ? 2 : 1 ) );
                    SIZE_T      attributeListSize   = 0;
                    InitializeProcThreadAttributeList( nullptr, 1, 0, &attributeListSize );
                    _attributeBytes.resize( attributeListSize );
                    LPPROC_THREAD_ATTRIBUTE_LIST pList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( _attributeBytes.data() );
                    if ( InitializeProcThreadAttributeList( pList, 1, 0, &attributeListSize ) == FALSE )
                        return false;
                    if ( UpdateProcThreadAttribute( pList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, _arrHandle, inheritedHandleSize, nullptr, nullptr ) == FALSE )
                    {
                        DeleteProcThreadAttributeList( pList );
                        return false;
                    }
                    _pAttributeList = pList;
                    return true;
                }
            };

            /**
             * @brief 셸 호출(`ShellExecuteW` · `SHOpenFolderAndSelectItems`) 동안 이 스레드의 COM 을 엽니다. Microsoft 가 셸 호출 앞에 요구한다.
             * @details 이미 다른 모델로 열린 스레드(`RPC_E_CHANGED_MODE`)는 그대로 쓰고 닫지 않는다 — 연 쪽이 닫는다.
             */
            struct ScopedShellCom
            {
                HRESULT _result;

                ScopedShellCom()
                    : _result{ CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE ) }
                {
                }
                ScopedShellCom( const ScopedShellCom& )            = delete;
                ScopedShellCom& operator=( const ScopedShellCom& ) = delete;
                ~ScopedShellCom()
                {
                    if ( SUCCEEDED( _result ) )
                        CoUninitialize();
                }
            };
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

    bool Process::launch( const ProcessCommand& command, const ProcessOptions& options )
    {
        using Internal = WindowsProcessInternal;
        shutdown();

        if ( command._executable.empty() )
        {
            SW_LOG_ERROR( "Process launch needs an executable" );
            return false;
        }

        // 환경 덮어쓰기는 파이프를 열기 전에 만든다 — 이름이 잘못되면 닫을 것이 없는 채로 실패한다.
        vector<utf16> listEnvironmentBlock;
        if ( options._listEnvironment.empty() == false && Internal::makeEnvironmentBlock( options._listEnvironment, listEnvironmentBlock ) == false )
        {
            SW_LOG_ERROR( "Process launch got an environment entry with an empty name or '=' in the name" );
            return false;
        }

        Internal::StdPipes pipes;
        if ( Internal::createStdPipes( options._bPipeStandardInput, pipes ) == false )
            return false;

        const string cmdStr     = command.toCommandLine();
        wstring      wsCmdLine  = StringUtil::utf8ToUTF16( cmdStr.c_str() );
        wstring      wsBuildDir = StringUtil::utf8ToUTF16( options._workingDirectory.c_str() );

        Internal::InheritedHandleList inheritedHandles;
        if ( inheritedHandles.initialize( pipes ) == false )
        {
            pipes.closeAll();
            SW_LOG_ERROR( "Failed to restrict inherited handles for: %#", cmdStr.c_str() );
            return false;
        }

        STARTUPINFOEXW startupInfo{};
        startupInfo.StartupInfo.cb         = sizeof( STARTUPINFOEXW );
        startupInfo.StartupInfo.hStdError  = pipes._hOutWrite;
        startupInfo.StartupInfo.hStdOutput = pipes._hOutWrite;
        startupInfo.StartupInfo.hStdInput  = pipes._hInRead;
        startupInfo.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startupInfo.lpAttributeList = inheritedHandles._pAttributeList;

        // 정상 종료 요청(`requestStop` — 콘솔 그룹에 Ctrl+Break)을 받을 자식은 이 프로세스의 콘솔을 나눠 써야 한다. CREATE_NO_WINDOW 는 숨은 새 콘솔을
        // 만들어 이벤트가 닿지 않는다. 이 프로세스에 콘솔이 없으면(서비스 · 창 앱) 나눠 쓸 것이 없으니 그대로 숨긴다 — 새 콘솔 창이 뜨지 않게.
        DWORD       consoleProcessID = 0;
        const bool  bShareConsole    = options._bNewProcessGroup && GetConsoleProcessList( &consoleProcessID, 1 ) != 0;
        const DWORD creationFlags    = ( options._bCreateWindow || bShareConsole ? 0 : CREATE_NO_WINDOW ) | EXTENDED_STARTUPINFO_PRESENT |
                                    ( options._bNewProcessGroup ? CREATE_NEW_PROCESS_GROUP : 0 ) | ( listEnvironmentBlock.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT );

        PROCESS_INFORMATION pi{};
        const BOOL          bCreated = CreateProcessW(
            nullptr,
            wsCmdLine.data(),
            nullptr,
            nullptr,
            TRUE,
            creationFlags,
            listEnvironmentBlock.empty() ? nullptr : listEnvironmentBlock.data(), // nullptr 이면 이 프로세스의 환경을 그대로 물려준다
            options._workingDirectory.empty() ? nullptr : wsBuildDir.c_str(),
            &startupInfo.StartupInfo,
            &pi );

        pipes.closeChildEnds();

        if ( bCreated == FALSE )
        {
            pipes.closeAll();
            SW_LOG_ERROR( "Failed to launch command: %#", cmdStr.c_str() );
            return false;
        }

        _pNativeHandle    = pi.hProcess;
        _pNativeThread    = pi.hThread;
        _pStdOutRead      = pipes._hOutRead;
        _pStdInWrite      = pipes._hInWrite;
        _bNewProcessGroup = options._bNewProcessGroup;
        _processID.store( static_cast<int32>( pi.dwProcessId ) );

        return true;
    }

    bool Process::launchDetached( const ProcessCommand& command, const ProcessOptions& options )
    {
        if ( command._executable.empty() )
        {
            SW_LOG_ERROR( "Detached process launch needs an executable" );
            return false;
        }
        if ( options._listEnvironment.empty() == false )
        {
            SW_LOG_ERROR( "Detached process launch does not take environment overrides - use Process::launch" );
            return false;
        }
        const string  cmdStr      = command.toCommandLine();
        wstring       wsCmdLine   = StringUtil::utf8ToUTF16( cmdStr.c_str() );
        const wstring wsDirectory = StringUtil::utf8ToUTF16( options._workingDirectory.c_str() );

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

    bool Process::launchURL( string_view url )
    {
        const WindowsProcessInternal::ScopedShellCom com;
        const wstring                                wideURL = StringUtil::utf8ToUTF16( string( url ).c_str() );
        // 32 이하는 오류 코드다(ShellExecute 의 옛 계약).
        const INT_PTR result = reinterpret_cast<INT_PTR>( ShellExecuteW( nullptr, L"open", wideURL.c_str(), nullptr, nullptr, SW_SHOWNORMAL ) );
        if ( result <= 32 )
        {
            SW_LOG_ERROR( "ShellExecuteW failed (%#) for URL: %#", static_cast<int64>( result ), string( url ) );
            return false;
        }
        return true;
    }

    bool Process::showInFileManager( string_view filePath )
    {
        // 탐색기는 백슬래시 경로만 고른다(슬래시면 ILCreateFromPathW 가 못 찾는다).
        string windowsPath( filePath );
        for ( utf8& character : windowsPath )
        {
            if ( character == '/' )
                character = '\\';
        }
        const WindowsProcessInternal::ScopedShellCom com;
        const wstring                                widePath = StringUtil::utf8ToUTF16( windowsPath.c_str() );
        PIDLIST_ABSOLUTE                             pItem    = ILCreateFromPathW( widePath.c_str() );
        if ( pItem == nullptr )
        {
            SW_LOG_ERROR( "ILCreateFromPathW failed for: %#", windowsPath );
            return false;
        }
        const HRESULT result = SHOpenFolderAndSelectItems( pItem, 0, nullptr, 0 );
        ILFree( pItem );
        if ( FAILED( result ) )
        {
            SW_LOG_ERROR( "SHOpenFolderAndSelectItems failed (HRESULT %#) for: %#", static_cast<uint32>( result ), windowsPath );
            return false;
        }
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

    string Process::getHostName()
    {
        utf16 arrBuffer[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD length = static_cast<DWORD>( MAX_COMPUTERNAME_LENGTH + 1 );
        if ( GetComputerNameW( arrBuffer, &length ) == FALSE )
            return {};
        return StringUtil::utf16ToUTF8( arrBuffer );
    }

    bool Process::isDebuggerPresent()
    {
        return IsDebuggerPresent() != FALSE;
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
