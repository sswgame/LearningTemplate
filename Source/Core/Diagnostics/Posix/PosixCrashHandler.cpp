#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Diagnostics/CrashHandlerProcessHook.h"
#include "Core/Log/Logger.h"
#include "Core/Process/ThreadCrashStack.h"

#include <csignal>
#include <ctime>

#if defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
    #include "Core/Foundation/PlatformOSHeaders.h"

    #include <pthread.h>
    #include <unistd.h>

namespace sw
{
    namespace
    {
        /**
         * @brief 지금 리포트를 쓰는 스레드의 ID 입니다(0 이면 아무도 쓰지 않는다).
         * @details bool 로 두면 두 스레드가 거의 동시에 죽을 때 두 번째 스레드가 "이미 보고 중" 을 보고 곧장 기본 동작으로 시그널을 다시
         *          올려 프로세스를 끝냅니다 — 첫 스레드가 리포트를 쓰는 도중에.
         */
        atomic<uint64> s_reportingThreadID{ 0 };
        /** @brief 지금 보고 중인 시그널 — 시한이 지나 끝낼 때 종료 코드(128 + 시그널)로 쓴다. */
        atomic<int32> s_reportSignalNumber{ 0 };
        /** @brief 지금 보고 중인 처리기입니다 — 시한 시그널(사용자 인자를 받지 않는다)이 그 진척을 본다. */
        atomic<CrashHandler*> s_pReportingHandler{ nullptr };
        /** @brief 앞 시한 때의 보고 진척 수입니다(`CrashHandler::getProgress`). 시한 시그널이 이것과 비교한다. */
        atomic<uint32> s_seenReportProgress{ 0 };

