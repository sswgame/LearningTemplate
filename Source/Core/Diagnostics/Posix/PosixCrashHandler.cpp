#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Log/Logger.h"
#include "Core/Process/ThreadCrashStack.h"

#include <csignal>
#include <ctime>

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <pthread.h>
    #include <unistd.h>

SW_LOG_CALLER( "PosixCrashHandler" );
namespace sw
{
    namespace
    {
        /**
         * @brief 현재 스레드의 64비트 ID 입니다.
         * @details Linux 의 `pthread_t` 는 정수라 그대로 캐스팅합니다.
         */
        uint64 currentThreadID64Internal()
        {
            return static_cast<uint64>( ::pthread_self() );
        }

        atomic<bool> s_bInstalled{ false };
        /**
         * @brief 지금 리포트를 쓰는 스레드의 ID 입니다(0 이면 아무도 쓰지 않는다).
         * @details bool 로 두면 두 스레드가 거의 동시에 죽을 때 두 번째 스레드가 "이미 보고 중" 을 보고 곧장 기본 동작으로 시그널을 다시
         *          올려 프로세스를 끝냅니다 — 첫 스레드가 리포트를 쓰는 도중에.
         */
        atomic<uint64> s_reportingThreadID{ 0 };
        /** @brief 지금 보고 중인 시그널 — 시한이 지나 끝낼 때 종료 코드(128 + 시그널)로 쓴다. */
        atomic<int32> s_reportSignalNumber{ 0 };

        constexpr int32 kArrFatalSignal[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };

        /**
         * @brief 보고가 시한을 넘겼다(`alarm`) — 보고를 버리고 곧장 끝냅니다.
         * @details 시그널 안이라 async-signal-safe 한 것(`write` · `_exit`)만 쓴다. **stdio 를 거치지 않는다** — 보고가 막힌 자리가 바로
         *          stderr 의 락일 수 있다(CrashTestKind::StderrHeld).
         */
        void onReportDeadlineInternal( int32 )
        {
            constexpr utf8                 kMessage[] = "\n[CrashHandler] crash report did not finish in time - exiting without it\n";
            [[maybe_unused]] const ssize_t written    = ::write( STDERR_FILENO, kMessage, sizeof( kMessage ) - 1 );
            ::_exit( 128 + s_reportSignalNumber.load() );
        }

