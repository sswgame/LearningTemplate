#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Process/Process.h"
#include "Core/Process/ShutdownSignal.h"
#include "Core/Process/StandardInputReader.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"

    #include <DbgHelp.h>
    #include <cstdlib>
    #include <shellapi.h>
    #include <shlobj.h>

namespace sw
{
    namespace
    {
        struct WindowsProcessInternal
        {
            /** @brief 절대 경로 실행 파일을 `CreateProcessW` 의 응용 프로그램 이름(UTF-16, 구분자 `\`)으로 바꿉니다. */
            static wstring toApplicationName( const string& executable )
            {
                string nativePath{ executable };
                StringUtil::replaceChar( nativePath, '/', '\\' );
                return StringUtil::utf8ToUTF16( nativePath.c_str() );
            }

            /** @brief @p path 가 절대 경로(드라이브 문자 + 구분자 · 두 구분자로 시작하는 UNC)이면 true 입니다. */
            static bool isAbsoluteWindowsPath( const wstring& path )
            {
                const auto isSeparator = []( wchar_t character )
                { return character == L'\\' || character == L'/'; };
                const bool bDrive        = path.size() >= 3 && path[1] == L':' && isSeparator( path[2] );
                const bool bNetworkShare = path.size() >= 2 && isSeparator( path[0] ) && isSeparator( path[1] );
                return bDrive || bNetworkShare;
            }

            /**
             * @brief 명령의 실행 파일을 `CreateProcessW` 의 응용 프로그램 이름(전체 경로)으로 정합니다. 이름 명령은 PATH 의 **절대 경로 폴더만** 뒤집니다. 못 찾으면 빈 글입니다.
             * @details 응용 프로그램 이름 없이 명령줄만 주면 `CreateProcessW` 는 실행 파일 폴더 → **작업 폴더** → 시스템 폴더 → PATH 순으로 찾는다 — 프로젝트 폴더에 놓인
             *          `git.exe` 가 PATH 의 git 보다 먼저 돈다(바이너리 심기). Go `os/exec.LookPath`(1.19 부터 작업 폴더 결과를 거절)와 같이 PATH 만 본다.
             *          확장자가 없으면 `.exe` 를 붙인다(`CreateProcessW` 와 같은 규칙).
             */
            static wstring resolveApplicationName( const ProcessCommand& command )
            {
                if ( command._bSearchPath == false )
                    return toApplicationName( command._executable );
                const wstring fileName = StringUtil::utf8ToUTF16( command._executable.c_str() );
                if ( fileName.empty() || fileName.find_first_of( L"\\/" ) != wstring::npos )
                    return {};
                wstring candidate = fileName;
                if ( fileName.find( L'.' ) == wstring::npos )
                    candidate += L".exe";
                string pathValue;
                if ( ProcessEnvironment::tryReadNonEmpty( "PATH", pathValue ) == false )
                    return {};
                const wstring widePath = StringUtil::utf8ToUTF16( pathValue.c_str() );
                size_t        start    = 0;
                while ( start <= widePath.size() )
                {
                    const size_t end       = MathUtil::min( widePath.find( L';', start ), widePath.size() );
                    wstring      directory = widePath.substr( start, end - start );
                    start                  = end + 1;
                    if ( directory.size() >= 2 && directory.front() == L'"' && directory.back() == L'"' )
                        directory = directory.substr( 1, directory.size() - 2 );
                    if ( isAbsoluteWindowsPath( directory ) == false )
                        continue; // 빈 칸 · `.` · 상대 경로는 작업 폴더 기준이다 — 뒤지지 않는다
                    if ( directory.back() != L'\\' && directory.back() != L'/' )
                        directory.push_back( L'\\' );
                    wstring fullPath = std::move( directory );
                    fullPath += candidate;
                    const DWORD attributes = GetFileAttributesW( fullPath.c_str() );
                    if ( attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 )
                        return fullPath;
                }
                return {};
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
                    StringBuilder<constant::buffer::kMax1024> textBuilder;
                    textBuilder.appendFormat( "%#=%#", entry._name, entry._value );
                    listKept.push_back( StringUtil::utf8ToUTF16( textBuilder.c_str() ) );
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
                    SW_LOG_ERROR_ID( "WindowsProcess.CreatePipeFailed", "CreatePipe failed!" );
                    return false;
                }
                SetHandleInformation( outPipes._hOutRead, HANDLE_FLAG_INHERIT, 0 );

