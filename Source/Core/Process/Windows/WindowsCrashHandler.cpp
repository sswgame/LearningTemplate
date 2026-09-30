#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/Process/CrashContext.h"
#include "Core/Process/CrashHandler.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <DbgHelp.h>
    #include <csignal>

SW_LOG_CALLER( "WindowsCrashHandler" );
namespace sw
{
    namespace
    {
        atomic<bool> s_bInstalled{ false };
        /**
         * @brief 지금 리포트를 쓰는 스레드의 ID 입니다(0 이면 아무도 쓰지 않는다).
         * @details 예전에는 bool 이라, 두 스레드가 거의 동시에 죽으면 두 번째 스레드가 "이미 보고 중" 을 보고 곧장 필터를 빠져나가 프로세스를
         *          끝냈습니다 — 첫 스레드가 덤프를 쓰는 도중에. 같은 스레드가 보고하다 또 죽은 경우와 다른 스레드가 죽은 경우를 가르려고 ID 를 둡니다.
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
         *          않아서**, 예전에는 이 셋으로 끝나면 덤프도 스택도 남지 않았습니다(실측: 종료 코드 0xC0000409, 필터 미진입). 엔진의 치명 경로가
         *          "로그 + std::abort" 라 가장 흔한 죽음이 가장 기록이 없었습니다. 훅에서 이 코드로 예외를 올려 접근 위반과 같은 길을 태웁니다.
         */
        constexpr DWORD kExceptionCodeAbort            = 0xE0535701;
        constexpr DWORD kExceptionCodePureCall         = 0xE0535702;
        constexpr DWORD kExceptionCodeInvalidParameter = 0xE0535703;

        /**
         * @brief 넘친 뒤 핸들러가 쓸 스택 자리입니다(`SetThreadStackGuarantee`).
         * @details 스택 오버플로는 스택이 바닥난 채로 필터에 들어옵니다. 자리가 없으면 필터의 첫 호출에서 다시 넘쳐 **아무것도 남지 않습니다**
         *          (실측: 덤프 0 바이트). 미니덤프 · 심볼 변환 · 8 KB 문자열 버퍼가 차례로 들어가는 크기입니다 — 실측으로 64 KB 에서 덤프가 온전합니다.
         */
        constexpr ULONG kStackGuaranteeBytes = 64 * 1024;

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
        bool writeMiniDump( EXCEPTION_POINTERS* pInfo )
        {
            utf8 arrPath[constant::kMaxBuffer1024]{};
            buildCrashReportPath( arrPath, constant::kMaxBuffer1024, "dmp" );

            const HANDLE hFile = CreateFileA( arrPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
            if ( hFile == INVALID_HANDLE_VALUE )
                return false;

            MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
            exceptionInfo.ThreadId          = GetCurrentThreadId();
            exceptionInfo.ExceptionPointers = pInfo;
            exceptionInfo.ClientPointers    = FALSE;

            const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs | MiniDumpWithThreadInfo | MiniDumpWithHandleData );
            const BOOL bWritten = MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(), hFile, dumpType,
                                                     ( pInfo != nullptr ) ? &exceptionInfo : nullptr, nullptr, nullptr );
            CloseHandle( hFile );
            return bWritten != FALSE;
        }

        /**
         * @brief 폴트 종류와 콜 스택을 로그로 남깁니다.
         * @param pPlatformContext 있으면 그 지점부터 스택을 따라갑니다(Windows 는 CONTEXT*). nullptr 이면 현재 스레드의 스택을
         *                         캡처합니다.
         */
        void reportCrash( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext, EXCEPTION_POINTERS* pExceptionInfo )
        {
            const uint32 selfThreadId = static_cast<uint32>( GetCurrentThreadId() );
            uint32       expectedId   = 0;
            if ( s_reportingThreadId.compare_exchange_strong( expectedId, selfThreadId ) == false )
            {
                // 같은 스레드가 보고하다 또 죽었다 — 더 할 수 있는 것이 없다.
                if ( expectedId == selfThreadId )
                    return;
                // 다른 스레드가 보고 중이다. 그 스레드가 덤프를 다 쓰고 프로세스를 끝낼 때까지 기다린다(보고가 멈춘 경우를 위해 상한을 둔다).
                for ( uint32 waitIndex = 0; waitIndex < 3000 && s_reportingThreadId.load() != 0; ++waitIndex )
                    Sleep( 10 );
                return;
            }

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
            const bool bMiniDumpWritten = writeMiniDump( pExceptionInfo );
            writeCrashContextFile( pReason, pFaultAddress, GetCurrentProcessId(), GetCurrentThreadId() );

            // 본문은 세 플랫폼이 함께 쓴다(CrashContext.cpp).
            writeCrashReport( pReason, pFaultAddress, pPlatformContext, bMiniDumpWritten );

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
        initializeCurrentThread();

        s_pPreviousFilter = SetUnhandledExceptionFilter( &onUnhandledException );
        // CRT 가 필터를 건너뛰는 길 셋을 우리 필터로 돌린다(kExceptionCodeAbort 설명). abort 메시지 창도 끈다 — 리포트는 우리가 쓴다.
        s_pPreviousAbortHandler            = std::signal( SIGABRT, &onAbortSignalInternal );
        s_pPreviousPureCallHandler         = _set_purecall_handler( &onPureCallInternal );
        s_pPreviousInvalidParameterHandler = _set_invalid_parameter_handler( &onInvalidParameterInternal );
        s_previousAbortBehavior            = _set_abort_behavior( 0, _WRITE_ABORT_MSG );
        SW_LOG_TRACE( "Crash handler installed." );
    }

    void CrashHandler::initializeCurrentThread()
    {
        ULONG guaranteeBytes = kStackGuaranteeBytes;
        SetThreadStackGuarantee( &guaranteeBytes );
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

        // initialize 에서 잡은 심볼 참조를 돌려준다(참조 카운트의 짝 맞추기).
        CallStackCapture::shutdown();
    }
} // namespace sw

#endif
