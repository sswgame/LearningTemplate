#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Log/Logger.h"
#include "Core/Process/ThreadCrashStack.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <DbgHelp.h>
    #include <ProcessSnapshot.h>
    #include <csignal>

SW_LOG_CALLER( "WindowsCrashHandler" );
namespace sw
{
    namespace
    {
        atomic<bool> s_bInstalled{ false };
        /**
         * @brief 지금 리포트를 쓰는 스레드의 ID 입니다(0 이면 아무도 쓰지 않는다).
         * @details bool 로 두면 두 스레드가 거의 동시에 죽을 때 두 번째 스레드가 "이미 보고 중" 을 보고 곧장 필터를 빠져나가 프로세스를
         *          끝냅니다 — 첫 스레드가 덤프를 쓰는 도중에. 같은 스레드가 보고하다 또 죽은 경우와 다른 스레드가 죽은 경우를 가르려고 ID 를 둡니다.
         */
        atomic<uint32> s_reportingThreadId{ 0 };

        LPTOP_LEVEL_EXCEPTION_FILTER s_pPreviousFilter{ nullptr };
        _crt_signal_t                s_pPreviousAbortHandler{ SIG_DFL };
        _purecall_handler            s_pPreviousPureCallHandler{ nullptr };
        _invalid_parameter_handler   s_pPreviousInvalidParameterHandler{ nullptr };
        uint32                       s_previousAbortBehavior{ 0 };

        /**
         * @brief CRT 가 예외 필터를 건너뛰고 끝내는 길을 우리 필터로 돌리는 사용자 예외 코드입니다(0xE0 = 사용자 정의 · 치명).
         * @details abort() · 순수 가상 호출 · 잘못된 CRT 인자는 CRT 가 `__fastfail` 로 프로세스를 끝냅니다. `__fastfail` 은 **예외 필터를 태우지
         *          않아서**, 훅이 없으면 이 셋으로 끝날 때 덤프도 스택도 남지 않습니다(종료 코드 0xC0000409, 필터 미진입). 엔진의 치명 경로가
         *          "로그 + std::abort" 라 가장 흔한 죽음입니다. 훅에서 이 코드로 예외를 올려 접근 위반과 같은 길을 태웁니다.
         */
        constexpr DWORD kExceptionCodeAbort            = 0xE0535701;
        constexpr DWORD kExceptionCodePureCall         = 0xE0535702;
        constexpr DWORD kExceptionCodeInvalidParameter = 0xE0535703;

        /**
         * @brief 크래시 보고를 맡는 스레드 — 설치할 때 미리 띄워 두고 신호를 기다린다.
         * @details **죽은 스레드에서 덤프를 쓰지 않는다.** 스택 오버플로는 넘친 스택의 보증 자리(64 KB)만으로 필터를 돈다 — 그 위에서
         *          `MiniDumpWriteDump` · 심볼 변환을 하면 크래시 자식이 **간헐로 멈춘다**(하나씩은 드물고, 여럿을 겹쳐 돌리면 수백 번에
         *          몇 번). 브레이크패드와 언리얼(`FCrashReportingThread`)이 같은 이유로 보고를 미리 띄운 스레드에서 한다 — 그 스레드는
         *          멀쩡한 스택을 갖고 있다.
         *          그리고 폴트 스레드는 **시한까지만**(`CrashHandler::setReportDeadline`) 기다린다: 보고가 어디서 멈추든(힙 · stdio 락)
         *          프로세스는 끝난다. 크래시 난 게임이 창을 띄운 채 서 있는 것보다 보고 없이 끝나는 편이 낫다.
         */
        HANDLE       s_hReportThread{ nullptr };
        HANDLE       s_hReportRequested{ nullptr };
        HANDLE       s_hReportFinished{ nullptr };
        DWORD        s_reportThreadId{ 0 };
        atomic<bool> s_bStopReportThread{ false };