        /**
         * @brief 폴트 종류와 콜 스택을 로그로 남깁니다.
         */
        void reportCrash( int32 signalNumber, const utf8* pReason, const void* pFaultAddress, void* pPlatformContext )
        {
            const uint64 selfThreadID = currentThreadID64Internal();
            uint64       expectedID   = 0;
            if ( s_reportingThreadID.compare_exchange_strong( expectedID, selfThreadID ) == false )
            {
                // 같은 스레드가 보고하다 또 죽었다 — 더 할 수 있는 것이 없다.
                if ( expectedID == selfThreadID )
                    return;
                // 다른 스레드가 보고 중이다. 그 스레드가 끝낼 때까지 기다린다(nanosleep 은 시그널 안에서 불러도 된다). 상한을 둔다.
                const timespec waitStep{ 0, 10 * 1000 * 1000 };
                for ( uint32 waitIndex = 0; waitIndex < 3000 && s_reportingThreadID.load() != 0; ++waitIndex )
                {
                    ::nanosleep( &waitStep, nullptr );
                }
                return;
            }

            // 시한을 건다(CrashHandler::setReportDeadline). 보고는 죽어 가는 프로세스 안에서 돈다 — glibc 는 힙 손상을 malloc 의 락을
            // 쥔 채 abort 하고, 아래의 할당은 그 락을 **같은 스레드에서** 영영 기다린다(시한이 없으면 그대로 서 있다).
            // (Windows 는 폴트 스레드가 보고 스레드를 시한까지만 기다린다.) 핸들러의 sa_mask 가 비어 있어 SIGALRM 은 여기서도 들어온다.
            s_reportSignalNumber.store( signalNumber );
            struct sigaction deadlineAction{};
            deadlineAction.sa_handler = &onReportDeadlineInternal;
            sigemptyset( &deadlineAction.sa_mask );
            sigaction( SIGALRM, &deadlineAction, nullptr );
            ::alarm( getCrashReportDeadlineSeconds() );

            // 컨텍스트를 **먼저** 쓴다. 아래 symbolize() 는 sw::string 을 값으로 반환하므로 반드시 할당하고, StringBuilder 도
            // 8KB 를 넘기면 힙으로 늘어난다. 힙이 깨져서 죽은 경우라면 거기서 다시 죽는다. 컨텍스트 쓰기는 fixed_string 과
            // fprintf 뿐이라 할당이 없다(formatstring 은 호출하는 쪽의 버퍼에 쓰므로 크래시 경로에서도 안전하다).
            // POSIX 에는 미니덤프가 없다. 코어 덤프는 `ulimit -c` 와 `kernel.core_pattern` 이 정하는 것이라 프로세스가 만들 수
            // 없다. 그래서 컨텍스트와 심볼 변환한 스택을 파일로 남기는 것이 여기서 할 수 있는 전부이고, 코어 덤프가 켜져
            // 있으면 세션 ID 로 그것과 짝지을 수 있다.
            writeCrashContextFile( pReason, pFaultAddress, static_cast<uint64>( ::getpid() ),
                                   currentThreadID64Internal() );

            // 본문은 세 플랫폼이 함께 쓴다(CrashContext.cpp). 미니덤프는 여기서 만들 수 없으므로 목록에도 넣지 않는다.
            writeCrashReport( pReason, pFaultAddress, pPlatformContext, false );

            ::alarm( 0 );
            s_reportingThreadID.store( 0 );
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

        /**
         * @brief SA_SIGINFO 핸들러입니다. 폴트 주소와 레지스터 컨텍스트를 함께 받습니다.
         *
         * `std::signal` 은 siginfo 도 ucontext 도 주지 않아서 폴트 주소가 항상 nullptr 이고, 스택도 폴트 지점이 아니라 핸들러
         * 안에서 시작합니다. Windows 쪽이 EXCEPTION_POINTERS 로 둘 다 받는 것과 같은 품질을 내려고 이것을 씁니다.
         */
        void onFatalSignal( int32 signalNumber, siginfo_t* pSignalInfo, void* pPlatformContext )
        {
            const void* pFaultAddress = ( pSignalInfo != nullptr ) ? pSignalInfo->si_addr : nullptr;
            reportCrash( signalNumber, signalName( signalNumber ), pFaultAddress, pPlatformContext );

            // 기본 동작으로 되돌려 시그널을 다시 올린다. 코어 덤프가 켜져 있으면 그때 남는다.
            struct sigaction restoreAction{};
            restoreAction.sa_handler = SIG_DFL;
            sigemptyset( &restoreAction.sa_mask );
            sigaction( signalNumber, &restoreAction, nullptr );
            std::raise( signalNumber );
        }
    } // namespace

    void CrashHandler::initialize()
    {
        if ( s_bInstalled.exchange( true ) )
            return;

        CallStackCapture::initialize();

        // 스택 오버플로에서도 핸들러가 돌 수 있도록 이 스레드에 먼저 대체 스택을 깐다. 다른 스레드는 시작할 때 `ThreadCrashStack::initializeCurrentThread` 로 깐다.
        ThreadCrashStack::initializeCurrentThread();

        // SA_ONSTACK 은 대체 스택을 깐 스레드에서만 효과가 있다. 깔지 않은 스레드는 평소 스택에서 핸들러를 돈다.
        struct sigaction action{};
        action.sa_sigaction = &onFatalSignal;
        sigemptyset( &action.sa_mask );
        action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;

        for ( int32 signalNumber : kArrFatalSignal )
        {
            if ( sigaction( signalNumber, &action, nullptr ) != 0 )
                SW_LOG_WARNING( "sigaction(%#) failed — 이 시그널은 기록되지 않습니다.", signalNumber );
        }

        SW_LOG_TRACE( "Crash handler installed." );
    }

    void CrashHandler::shutdown()
    {
        if ( s_bInstalled.exchange( false ) == false )
            return;

        struct sigaction restoreAction{};
        restoreAction.sa_handler = SIG_DFL;
        sigemptyset( &restoreAction.sa_mask );
        for ( int32 signalNumber : kArrFatalSignal )
        {
            sigaction( signalNumber, &restoreAction, nullptr );
        }

        // 이 스레드의 대체 스택도 걷어 낸다. 커널이 들고 있는 등록을 지워 두는 편이 뒤에 오는 핸들러(테스트 · 도구)와 엉키지 않는다.
        // 다른 스레드의 것은 그 스레드가 끝날 때 돌려준다.
        ThreadCrashStack::releaseCurrentThread();

        CallStackCapture::shutdown();
    }
} // namespace sw

#endif