        /** @brief POSIX 훅의 도우미입니다. 시그널 처리기와 그 뒤의 보고 단계입니다(주인 타입은 크로스 플랫폼이라 시그널 형식을 들 수 없다). */
        struct PosixCrashHandlerInternal
        {
            static constexpr int32  kArrFatalSignal[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
            static constexpr size_t kFatalSignalCount = sizeof( kArrFatalSignal ) / sizeof( kArrFatalSignal[0] );
            /**
             * @brief 설치 전에 걸려 있던 처리기입니다(`kArrFatalSignal` 순서). 보고 뒤와 `uninstall` 이 이것을 되돌린다 — 덮어쓰면 Android debuggerd(tombstone · Play Console 기록)와
             *        ASan · TSan 의 SEGV 처리기가 사라진다(감사 C-13, Breakpad · Crashpad 와 같은 순서). `install` 이 시그널을 걸기 전에 채우고 그 뒤로는 읽기만 한다.
             *        주인 타입(`CrashHandlerProcessHook`)은 크로스 플랫폼이라 `struct sigaction` 을 들 수 없어 여기 둔다.
             */
            static inline struct sigaction s_arrPreviousAction[kFatalSignalCount]{};

            /** @brief 우리가 걸기 전의 처리기를 되돌립니다(없으면 기본 동작). 시그널 안에서도 부른다 — `sigaction` 은 async-signal-safe 다. */
            static void restorePreviousAction( size_t index )
            {
                sigaction( kArrFatalSignal[index], &s_arrPreviousAction[index], nullptr );
            }

            /**
             * @brief 현재 스레드의 64비트 ID 입니다.
             * @details Linux 의 `pthread_t` 는 정수라 그대로 캐스팅합니다.
             */
            static uint64 currentThreadID64() { return static_cast<uint64>( ::pthread_self() ); }

            /**
             * @brief 시한(`alarm`)이 닿았다 — 그동안 보고가 나아갔으면 시한을 다시 걸고, 한 걸음도 나아가지 않았으면 보고를 버리고 곧장 끝냅니다.
             * @details 시한은 진척이 멈춘 시간이다(`CrashHandler::recordProgress`). 시그널 안이라 async-signal-safe 한 것(`alarm` · `write` · `_exit` ·
             *          잠금 없는 원자 읽기)만 쓴다. **stdio 를 거치지 않는다** — 보고가 막힌 지점이 바로 stderr 의 락일 수 있다(CrashInjectionKind::StderrHeld).
             */
            static void onReportDeadline( int32 )
            {
                const CrashHandler* const pHandler = s_pReportingHandler.load();
                if ( pHandler != nullptr )
                {
                    const uint32 progress = pHandler->getProgress();
                    if ( progress != s_seenReportProgress.load() )
                    {
                        s_seenReportProgress.store( progress );
                        ::alarm( pHandler->getReportDeadlineSeconds() );
                        return;
                    }
                }
                constexpr utf8                 kMessage[] = "\n[CrashHandler] crash report made no progress before its deadline - exiting without it\n";
                [[maybe_unused]] const ssize_t written    = ::write( STDERR_FILENO, kMessage, sizeof( kMessage ) - 1 );
                ::_exit( 128 + s_reportSignalNumber.load() );
            }

            /** @brief 폴트 종류와 콜 스택을 @p handler 의 폴더에 남깁니다. */
            static void reportCrash( CrashHandler& handler, int32 signalNumber, const utf8* pReason, const void* pFaultAddress, void* pPlatformContext )
            {
                const uint64 selfThreadID = currentThreadID64();
                uint64       expectedID   = 0;
                if ( s_reportingThreadID.compare_exchange_strong( expectedID, selfThreadID ) == false )
                {
                    // 같은 스레드가 보고하다 또 죽었다 — 더 할 수 있는 것이 없다.
                    if ( expectedID == selfThreadID )
                        return;
                    // 다른 스레드가 보고 중이다. 그 스레드가 끝낼 때까지 기다린다(nanosleep 은 시그널 안에서 불러도 된다). 상한을 둔다.
                    const timespec waitStep{ 0, 10 * constant::time::kNanosecondsPerMillisecond };
                    for ( uint32 waitIndex = 0; waitIndex < 3000 && s_reportingThreadID.load() != 0; ++waitIndex )
                    {
                        ::nanosleep( &waitStep, nullptr );
                    }
                    return;
                }

                // 시한을 설정한다(CrashHandler::setReportDeadline). 보고는 죽어 가는 프로세스 안에서 돈다 — glibc 는 힙 손상을 malloc 의 락을
                // 쥔 채 abort 하고, 아래의 할당은 그 락을 **같은 스레드에서** 영영 기다린다(시한이 없으면 그대로 서 있다).
                // (Windows 는 폴트 스레드가 보고 스레드의 진척을 본다.) 핸들러의 sa_mask 가 비어 있어 SIGALRM 은 여기서도 들어온다.
                s_reportSignalNumber.store( signalNumber );
                s_pReportingHandler.store( &handler );
                struct sigaction deadlineAction{};
                deadlineAction.sa_handler = &onReportDeadline;
                sigemptyset( &deadlineAction.sa_mask );
                sigaction( SIGALRM, &deadlineAction, nullptr );
                s_seenReportProgress.store( handler.getProgress() );
                ::alarm( handler.getReportDeadlineSeconds() );

                // 컨텍스트를 **먼저** 쓴다. 아래 symbolize() 는 sw::string 을 값으로 반환하므로 반드시 할당하고, StringBuilder 도
                // 8KB 를 넘기면 힙으로 늘어난다. 힙이 깨져서 죽은 경우라면 거기서 다시 죽는다. 컨텍스트 쓰기는 fixed_string 과
                // 원시 쓰기뿐이라 할당이 없다(formatstring 은 호출하는 쪽의 버퍼에 쓰므로 크래시 경로에서도 안전하다).
                // POSIX 에는 미니덤프가 없다. 코어 덤프는 `ulimit -c` 와 `kernel.core_pattern` 이 정하는 것이라 프로세스가 만들 수
                // 없다. 그래서 컨텍스트와 심볼 변환한 스택을 파일로 남기는 것이 여기서 할 수 있는 전부이고, 코어 덤프가 켜져
                // 있으면 세션 ID 로 그것과 짝지을 수 있다.
                handler.writeContextFile( pReason, pFaultAddress, static_cast<uint64>( ::getpid() ), currentThreadID64() );

                // 본문은 세 플랫폼이 함께 쓴다(CrashHandler.cpp). 미니덤프는 여기서 만들 수 없으므로 목록에도 넣지 않는다.
                handler.writeReport( pReason, pFaultAddress, pPlatformContext, false );

                ::alarm( 0 );
                s_pReportingHandler.store( nullptr );
                s_reportingThreadID.store( 0 );
            }

            /** @brief 시그널 번호를 이름으로 바꿉니다. */
            static const utf8* signalName( int32 signalNumber )
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
             * @details `std::signal` 은 siginfo 도 ucontext 도 주지 않아서 폴트 주소가 항상 nullptr 이고, 스택도 폴트 지점이 아니라 핸들러
             *          안에서 시작합니다. Windows 쪽이 EXCEPTION_POINTERS 로 둘 다 받는 것과 같은 품질을 내려고 이것을 씁니다.
             */
            static void onFatalSignal( int32 signalNumber, siginfo_t* pSignalInfo, void* pPlatformContext )
            {
                const void*         pFaultAddress = ( pSignalInfo != nullptr ) ? pSignalInfo->si_addr : nullptr;
                CrashHandler* const pHandler      = CrashHandlerProcessHook::findInstalled();
                if ( pHandler != nullptr )
                    reportCrash( *pHandler, signalNumber, signalName( signalNumber ), pFaultAddress, pPlatformContext );

                // 앞 처리기(없으면 기본 동작)로 되돌려 시그널을 다시 올린다 — debuggerd · 새니타이저가 이어 받고, 기본 동작이면 코어 덤프가 그때 남는다.
                for ( size_t index = 0; index < kFatalSignalCount; ++index )
                {
                    if ( kArrFatalSignal[index] == signalNumber )
                        restorePreviousAction( index );
                }
                std::raise( signalNumber );
            }
        };
    } // namespace