        /** @brief 보고 스레드에 넘기는 크래시 하나(폴트 스레드가 시한까지 기다리는 동안만 유효하다 — 그 스레드의 스택을 가리킨다). */
        struct PendingCrash
        {
            const utf8*         _pReason{ nullptr };
            const void*         _pFaultAddress{ nullptr };
            void*               _pPlatformContext{ nullptr };
            EXCEPTION_POINTERS* _pExceptionInfo{ nullptr };
            DWORD               _faultThreadId{ 0 };
        };
        PendingCrash s_pendingCrash{};

        /**
         * @brief 크래시 순간에 부를 `MiniDumpWriteDump` — **설치할 때** 풀어 둔다.
         * @details dbghelp 의 `MiniDumpWriteDump` 는 구현(dbgcore.dll)을 **처음 부를 때** `LoadLibrary` 로 싣는다. 크래시 순간의 일을 하나
         *          덜려고 그 적재를 설치 시점으로 당긴다. 덤프 구현은 안에서 또 `LoadLibraryExA` 를 부르므로 이것만으로 로더 대기가
         *          사라지지는 않는다 — 그 대기가 영영 풀리지 않는 원인(세워진 스레드)은 kSnapshotFlags 가 없앤다.
         */
        using PfnMiniDumpWriteDump = BOOL( WINAPI* )( HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                                      PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION );
        HMODULE              s_hDumpModule{ nullptr };
        PfnMiniDumpWriteDump s_pfnMiniDumpWriteDump{ nullptr };

        /** @brief 남길 덤프 종류 — 언리얼의 기본값과 비슷한 수준(스택 + 간접 참조 메모리 + 스레드 정보). */
        constexpr MINIDUMP_TYPE kMiniDumpType = static_cast<MINIDUMP_TYPE>( MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs |
                                                                            MiniDumpWithThreadInfo | MiniDumpWithHandleData );

        /**
         * @brief 덤프할 프로세스 스냅샷(`PssCaptureSnapshot`)에 담을 것 — 주소 공간 복제 · 스레드 컨텍스트 · 핸들.
         * @details **살아 있는 프로세스를 덤프하면 `MiniDumpWriteDump` 가 다른 스레드를 모두 세운다.** 세운 스레드 하나가 막 시작하던
         *          참이라(`LdrInitializeThunk`) 로더를 쥐고 있으면, 덤프가 안에서 부르는 `LoadLibraryExA` 가 그 스레드를 영영 기다린다 —
         *          dbgcore 를 미리 싣거나 같은 종류의 덤프를 설치 때 한 번 돌려 두어도 그 호출은 막지 못한다.
         *          세워진 스레드에는 시한을 기다리던 폴트 스레드도 있어서, **시한도 듣지 않는다**. 스냅샷은
         *          커널이 스레드 컨텍스트를 잠깐 떠 두고 주소 공간을 복제한 것이라, 덤프는 복제본을 읽고 살아 있는 스레드는 세우지 않는다.
         *          로더를 쥔 스레드는 제 일을 마치고 놓는다. 마이크로소프트가 자기 프로세스를 덤프할 때 권하는 길이다.
         */
        constexpr PSS_CAPTURE_FLAGS kSnapshotFlags = static_cast<PSS_CAPTURE_FLAGS>(
            PSS_CAPTURE_VA_CLONE | PSS_CAPTURE_HANDLES | PSS_CAPTURE_HANDLE_NAME_INFORMATION | PSS_CAPTURE_HANDLE_BASIC_INFORMATION |
            PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION | PSS_CAPTURE_THREADS | PSS_CAPTURE_THREAD_CONTEXT | PSS_CAPTURE_THREAD_CONTEXT_EXTENDED |
            PSS_CREATE_BREAKAWAY | PSS_CREATE_BREAKAWAY_OPTIONAL | PSS_CREATE_USE_VM_ALLOCATIONS | PSS_CREATE_RELEASE_SECTION );

        /** @brief 넘긴 핸들이 스냅샷이라고 덤프에 알린다(`IsProcessSnapshotCallback` 에 S_FALSE). 나머지 질문은 기본값 그대로. */
        BOOL CALLBACK onSnapshotDumpCallbackInternal( PVOID, const PMINIDUMP_CALLBACK_INPUT pInput, PMINIDUMP_CALLBACK_OUTPUT pOutput )
        {
            if ( pInput != nullptr && pOutput != nullptr && pInput->CallbackType == IsProcessSnapshotCallback )
                pOutput->Status = S_FALSE;
            return TRUE;
        }

