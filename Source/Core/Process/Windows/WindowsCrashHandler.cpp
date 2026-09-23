#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/Process/CrashContext.h"
#include "Core/Process/CrashHandler.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <DbgHelp.h>

SW_LOG_CALLER( "WindowsCrashHandler" );
namespace sw
{
    namespace
    {
        atomic<bool> s_bInstalled{ false };
        /** @brief 핸들러 안에서 또 크래시가 나도 끝없이 재진입하지 않게 막습니다. */
        atomic<bool> s_bReporting{ false };

        LPTOP_LEVEL_EXCEPTION_FILTER s_pPreviousFilter{ nullptr };

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
            if ( s_bReporting.exchange( true ) )
                return;

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

            s_bReporting.store( false );
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

        s_pPreviousFilter = SetUnhandledExceptionFilter( &onUnhandledException );
        SW_LOG_TRACE( "Crash handler installed." );
    }

    void CrashHandler::shutdown()
    {
        if ( s_bInstalled.exchange( false ) == false )
            return;

        SetUnhandledExceptionFilter( s_pPreviousFilter );
        s_pPreviousFilter = nullptr;

        // initialize 에서 잡은 심볼 참조를 돌려준다(참조 카운트의 짝 맞추기).
        CallStackCapture::shutdown();
    }
} // namespace sw

#endif