                // 표준 입력: 파이프(쓰는 끝은 이 프로세스만) 또는 NUL.
                if ( bPipeStandardInput )
                {
                    if ( CreatePipe( &outPipes._hInRead, &outPipes._hInWrite, &saAttr, 0 ) == FALSE )
                    {
                        outPipes.closeAll();
                        SW_LOG_ERROR_ID( "WindowsProcess.CreatePipeStdinFailed", "CreatePipe (stdin) failed!" );
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

        // POSIX 와 같은 계약 — 도는 자식은 `_releaseMode` 대로 죽이거나 끝날 때까지 기다린 뒤 놓는다(Windows 는 좀비가 없지만 두 플랫폼이 같은 일을 한다).
        if ( _pNativeHandle != nullptr && WaitForSingleObject( static_cast<HANDLE>( _pNativeHandle ), 0 ) == WAIT_TIMEOUT )
        {
            if ( _releaseMode == ProcessReleaseMode::Terminate || _bEndWithParent ) // 부모와 함께 끝낼 자식은 기다리지 않고 트리째 끝낸다 — POSIX 와 같은 순서
                (void)terminate();
            else
                SW_LOG_WARNING_ID( "Process.WaitingOnRelease", "Releasing a running child (pid %#) - waiting for it to exit; call terminate() first or launch with ProcessReleaseMode::Terminate",
                                   _processID.load() );
            (void)WaitForSingleObject( static_cast<HANDLE>( _pNativeHandle ), INFINITE );
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

        // 자식은 위에서 끝났다. Job 핸들을 닫으면 부모와 함께 끝낼 자식(`_bEndWithParent`, KILL_ON_JOB_CLOSE)은 트리째 끝나고, 그 밖의 Job 은 핸들만 닫는다(떼어 띄운 손자는 살린다).
        if ( _pNativeJob != nullptr )
        {
            CloseHandle( static_cast<HANDLE>( _pNativeJob ) );
            _pNativeJob = nullptr;
        }

        _bufferedOutput.clear();
        _processID.store( 0 );
        _bEndWithParent = false;
    }

    bool Process::launch( const ProcessCommand& command, const ProcessOptions& options )
    {
        using Internal = WindowsProcessInternal;
        shutdown();

        if ( command._executable.empty() )
        {
            SW_LOG_ERROR_ID( "Process.NoExecutable", "Process launch needs an executable" );
            return false;
        }

        // 환경 덮어쓰기는 파이프를 열기 전에 만든다 — 이름이 잘못되면 닫을 것이 없는 채로 실패한다.
        vector<utf16> listEnvironmentBlock;
        if ( options._listEnvironment.empty() == false && Internal::makeEnvironmentBlock( options._listEnvironment, listEnvironmentBlock ) == false )
        {
            SW_LOG_ERROR_ID( "Process.InvalidEnvironmentName", "Process launch got an environment entry with an empty name or '=' in the name" );
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
            SW_LOG_ERROR_ID( "WindowsProcess.FailedRestrictInheritedHandles", "Failed to restrict inherited handles for: %#", cmdStr.c_str() );
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
        DWORD      consoleProcessID = 0;
        const bool bShareConsole    = options._bNewProcessGroup && GetConsoleProcessList( &consoleProcessID, 1 ) != 0;
        // 멈춘 채 띄워 Job 에 넣은 뒤 돌린다 — 넣기 전에 돌면 그 사이 띄운 손자가 Job 밖에 남는다.
        const DWORD creationFlags = CREATE_SUSPENDED | ( options._bCreateWindow || bShareConsole ? 0 : CREATE_NO_WINDOW ) | EXTENDED_STARTUPINFO_PRESENT |
                                    ( options._bNewProcessGroup ? CREATE_NEW_PROCESS_GROUP : 0 ) | ( listEnvironmentBlock.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT );

        PROCESS_INFORMATION pi{};
        // 실행 파일은 늘 전체 경로로 넘긴다 — 명령줄만 주면 작업 폴더까지 뒤진다(`resolveApplicationName`). 못 찾았으면 띄우지 않은 것으로 간다.
        const wstring wsApplication = WindowsProcessInternal::resolveApplicationName( command );
        const BOOL    bCreated      = wsApplication.empty() ? FALSE : CreateProcessW( wsApplication.c_str(), wsCmdLine.data(), nullptr, nullptr, TRUE, creationFlags,
                                                                              listEnvironmentBlock.empty() ? nullptr : listEnvironmentBlock.data(), // nullptr 이면 이 프로세스의 환경을 그대로 물려준다
                                                                              options._workingDirectory.empty() ? nullptr : wsBuildDir.c_str(), &startupInfo.StartupInfo, &pi );

        pipes.closeChildEnds();

        if ( bCreated == FALSE )
        {
            pipes.closeAll();
            SW_LOG_ERROR_ID( "Process.LaunchFailed", "Failed to launch command: %#", cmdStr.c_str() );
            return false;
        }

        // 자식 트리를 Job 하나로 묶는다(`terminate` — POSIX 의 프로세스 그룹과 같은 뜻). 묶지 못하면 손자를 멈출 수 없으니 띄우지 않은 것으로 돌린다.
        HANDLE hJob = CreateJobObjectW( nullptr, nullptr );
        // 부모와 함께 끝낼 자식은 Job 의 마지막 핸들이 닫힐 때 트리째 끝낸다 — 이 프로세스가 죽으면 OS 가 핸들을 닫는다(감사 E-15). 걸지 못하면 띄우지 않는다(고아를 남기지 않는다).
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const bool bLimitSet                   = options._bEndWithParent == false || ( hJob != nullptr && SetInformationJobObject( hJob, JobObjectExtendedLimitInformation, &limit, sizeof( limit ) ) != FALSE );
        if ( hJob == nullptr || bLimitSet == false || AssignProcessToJobObject( hJob, pi.hProcess ) == FALSE )
        {
            const DWORD error = GetLastError();
            TerminateProcess( pi.hProcess, 1 );
            CloseHandle( pi.hThread );
            CloseHandle( pi.hProcess );
            if ( hJob != nullptr )
                CloseHandle( hJob );
            pipes.closeAll();
            SW_LOG_ERROR_ID( "WindowsProcess.JobAssignFailed", "Failed to put the child in a job object (error %#): %#", static_cast<uint32>( error ), cmdStr.c_str() );
            return false;
        }
        ResumeThread( pi.hThread );

        _pNativeJob       = hJob;
        _pNativeHandle    = pi.hProcess;
        _pNativeThread    = pi.hThread;
        _pStdOutRead      = pipes._hOutRead;
        _pStdInWrite      = pipes._hInWrite;
        _bNewProcessGroup = options._bNewProcessGroup;
        _releaseMode      = options._releaseMode;
        _bEndWithParent   = options._bEndWithParent;
        _processID.store( static_cast<int32>( pi.dwProcessId ) );

        return true;
    }

    optional<DetachedProcessID> Process::launchDetached( const ProcessCommand& command, const ProcessOptions& options )
    {
        if ( options._bEndWithParent )
        {
            SW_LOG_ERROR_ID( "Process.DetachedEndWithParent", "A detached process cannot end with its parent - launch it attached" );
            return nullopt;
        }
        if ( command._executable.empty() )
        {
            SW_LOG_ERROR_ID( "Process.NoExecutable", "Detached process launch needs an executable" );
            return nullopt;
        }
        if ( options._listEnvironment.empty() == false )
        {
            SW_LOG_ERROR_ID( "Process.DetachedEnvironment", "Detached process launch does not take environment overrides - use Process::launch" );
            return nullopt;
        }
        const string  cmdStr      = command.toCommandLine();
        wstring       wsCmdLine   = StringUtil::utf8ToUTF16( cmdStr.c_str() );
        const wstring wsDirectory = StringUtil::utf8ToUTF16( options._workingDirectory.c_str() );

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof( STARTUPINFOW );

        // 핸들을 하나도 물려주지 않는다(bInheritHandles = FALSE) — 띄운 프로그램이 이 프로세스의 파이프 · 파일을 붙들지 않는다.
        PROCESS_INFORMATION pi{};
        const wstring       wsApplication = WindowsProcessInternal::resolveApplicationName( command );
        const BOOL          bCreated      = wsApplication.empty() ? FALSE : CreateProcessW( wsApplication.c_str(), wsCmdLine.data(), nullptr, nullptr, FALSE, options._bCreateWindow ? 0 : CREATE_NO_WINDOW, nullptr, options._workingDirectory.empty() ? nullptr : wsDirectory.c_str(), &startupInfo, &pi );
        if ( bCreated == FALSE )
        {
            SW_LOG_ERROR_ID( "Process.LaunchDetachedFailed", "Failed to launch detached command: %#", cmdStr.c_str() );
            return nullopt;
        }

        CloseHandle( pi.hThread );
        CloseHandle( pi.hProcess );
        return DetachedProcessID{ static_cast<uint32>( pi.dwProcessId ) };
    }

    bool Process::waitForDetachedExit( DetachedProcessID id, uint32 timeoutMilliseconds )
    {
        HANDLE hProcess = OpenProcess( SYNCHRONIZE, FALSE, static_cast<DWORD>( id._value ) );
        if ( hProcess == nullptr )
            return GetLastError() == ERROR_INVALID_PARAMETER; // 그 id 의 프로세스가 없다 — 이미 끝났다
        const DWORD waited = WaitForSingleObject( hProcess, timeoutMilliseconds );
        CloseHandle( hProcess );
        return waited == WAIT_OBJECT_0;
    }

    bool Process::launchURL( string_view url )
    {
        const WindowsProcessInternal::ScopedShellCom com;
        const wstring                                wideURL = StringUtil::utf8ToUTF16( string( url ).c_str() );
        // 32 이하는 오류 코드다(ShellExecute 의 옛 계약).
        const INT_PTR result = reinterpret_cast<INT_PTR>( ShellExecuteW( nullptr, L"open", wideURL.c_str(), nullptr, nullptr, SW_SHOWNORMAL ) );
        if ( result <= 32 )
        {
            SW_LOG_ERROR_ID( "WindowsProcess.ShellExecuteWFailedURL", "ShellExecuteW failed (%#) for URL: %#", static_cast<int64>( result ), string( url ) );
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
            SW_LOG_ERROR_ID( "WindowsProcess.ILCreateFromPathWFailed", "ILCreateFromPathW failed for: %#", windowsPath );
            return false;
        }
        const HRESULT result = SHOpenFolderAndSelectItems( pItem, 0, nullptr, 0 );
        ILFree( pItem );
        if ( FAILED( result ) )
        {
            SW_LOG_ERROR_ID( "WindowsProcess.SHOpenFolderAndSelectItemsFailedHRESULT", "SHOpenFolderAndSelectItems failed (HRESULT %#) for: %#", static_cast<uint32>( result ), windowsPath );
            return false;
        }
        return true;
    }

    size_t Process::readOutputBytes( utf8* pBuffer, size_t capacity )
    {
        DWORD bytesRead = 0;
        if ( ReadFile( static_cast<HANDLE>( _pStdOutRead ), pBuffer, static_cast<DWORD>( capacity ), &bytesRead, nullptr ) == FALSE )
            return 0; // 쓰는 쪽이 닫혔다(ERROR_BROKEN_PIPE) — 끝
        return static_cast<size_t>( bytesRead );
    }

    expected<int32> Process::waitForExit()
    {
        if ( _pNativeHandle == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "there is no child process to wait for" } );

        if ( WaitForSingleObject( static_cast<HANDLE>( _pNativeHandle ), INFINITE ) != WAIT_OBJECT_0 )
            return unexpected( Error{ ErrorKind::ExternalFailure, "WaitForSingleObject failed on the child process" } );

        DWORD exitCode = 0;
        if ( GetExitCodeProcess( static_cast<HANDLE>( _pNativeHandle ), &exitCode ) == FALSE )
            return unexpected( Error{ ErrorKind::ExternalFailure, "GetExitCodeProcess failed on the child process" } );
        return static_cast<int32>( exitCode );
    }

    bool Process::terminate( int32 exitCode )
    {
        if ( _pNativeJob == nullptr )
            return false;

        return TerminateJobObject( static_cast<HANDLE>( _pNativeJob ), static_cast<UINT>( exitCode ) ) != FALSE;
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

    expected<string> Process::requestDump( string_view dumpPath )
    {
        const int32 processID = _processID.load();
        if ( _pNativeHandle == nullptr || processID <= 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the child process is not running - nothing to dump" } );
        const string path{ dumpPath };
        // 파일 층(`PlatformFileUtil`)은 이 층 위라 OS 에 바로 연다(구분자는 실행 파일 이름과 같은 규칙으로 바꾼다).
        const HANDLE hFile = CreateFileW( WindowsProcessInternal::toApplicationName( path ).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
        if ( hFile == INVALID_HANDLE_VALUE )
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not create the dump file", path } );
        // 남의 프로세스라 스냅샷이 필요 없다(자기 덤프의 로더 교착은 덤프하는 스레드가 덤프되는 프로세스 안에 있을 때의 일이다 — `WindowsCrashHandler`).
        constexpr MINIDUMP_TYPE kDumpType = static_cast<MINIDUMP_TYPE>( MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory );
        const BOOL              bWritten  = MiniDumpWriteDump( static_cast<HANDLE>( _pNativeHandle ), static_cast<DWORD>( processID ), hFile, kDumpType, nullptr, nullptr, nullptr );
        const DWORD             dumpError = bWritten != FALSE ? ERROR_SUCCESS : GetLastError();
        CloseHandle( hFile );
        if ( bWritten == FALSE )
        {
            fixed_string<constant::buffer::kMax1024> detail;
            formatstring( detail.data(), detail.capacity(), "%# (error %#)", path, static_cast<uint32>( dumpError ) );
            return unexpected( Error{ ErrorKind::ExternalFailure, "MiniDumpWriteDump failed on the child process", detail.view() } );
        }
        return path;
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

    void Process::exitImmediately( int32 exitCode )
    {
        TerminateProcess( GetCurrentProcess(), static_cast<UINT>( exitCode ) );
        // TerminateProcess 는 자기 프로세스면 돌아오지 않는다 — 돌아왔으면(거절) 표준 즉시 종료로 끝낸다.
        std::_Exit( exitCode );
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

// ------------------------------------------------------------------------------
// ProcessEnvironment
// ------------------------------------------------------------------------------

namespace sw
{
    namespace
    {
        struct WindowsProcessEnvironmentInternal
        {
            /** @brief 읽는 동안 다른 스레드가 값을 바꿔 길이가 달라지면 다시 읽는 횟수의 상한입니다. */
            static constexpr uint32 kMaxReadAttempts = 4;

            static bool isValidName( string_view name ) { return name.empty() == false && name.find( '=' ) == string_view::npos; }
        };
    } // namespace

    bool ProcessEnvironment::tryRead( string_view name, string& outValue )
    {
        using Internal = WindowsProcessEnvironmentInternal;
        outValue.clear();
        if ( Internal::isValidName( name ) == false )
            return false;
        const wstring wideName = StringUtil::utf8ToUTF16( string{ name }.c_str() );
        for ( uint32 attempt = 0; attempt < Internal::kMaxReadAttempts; ++attempt )
        {
            // 값이 빈 변수도 0 을 돌려준다 — 오류 코드를 먼저 지워 "없음"(ERROR_ENVVAR_NOT_FOUND)과 가른다.
            SetLastError( ERROR_SUCCESS );
            const DWORD required = GetEnvironmentVariableW( wideName.c_str(), nullptr, 0 );
            if ( required == 0 )
                return GetLastError() != ERROR_ENVVAR_NOT_FOUND; // 빈 값이면 true
            vector<utf16> listBuffer( static_cast<size_t>( required ), L'\0' );
            // 빈 값이면 길이는 1(끝의 널)이고 읽은 글자 수는 0 이다 — 0 은 "그 사이 지워짐"(ERROR_ENVVAR_NOT_FOUND)일 때만 실패다.
            SetLastError( ERROR_SUCCESS );
            const DWORD written = GetEnvironmentVariableW( wideName.c_str(), listBuffer.data(), required );
            if ( written == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND )
                return false;
            if ( written >= required )
                continue;                                            // 그 사이 길어졌다 — 다시 길이를 잰다
            outValue = StringUtil::utf16ToUTF8( listBuffer.data() ); // 끝의 널은 API 가 적었다
            return true;
        }
        return false;
    }

    bool ProcessEnvironment::trySet( string_view name, string_view value )
    {
        if ( WindowsProcessEnvironmentInternal::isValidName( name ) == false )
            return false;
        const wstring wideName  = StringUtil::utf8ToUTF16( string{ name }.c_str() );
        const wstring wideValue = StringUtil::utf8ToUTF16( string{ value }.c_str() );
        // OS 환경과 CRT 복사본을 함께 바꾼다 — CRT 의 `getenv` 를 읽는 서드파티 코드(Tracy 클라이언트)는 복사본을 본다.
        // `_wputenv_s` 는 빈 값을 "지우기"로 읽어 옛 값이 복사본에 남지 않게 한다. 마지막에 OS 환경을 최종 값(빈 값 포함)으로 맞춘다.
        const bool bCRTSet = _wputenv_s( wideName.c_str(), wideValue.c_str() ) == 0;
        const bool bOSSet  = SetEnvironmentVariableW( wideName.c_str(), wideValue.c_str() ) != FALSE;
        return bCRTSet && bOSSet;
    }

    bool ProcessEnvironment::tryUnset( string_view name )
    {
        if ( WindowsProcessEnvironmentInternal::isValidName( name ) == false )
            return false;
        const wstring wideName = StringUtil::utf8ToUTF16( string{ name }.c_str() );
        // 없던 변수를 지울 때 `SetEnvironmentVariableW` 는 ERROR_ENVVAR_NOT_FOUND 로 실패한다 — 지워진 상태가 목적이므로 성공으로 본다.
        const bool bOSCleared = SetEnvironmentVariableW( wideName.c_str(), nullptr ) != FALSE || GetLastError() == ERROR_ENVVAR_NOT_FOUND;
        return bOSCleared && _wputenv_s( wideName.c_str(), L"" ) == 0;
    }
} // namespace sw

// ------------------------------------------------------------------------------
// ThreadCrashStack
// ------------------------------------------------------------------------------

namespace sw
{
    namespace
    {
        struct WindowsThreadCrashStackInternal
        {
            /**
             * @brief 넘친 뒤 핸들러가 쓸 스택 영역입니다(`SetThreadStackGuarantee`).
             * @details 스택 오버플로는 스택이 바닥난 채로 필터에 들어옵니다. 영역이 없으면 필터의 첫 호출에서 다시 넘쳐 **아무것도 남지 않습니다**
             *          (실측: 덤프 0 바이트). 미니덤프 · 심볼 변환 · 8 KB 문자열 버퍼가 차례로 들어가는 크기입니다 — 실측으로 64 KB 에서 덤프가 온전합니다.
             */
            static constexpr ULONG kStackGuaranteeBytes = 64 * 1024;
        };
    } // namespace
} // namespace sw

namespace sw
{
    void ThreadCrashStack::initializeCurrentThread()
    {
        ULONG guaranteeBytes = WindowsThreadCrashStackInternal::kStackGuaranteeBytes;
        SetThreadStackGuarantee( &guaranteeBytes );
    }

    void ThreadCrashStack::releaseCurrentThread()
    {
    }
} // namespace sw

// ------------------------------------------------------------------------------
// StandardInputReader
// ------------------------------------------------------------------------------

namespace sw
{
    namespace
    {
        struct WindowsStandardInputReaderInternal
        {
            /** @brief 표준 입력 핸들이 무엇인가 — 기다리는 방법이 종류마다 다르다. */
            enum class InputKind : uint8
            {
                None,    ///< 핸들이 없다(서비스) — 바로 EOF
                Console, ///< 콘솔 — 키 입력을 기다렸다 읽는다
                Pipe,    ///< 파이프 — 쌓인 바이트를 들여다보고 읽는다(읽기가 막히면 멈춤 요청을 못 본다)
                File,    ///< 파일 · NUL — 끝까지 읽는다
            };

            static InputKind classify( HANDLE hInput )
            {
                if ( hInput == nullptr || hInput == INVALID_HANDLE_VALUE )
                    return InputKind::None;
                const DWORD fileType = GetFileType( hInput );
                DWORD       mode     = 0;
                if ( fileType == FILE_TYPE_CHAR && GetConsoleMode( hInput, &mode ) != FALSE )
                    return InputKind::Console;
                if ( fileType == FILE_TYPE_PIPE )
                    return InputKind::Pipe;
                return InputKind::File;
            }
        };
    } // namespace

    bool StandardInputReader::readLinePlatform( const StopToken& stopToken, string& outLine )
    {
        const HANDLE                                        hInput = GetStdHandle( STD_INPUT_HANDLE );
        const WindowsStandardInputReaderInternal::InputKind kind   = WindowsStandardInputReaderInternal::classify( hInput );
        for ( ;; )
        {
            if ( takeLine( outLine ) )
                return true;
            if ( stopToken.isStopRequested() )
                return false;
            switch ( kind )
            {
                case WindowsStandardInputReaderInternal::InputKind::None:
                {
                    _endOfInput.store( 1 );
                    return false;
                }
                case WindowsStandardInputReaderInternal::InputKind::Console:
                {
                    // 입력 레코드가 오면 신호가 선다. 키 누름이 아닌 레코드(포커스 · 마우스 · 창 크기)는 버리고, 키가 있을 때만 줄 읽기로 간다 —
                    // ReadConsoleW 는 Enter 까지 막히므로, 키를 친 뒤의 멈춤은 그 줄이 끝날 때까지 늦어진다(서버 종료는 정상 종료 요청이 먼저 온다).
                    if ( WaitForSingleObject( hInput, 100 ) != WAIT_OBJECT_0 )
                        continue;
                    INPUT_RECORD record{};
                    DWORD        peeked = 0;
                    if ( PeekConsoleInputW( hInput, &record, 1, &peeked ) == FALSE || peeked == 0 )
                        continue;
                    if ( record.EventType != KEY_EVENT || record.Event.KeyEvent.bKeyDown == FALSE )
                    {
                        (void)ReadConsoleInputW( hInput, &record, 1, &peeked );
                        continue;
                    }
                    utf16 arrBuffer[256];
                    DWORD readCount = 0;
                    if ( ReadConsoleW( hInput, arrBuffer, SW_COUNT_OF( arrBuffer ) - 1, &readCount, nullptr ) == FALSE )
                    {
                        _endOfInput.store( 1 );
                        return false;
                    }
                    arrBuffer[readCount] = L'\0';
                    _partialLine += StringUtil::utf16ToUTF8( arrBuffer );
                    break;
                }
                case WindowsStandardInputReaderInternal::InputKind::Pipe:
                {
                    DWORD available = 0;
                    if ( PeekNamedPipe( hInput, nullptr, 0, nullptr, &available, nullptr ) == FALSE )
                    {
                        _endOfInput.store( 1 ); // 쓰는 쪽이 닫았다(ERROR_BROKEN_PIPE)
                        return false;
                    }
                    if ( available == 0 )
                    {
                        Sleep( 50 );
                        continue;
                    }
                    utf8  arrBuffer[512];
                    DWORD readCount = 0;
                    if ( ReadFile( hInput, arrBuffer, available < sizeof( arrBuffer ) ? available : static_cast<DWORD>( sizeof( arrBuffer ) ), &readCount, nullptr ) == FALSE ||
                         readCount == 0 )
                    {
                        _endOfInput.store( 1 );
                        return false;
                    }
                    _partialLine.append( arrBuffer, readCount );
                    break;
                }
                case WindowsStandardInputReaderInternal::InputKind::File:
                {
                    utf8  arrBuffer[512];
                    DWORD readCount = 0;
                    if ( ReadFile( hInput, arrBuffer, sizeof( arrBuffer ), &readCount, nullptr ) == FALSE || readCount == 0 )
                    {
                        // 끝에 줄바꿈 없는 마지막 줄도 명령이다.
                        if ( _partialLine.empty() == false )
                            _partialLine += '\n';
                        if ( takeLine( outLine ) )
                            return true;
                        _endOfInput.store( 1 );
                        return false;
                    }
                    _partialLine.append( arrBuffer, readCount );
                    break;
                }
            }
        }
    }
} // namespace sw

// ------------------------------------------------------------------------------
// ShutdownSignal
// ------------------------------------------------------------------------------

namespace sw
{
    namespace
    {
        struct WindowsShutdownSignalInternal
        {
            /** @brief 창 닫기에 OS 가 주는 시간(약 5 초)보다 짧게 기다린다 — 넘기면 OS 가 정리 도중에 프로세스를 끝낸다. */
            static constexpr DWORD kCloseWaitMilliseconds = 4500;

            static inline atomic<bool>   s_bInstalled{ false };
            static inline atomic<uint32> s_serviceMode{ 0 };
            static inline atomic<uint32> s_interruptCount{ 0 };

            /** @brief 종료 완료 사건 — 첫 호출이 만든다. 함수 안 정적 변수라 여러 스레드가 동시에 처음 불러도 한 번만 만든다(실패하면 nullptr). */
            static HANDLE completeEvent()
            {
                static const HANDLE s_hComplete = CreateEventW( nullptr, TRUE, FALSE, nullptr );
                return s_hComplete;
            }

            static void waitForShutdownComplete()
            {
                const HANDLE hComplete = completeEvent();
                if ( hComplete != nullptr )
                    (void)WaitForSingleObject( hComplete, kCloseWaitMilliseconds );
            }

            /** @brief 콘솔 제어 처리기 — OS 가 새 스레드에서 부른다. TRUE 를 돌려주면 다음 처리기(기본 = 즉시 종료)로 넘기지 않는다. */
            static BOOL WINAPI onConsoleControl( DWORD controlType )
            {
                const bool                 bInterrupt = controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT;
                const uint32               earlier    = bInterrupt ? s_interruptCount.fetch_add( 1, std::memory_order_relaxed ) : 0u;
                const ShutdownSignalAction action =
                    ShutdownSignal::decideConsoleControl( static_cast<uint32>( controlType ), earlier, s_serviceMode.load( std::memory_order_relaxed ) != 0 );
                if ( action._bPassToDefault || action._cause == ShutdownCause::None )
                    return FALSE;
                ShutdownSignal::request( action._cause );
                if ( action._bWaitForCompletion )
                    waitForShutdownComplete();
                return TRUE;
            }
        };
    } // namespace

    bool ShutdownSignal::install()
    {
        // 교환으로 "내가 설치할 차례" 를 한 스레드만 얻는다 — 두 스레드가 동시에 부르면 같은 처리기가 두 번 걸려 첫 Ctrl+C 가 둘째 호출로 세어진다.
        if ( WindowsShutdownSignalInternal::s_bInstalled.exchange( true, std::memory_order_acq_rel ) )
            return true;
        const bool bInstalled = SetConsoleCtrlHandler( &WindowsShutdownSignalInternal::onConsoleControl, TRUE ) != FALSE;
        if ( bInstalled == false )
            WindowsShutdownSignalInternal::s_bInstalled.store( false, std::memory_order_release );
        return bInstalled && WindowsShutdownSignalInternal::completeEvent() != nullptr;
    }

    void ShutdownSignal::uninstall()
    {
        if ( WindowsShutdownSignalInternal::s_bInstalled.exchange( false, std::memory_order_acq_rel ) )
            (void)SetConsoleCtrlHandler( &WindowsShutdownSignalInternal::onConsoleControl, FALSE );
        // 사건 핸들은 프로세스 끝까지 둔다 — 처리기 스레드가 아직 기다리고 있을 수 있다.
    }

    void ShutdownSignal::notifyShutdownComplete()
    {
        const HANDLE hComplete = WindowsShutdownSignalInternal::completeEvent();
        if ( hComplete != nullptr )
            (void)SetEvent( hComplete );
    }

    void ShutdownSignal::setServiceMode( bool bServiceMode )
    {
        WindowsShutdownSignalInternal::s_serviceMode.store( bServiceMode ? 1u : 0u, std::memory_order_relaxed );
    }

    ShutdownSignalAction ShutdownSignal::decideConsoleControl( uint32 controlType, uint32 earlierInterruptCount, bool bServiceMode )
    {
        switch ( controlType )
        {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
            {
                if ( earlierInterruptCount >= 1 )
                    return ShutdownSignalAction{ ShutdownCause::None, true, false };
                return ShutdownSignalAction{ ShutdownCause::Interrupt, false, false };
            }
            case CTRL_CLOSE_EVENT:
            {
                return ShutdownSignalAction{ ShutdownCause::ConsoleClose, false, true };
            }
            case CTRL_LOGOFF_EVENT:
            {
                if ( bServiceMode )
                    return ShutdownSignalAction{ ShutdownCause::None, true, false }; // 서비스는 사용자가 로그오프해도 돈다
                return ShutdownSignalAction{ ShutdownCause::SystemShutdown, false, true };
            }
            case CTRL_SHUTDOWN_EVENT:
            {
                return ShutdownSignalAction{ ShutdownCause::SystemShutdown, false, true };
            }
            default:
            {
                return ShutdownSignalAction{ ShutdownCause::None, true, false };
            }
        }
    }
} // namespace sw

#endif