        void __cdecl onAbortSignalInternal( int32 )
        {
            RaiseException( kExceptionCodeAbort, EXCEPTION_NONCONTINUABLE, 0, nullptr );
        }

        void __cdecl onPureCallInternal()
        {
            RaiseException( kExceptionCodePureCall, EXCEPTION_NONCONTINUABLE, 0, nullptr );
        }

        void __cdecl onInvalidParameterInternal( const utf16*, const utf16*, const utf16*, uint32, uintptr_t )
        {
            RaiseException( kExceptionCodeInvalidParameter, EXCEPTION_NONCONTINUABLE, 0, nullptr );
        }

        /**
         * @brief 미니덤프를 씁니다.
         * @details 최적화된 배포 빌드는 인라인과 꼬리 호출로 프레임이 합쳐지고 지역 변수도 남지 않습니다. 텍스트 콜 스택만으로는
         *          원인을 좁히기 어렵습니다. 덤프가 있어야 디버거로 그 순간을 열어 볼 수 있습니다. 덤프 종류는 언리얼의 기본값과
         *          비슷한 수준(스택 + 간접 참조 메모리 + 스레드 정보)으로 고릅니다. Full 덤프는 수백 MB 가 되어 사용자가 보내 주지
         *          못합니다.
         */
        [[nodiscard]] bool writeMiniDump( EXCEPTION_POINTERS* pInfo, DWORD faultThreadId )
        {
            utf8 arrPath[constant::kMaxBuffer1024]{};
            buildCrashReportPath( arrPath, constant::kMaxBuffer1024, "dmp" );

            // 경로는 UTF-8 이다 — 힙 없이 스택에서 UTF-16 으로 바꿔 W 판에 넘긴다.
            utf16 arrWidePath[constant::kMaxBuffer1024]{};
            if ( MultiByteToWideChar( CP_UTF8, 0, arrPath, -1, arrWidePath, static_cast<int32>( constant::kMaxBuffer1024 ) ) == 0 )
                return false;
            const HANDLE hFile = CreateFileW( arrWidePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
            if ( hFile == INVALID_HANDLE_VALUE )
                return false;

            MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
            exceptionInfo.ThreadId          = faultThreadId;
            exceptionInfo.ExceptionPointers = pInfo;
            exceptionInfo.ClientPointers    = FALSE;

            const PfnMiniDumpWriteDump      pfnWrite   = ( s_pfnMiniDumpWriteDump != nullptr ) ? s_pfnMiniDumpWriteDump : &MiniDumpWriteDump;
            PMINIDUMP_EXCEPTION_INFORMATION pException = ( pInfo != nullptr ) ? &exceptionInfo : nullptr;

            // 살아 있는 프로세스가 아니라 **스냅샷**을 덤프한다(kSnapshotFlags 설명). 스냅샷을 못 뜨면 살아 있는 프로세스를.
            BOOL       bWritten = FALSE;
            HPSS       hSnapshot{ nullptr };
            const bool bSnapshot = PssCaptureSnapshot( GetCurrentProcess(), kSnapshotFlags, CONTEXT_ALL, &hSnapshot ) == ERROR_SUCCESS;
            if ( bSnapshot )
            {
                MINIDUMP_CALLBACK_INFORMATION callbackInfo{};
                callbackInfo.CallbackRoutine = &onSnapshotDumpCallbackInternal;
                bWritten                     = pfnWrite( static_cast<HANDLE>( hSnapshot ), GetCurrentProcessId(), hFile, kMiniDumpType, pException, nullptr, &callbackInfo );
                PssFreeSnapshot( GetCurrentProcess(), hSnapshot );
            }
            else
            {
                bWritten = pfnWrite( GetCurrentProcess(), GetCurrentProcessId(), hFile, kMiniDumpType, pException, nullptr, nullptr );
            }
            CloseHandle( hFile );
            return bWritten != FALSE;
        }

        /**
         * @brief 덤프 · 컨텍스트 · 리포트를 씁니다(보고 스레드에서, 또는 그것이 없으면 폴트 스레드에서).
         * @param hFaultThread 스택을 따라갈 폴트 스레드. nullptr 이면 지금 스레드.
         */
        void writeReportInternal( const PendingCrash& crash, HANDLE hFaultThread )
        {
            // 덤프와 컨텍스트를 **먼저** 쓴다. 이 순서는 취향이 아니라 안전장치다.
            //
            // 크래시 지점에서 안전한 것과 그렇지 않은 것:
            //  - formatstring : 호출하는 쪽의 버퍼에 쓰고 할당하지 않는다(noexcept). 안전하다.
            //  - fixed_string : 크기가 고정이라 안전하다. 컨텍스트 값을 여기에 미리 담아 두는 이유다.
            //  - StringBuilder: 스택 버퍼로 시작하지만 **넘치면 힙으로 늘어난다.** 스택이 깊으면 넘친다.
            //  - symbolize()  : sw::string 을 값으로 반환하므로 **반드시 할당**한다. 가장 위험하다.
            //
            // 힙이 이미 깨져서 죽은 경우라면 아래 심볼 변환에서 다시 죽을 수 있다. 그래서 할당이 없는 덤프 · 컨텍스트를 먼저
            // 확보한다. 심볼 변환이 실패해도 덤프는 남고, 덤프만 있어도 디버거로 그 순간을 열 수 있다.
            // **이 두 줄을 아래로 옮기지 말 것.**
            const bool bMiniDumpWritten = writeMiniDump( crash._pExceptionInfo, crash._faultThreadId );
            writeCrashContextFile( crash._pReason, crash._pFaultAddress, GetCurrentProcessId(), crash._faultThreadId );

            // 본문은 세 플랫폼이 함께 쓴다(CrashContext.cpp).
            writeCrashReport( crash._pReason, crash._pFaultAddress, crash._pPlatformContext, bMiniDumpWritten, hFaultThread );
        }

        /** @brief 보고 스레드 — 신호가 오면 그 크래시를 쓰고 끝났다고 알린다. */
        DWORD WINAPI reportThreadMainInternal( LPVOID )
        {
            while ( true )
            {
                WaitForSingleObject( s_hReportRequested, INFINITE );
                if ( s_bStopReportThread.load() )
                    return 0;

                const HANDLE hFaultThread = OpenThread( THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME, FALSE,
                                                        s_pendingCrash._faultThreadId );
                writeReportInternal( s_pendingCrash, hFaultThread );
                if ( hFaultThread != nullptr )
                    CloseHandle( hFaultThread );
                SetEvent( s_hReportFinished );
            }
        }

        /**
         * @brief (폴트 스레드에서) 크래시를 보고 스레드에 맡기고 시한까지 기다립니다.
         * @param pPlatformContext 있으면 그 지점부터 스택을 따라갑니다(Windows 는 CONTEXT*). nullptr 이면 현재 스레드의 스택을
         *                         캡처합니다.
         */
        void reportCrash( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext, EXCEPTION_POINTERS* pExceptionInfo )
        {
            // 보고 스레드 자신이 보고하다 죽었다 — 더 할 수 있는 것이 없다. 폴트 스레드는 시한이 지나면 스스로 끝난다.
            if ( s_reportThreadId != 0 && GetCurrentThreadId() == s_reportThreadId )
                return;

            const uint32 selfThreadId = static_cast<uint32>( GetCurrentThreadId() );
            uint32       expectedId   = 0;
            if ( s_reportingThreadId.compare_exchange_strong( expectedId, selfThreadId ) == false )
            {
                // 같은 스레드가 보고하다 또 죽었다 — 더 할 수 있는 것이 없다.
                if ( expectedId == selfThreadId )
                    return;
                // 다른 스레드가 보고 중이다. 그 스레드가 덤프를 다 쓰고 프로세스를 끝낼 때까지 기다린다(보고가 멈춘 경우를 위해 상한을 둔다).
                for ( uint32 waitIndex = 0; waitIndex < 3000 && s_reportingThreadId.load() != 0; ++waitIndex )
                {
                    Sleep( 10 );
                }
                return;
            }

            PendingCrash crash{};
            crash._pReason          = pReason;
            crash._pFaultAddress    = pFaultAddress;
            crash._pPlatformContext = pPlatformContext;
            crash._pExceptionInfo   = pExceptionInfo;
            crash._faultThreadId    = GetCurrentThreadId();

            if ( s_hReportThread != nullptr )
            {
                // 보고 스레드에 맡기고 시한까지만 기다린다(이 스레드의 스택 · 예외 정보는 그동안 그대로다).
                s_pendingCrash = crash;
                SetEvent( s_hReportRequested );
                if ( WaitForSingleObject( s_hReportFinished, getCrashReportDeadlineSeconds() * 1000 ) != WAIT_OBJECT_0 )
                {
                    // **stdio 를 거치지 않는다** — 보고가 막힌 자리가 바로 stderr 의 락일 수 있다(CrashTestKind::StderrHeld). fputs 로
                    // 쓰면 시한이 지나도 여기서 다시 막힌다. 그리고 이전 필터에 넘기지 않고 여기서 끝낸다 — 그 필터도
                    // 같은 락에서 막힐 수 있고, 시한은 "여기서 끝난다" 는 약속이어야 한다.
                    constexpr utf8 kMessage[] = "\n[CrashHandler] crash report did not finish in time - exiting without it\n";
                    DWORD          written    = 0;
                    WriteFile( GetStdHandle( STD_ERROR_HANDLE ), kMessage, sizeof( kMessage ) - 1, &written, nullptr );
                    const UINT exitCode = ( pExceptionInfo != nullptr && pExceptionInfo->ExceptionRecord != nullptr )
                                            ? static_cast<UINT>( pExceptionInfo->ExceptionRecord->ExceptionCode )
                                            : 1u;
                    TerminateProcess( GetCurrentProcess(), exitCode );
                }
            }
            else
            {
                // 보고 스레드를 띄우지 못했다 — 이 자리에서 쓴다.
                writeReportInternal( crash, nullptr );
            }

            s_reportingThreadId.store( 0 );
        }

        /** @brief 예외 코드를 사람이 읽을 수 있는 이름으로 바꿉니다. */
        const utf8* exceptionCodeName( DWORD code )
        {
            switch ( code )
            {
                case EXCEPTION_ACCESS_VIOLATION:
                    return "EXCEPTION_ACCESS_VIOLATION (segfault)";
                case EXCEPTION_STACK_OVERFLOW:
                    return "EXCEPTION_STACK_OVERFLOW";
                case EXCEPTION_ILLEGAL_INSTRUCTION:
                    return "EXCEPTION_ILLEGAL_INSTRUCTION";
                case EXCEPTION_INT_DIVIDE_BY_ZERO:
                    return "EXCEPTION_INT_DIVIDE_BY_ZERO";
                case EXCEPTION_FLT_DIVIDE_BY_ZERO:
                    return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
                case EXCEPTION_DATATYPE_MISALIGNMENT:
                    return "EXCEPTION_DATATYPE_MISALIGNMENT";
                case EXCEPTION_IN_PAGE_ERROR:
                    return "EXCEPTION_IN_PAGE_ERROR";
                case EXCEPTION_PRIV_INSTRUCTION:
                    return "EXCEPTION_PRIV_INSTRUCTION";
                case kExceptionCodeAbort:
                    return "std::abort() called";
                case kExceptionCodePureCall:
                    return "pure virtual function call";
                case kExceptionCodeInvalidParameter:
                    return "invalid parameter passed to a CRT function";
                default:
                    return "unhandled SEH exception";
            }
        }

        LONG WINAPI onUnhandledException( EXCEPTION_POINTERS* pInfo )
        {
            const void* pFaultAddress = nullptr;
            const utf8* pReason       = "unhandled SEH exception";
            if ( pInfo != nullptr && pInfo->ExceptionRecord != nullptr )
            {
                pReason       = exceptionCodeName( pInfo->ExceptionRecord->ExceptionCode );
                pFaultAddress = pInfo->ExceptionRecord->ExceptionAddress;
            }
            reportCrash( pReason, pFaultAddress, ( pInfo != nullptr ) ? pInfo->ContextRecord : nullptr, pInfo );

            if ( s_pPreviousFilter != nullptr )
                return s_pPreviousFilter( pInfo );
            return EXCEPTION_EXECUTE_HANDLER;
        }
    } // namespace