    void CrashHandlerProcessHook::writeHangDump( CrashHandler& )
    {
        // POSIX 는 살아 있는 프로세스를 할당 없이 덤프할 길이 없다 — 컨텍스트 파일(`CrashHandler::reportHang`)만 남는다.
    }

    void CrashHandlerProcessHook::install()
    {
        CallStackCapture::initialize();

        // SA_ONSTACK 은 대체 스택을 깐 스레드에서만 효과가 있다. 설치한 스레드는 `CrashHandler::install` 이 깔고, 다른 스레드는 시작할 때
        // `ThreadCrashStack::initializeCurrentThread` 로 깐다. 깔지 않은 스레드는 평소 스택에서 핸들러를 돈다.
        struct sigaction action{};
        action.sa_sigaction = &PosixCrashHandlerInternal::onFatalSignal;
        sigemptyset( &action.sa_mask );
        action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;

        for ( size_t index = 0; index < PosixCrashHandlerInternal::kFatalSignalCount; ++index )
        {
            const int32 signalNumber = PosixCrashHandlerInternal::kArrFatalSignal[index];
            if ( sigaction( signalNumber, &action, &PosixCrashHandlerInternal::s_arrPreviousAction[index] ) != 0 )
                SW_LOG_WARNING_ID( "PosixCrashHandler.SigactionFailedSignalNot", "sigaction(%#) failed - this signal will not be recorded.", signalNumber );
        }

        SW_LOG_TRACE_ID( "PosixCrashHandler.CrashHandlerInstalled", "Crash handler installed." );
    }

    void CrashHandlerProcessHook::uninstall()
    {
        for ( size_t index = 0; index < PosixCrashHandlerInternal::kFatalSignalCount; ++index )
        {
            PosixCrashHandlerInternal::restorePreviousAction( index );
        }

        // 이 스레드의 대체 스택도 해제한다. 커널이 보관한 등록을 지워 두는 편이 뒤에 오는 핸들러(테스트 · 도구)와 엉키지 않는다.
        // 다른 스레드의 것은 그 스레드가 끝날 때 돌려준다.
        ThreadCrashStack::releaseCurrentThread();

        CallStackCapture::shutdown();
    }
} // namespace sw

#endif
