#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/Process/CrashContext.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/StringBuilder.h"

#include <cstdio>

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <DbgHelp.h>

SW_LOG_CALLER( "WindowsCrashHandler" );
namespace sw
{
    namespace
    {
        atomic<bool> s_bInstalled{ false };
        /** @brief 핸들러 안에서 또 크래시가 나도 무한 재진입하지 않게 막습니다. */
        atomic<bool> s_bReporting{ false };

        LPTOP_LEVEL_EXCEPTION_FILTER s_pPreviousFilter{ nullptr };

        /**
         * @brief 미니덤프를 씁니다.
         * @details 최적화된 배포 빌드는 인라인·꼬리호출로 프레임이 접히고 지역 변수도 남지 않는다 —
         *          텍스트 콜 스택만으로는 원인을 좁히기 어렵다. 덤프가 있어야 디버거로 그 순간을 연다.
         *          타입은 언리얼의 기본과 같은 정도로 고른다: 스택 + 간접 참조 메모리 + 스레드 정보.
         *          Full 덤프는 수백 MB 가 되어 고객이 보내주지 못한다.
         */
        void writeMiniDump( EXCEPTION_POINTERS* pInfo )
        {
            utf8 arrPath[constant::kMaxBuffer1024]{};
            buildCrashReportPath( arrPath, constant::kMaxBuffer1024, "dmp" );

            const HANDLE hFile = CreateFileA( arrPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
            if ( hFile == INVALID_HANDLE_VALUE )
                return;

            MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
            exceptionInfo.ThreadId          = GetCurrentThreadId();
            exceptionInfo.ExceptionPointers = pInfo;
            exceptionInfo.ClientPointers    = FALSE;

            const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs | MiniDumpWithThreadInfo | MiniDumpWithHandleData );
            MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(), hFile, dumpType,
                               ( pInfo != nullptr ) ? &exceptionInfo : nullptr, nullptr, nullptr );
            CloseHandle( hFile );
        }

        /**
         * @brief 폴트 종류와 콜 스택을 로그로 남깁니다.
         * @param pPlatformContext 있으면 그 지점부터 스택을 걷습니다(Windows 는 CONTEXT*).
         *                         nullptr 이면 현재 스레드 스택을 캡처합니다.
         */
        void reportCrash( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext, EXCEPTION_POINTERS* pExceptionInfo )
        {
            if ( s_bReporting.exchange( true ) )
                return;

            // 덤프와 컨텍스트를 **먼저** 쓴다. 이 순서는 취향이 아니라 안전장치다.
            //
            // 크래시 지점에서 안전한 것과 아닌 것이 갈린다:
            //  - formatstring : 호출자 버퍼에 쓰고 할당하지 않는다 (noexcept). 안전.
            //  - fixed_string : 크기가 고정이라 안전. 컨텍스트 값을 여기에 미리 담아 둔 이유다.
            //  - StringBuilder: 스택 버퍼로 시작하지만 **넘치면 힙으로 확장**한다. 스택이 깊으면 넘친다.
            //  - symbolize()  : sw::string 을 값으로 돌려주므로 **반드시 할당**한다. 가장 위험하다.
            //
            // 힙이 이미 깨져서 죽은 경우 아래 심볼화가 다시 죽을 수 있다. 그래서 할당이 없는 덤프·컨텍스트를
            // 먼저 확보한다 — 심볼화가 실패해도 덤프는 남고, 덤프만 있어도 디버거로 그 순간을 열 수 있다.
            // **이 두 줄을 아래로 옮기지 말 것.**
            writeMiniDump( pExceptionInfo );
            writeCrashContextFile( pReason, pFaultAddress, GetCurrentProcessId(), GetCurrentThreadId() );

            // 예외 컨텍스트에서 걸어야 KiUserExceptionDispatcher 등 디스패치 프레임이 앞을
            // 차지하지 않고 실제 폴트 지점이 [0] 에 온다.
            DeepCallStack stack{};
            CallStackCapture::captureFromContext( stack, pPlatformContext );

            StringBuilder<constant::kMaxBuffer8192> builder;
            builder.append( "\n==================== CRASH ====================\n" );
            builder.append( pReason != nullptr ? pReason : "unknown fault" );
            if ( pFaultAddress != nullptr )
            {
                builder.append( "\n  at address: " );
                builder.append( reinterpret_cast<uint64>( pFaultAddress ) );
            }
            builder.append( "\n----------------- call stack ------------------\n" );
            builder.append( CallStackCapture::symbolize( stack ).c_str() );
            builder.append( "-------------- crash report files -------------\n" );
            {
                utf8 arrReportPath[constant::kMaxBuffer1024]{};
                buildCrashReportPath( arrReportPath, constant::kMaxBuffer1024, "dmp" );
                builder.append( arrReportPath );
                builder.append( "\n" );
                buildCrashReportPath( arrReportPath, constant::kMaxBuffer1024, "txt" );
                builder.append( arrReportPath );
                builder.append( "\n" );
            }
            builder.append( "===============================================\n" );

            // 로거가 비동기일 수 있으므로 stderr 로도 직접 흘려 크래시 직전 기록을 보장한다.
            std::fputs( builder.c_str(), stderr );
            std::fflush( stderr );
            // stderr 는 배포 환경에서 아무도 보지 않는다 — 파일로도 남겨야 고객이 보낼 수 있다.
            writeCrashStackFile( builder.c_str() );
            SW_LOG_ERROR( "%#", builder.c_str() );

            s_bReporting.store( false );
        }

        /** @brief 예외 코드를 사람이 읽는 이름으로 바꿉니다. */
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

        // initialize 에서 잡은 심볼 참조를 돌려준다(참조 카운트 짝 맞추기).
        CallStackCapture::shutdown();
    }
} // namespace sw

#endif