    void CrashHandler::initialize()
    {
        if ( s_bInstalled.exchange( true ) )
            return;

        CallStackCapture::initialize();
        ThreadCrashStack::initializeCurrentThread();

        // 덤프 구현을 지금 싣는다(s_pfnMiniDumpWriteDump 설명). dbgcore 가 없는 옛 Windows 는 dbghelp 의 것을 쓴다.
        s_hDumpModule = LoadLibraryW( L"dbgcore.dll" );
        if ( s_hDumpModule != nullptr )
            s_pfnMiniDumpWriteDump = reinterpret_cast<PfnMiniDumpWriteDump>( GetProcAddress( s_hDumpModule, "MiniDumpWriteDump" ) );

        // 보고 스레드는 **크래시 전에** 띄워 둔다 — 크래시 순간에 스레드를 만들면 로더 락 · 힙을 그 자리에서 쓴다.
        s_bStopReportThread.store( false );
        s_hReportRequested = CreateEventW( nullptr, FALSE, FALSE, nullptr );
        s_hReportFinished  = CreateEventW( nullptr, FALSE, FALSE, nullptr );
        if ( s_hReportRequested != nullptr && s_hReportFinished != nullptr )
            s_hReportThread = CreateThread( nullptr, 1024 * 1024, &reportThreadMainInternal, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, &s_reportThreadId );
        if ( s_hReportThread == nullptr )
            SW_LOG_WARNING( "Crash report thread could not be created - reports will be written on the faulting thread." );

        s_pPreviousFilter = SetUnhandledExceptionFilter( &onUnhandledException );
        // CRT 가 필터를 건너뛰는 길 셋을 우리 필터로 돌린다(kExceptionCodeAbort 설명). abort 메시지 창도 끈다 — 리포트는 우리가 쓴다.
        s_pPreviousAbortHandler            = std::signal( SIGABRT, &onAbortSignalInternal );
        s_pPreviousPureCallHandler         = _set_purecall_handler( &onPureCallInternal );
        s_pPreviousInvalidParameterHandler = _set_invalid_parameter_handler( &onInvalidParameterInternal );
        s_previousAbortBehavior            = _set_abort_behavior( 0, _WRITE_ABORT_MSG );
        SW_LOG_TRACE( "Crash handler installed." );
    }

