#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/Process/CrashContext.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/StringBuilder.h"

#include <csignal>
#include <cstdio>

#if defined( SW_PLATFORM_MACOS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <pthread.h>
    #include <unistd.h>

SW_LOG_CALLER( "MacCrashHandler" );
namespace sw
{
    namespace
    {
        atomic<bool> s_bInstalled{ false };
        /** @brief 핸들러 안에서 또 크래시가 나도 무한 재진입하지 않게 막습니다. */
        atomic<bool> s_bReporting{ false };

        /**
         * @brief 폴트 종류와 콜 스택을 로그로 남깁니다.
         */
        void reportCrash( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext )
        {
            if ( s_bReporting.exchange( true ) )
                return;

            // 컨텍스트를 **먼저** 쓴다. 아래 symbolize() 는 sw::string 을 값으로 돌려주므로 반드시
            // 할당하고, StringBuilder 도 8KB 를 넘기면 힙으로 확장한다 — 힙이 깨져서 죽은 경우 거기서
            // 다시 죽는다. 컨텍스트 쓰기는 fixed_string 과 fprintf 뿐이라 할당이 없다.
            // (formatstring 은 호출자 버퍼에 쓰므로 크래시 경로에서도 안전하다.)
            // POSIX 에는 미니덤프가 없다 — 코어 덤프는 `ulimit -c` 와 `kernel.core_pattern` 이 정하므로
            // 프로세스가 만들 수 있는 것이 아니다. 그래서 컨텍스트와 심볼화된 스택을 파일로 남기는 것이
            // 여기서 할 수 있는 전부이고, 코어가 켜져 있으면 그와 세션 ID 로 짝지을 수 있다.
            // macOS 의 pthread_t 는 불투명 포인터라 정수 캐스팅이 안 된다 — 전용 API 로 64비트 ID 를 받는다.
            uint64_t threadId64{ 0 };
            ::pthread_threadid_np( nullptr, &threadId64 );
            writeCrashContextFile( pReason, pFaultAddress, static_cast<uint64>( ::getpid() ), static_cast<uint64>( threadId64 ) );

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
            builder.append( "===============================================\n" );

            // 로거가 비동기일 수 있으므로 stderr 로도 직접 흘려 크래시 직전 기록을 보장한다.
            std::fputs( builder.c_str(), stderr );
            std::fflush( stderr );
            // stderr 는 배포 환경에서 아무도 보지 않는다 — 파일로도 남겨야 고객이 보낼 수 있다.
            writeCrashStackFile( builder.c_str() );
            SW_LOG_ERROR( "%#", builder.c_str() );

            s_bReporting.store( false );
        }

        /** @brief 시그널 번호를 이름으로 바꿉니다. */
        const utf8* signalName( int32 signalNumber )
        {
            switch ( signalNumber )
            {
                case SIGSEGV:
                    return "SIGSEGV (segfault)";
                case SIGBUS:
                    return "SIGBUS";
                case SIGILL:
                    return "SIGILL";
                case SIGFPE:
                    return "SIGFPE";
                case SIGABRT:
                    return "SIGABRT";
                default:
                    return "fatal signal";
            }
        }

        void onFatalSignal( int32 signalNumber )
        {
            reportCrash( signalName( signalNumber ), nullptr, nullptr );
            std::signal( signalNumber, SIG_DFL );
            std::raise( signalNumber );
        }
    } // namespace

    void CrashHandler::initialize()
    {
        if ( s_bInstalled.exchange( true ) )
            return;

        CallStackCapture::initialize();

        std::signal( SIGSEGV, &onFatalSignal );
        std::signal( SIGBUS, &onFatalSignal );
        std::signal( SIGILL, &onFatalSignal );
        std::signal( SIGFPE, &onFatalSignal );
        std::signal( SIGABRT, &onFatalSignal );

        SW_LOG_TRACE( "Crash handler installed." );
    }

    void CrashHandler::shutdown()
    {
        if ( s_bInstalled.exchange( false ) == false )
            return;

        std::signal( SIGSEGV, SIG_DFL );
        std::signal( SIGBUS, SIG_DFL );
        std::signal( SIGILL, SIG_DFL );
        std::signal( SIGFPE, SIG_DFL );
        std::signal( SIGABRT, SIG_DFL );

        CallStackCapture::shutdown();
    }
} // namespace sw

#endif