    void CrashHandler::shutdown()
    {
        if ( s_bInstalled.exchange( false ) == false )
            return;

        SetUnhandledExceptionFilter( s_pPreviousFilter );
        s_pPreviousFilter = nullptr;
        std::signal( SIGABRT, s_pPreviousAbortHandler );
        _set_purecall_handler( s_pPreviousPureCallHandler );
        _set_invalid_parameter_handler( s_pPreviousInvalidParameterHandler );
        _set_abort_behavior( s_previousAbortBehavior & _WRITE_ABORT_MSG, _WRITE_ABORT_MSG );

        if ( s_hReportThread != nullptr )
        {
            s_bStopReportThread.store( true );
            SetEvent( s_hReportRequested );
            WaitForSingleObject( s_hReportThread, 2000 );
            CloseHandle( s_hReportThread );
            s_hReportThread  = nullptr;
            s_reportThreadId = 0;
        }
        for ( HANDLE* pEvent : { &s_hReportRequested, &s_hReportFinished } )
        {
            if ( *pEvent != nullptr )
            {
                CloseHandle( *pEvent );
                *pEvent = nullptr;
            }
        }

        s_pfnMiniDumpWriteDump = nullptr;
        if ( s_hDumpModule != nullptr )
        {
            FreeLibrary( s_hDumpModule );
            s_hDumpModule = nullptr;
        }

        // initialize 에서 잡은 심볼 참조를 돌려준다(참조 카운트의 짝 맞추기).
        CallStackCapture::shutdown();
    }
} // namespace sw

#endif
