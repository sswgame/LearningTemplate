#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/Process/CrashContext.h"
#include "Core/Process/CrashHandler.h"

#include <csignal>
#include <ctime>

#if defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <pthread.h>
    #include <unistd.h>

SW_LOG_CALLER( "PosixCrashHandler" );
namespace sw
{
    namespace
    {
        /**
         * @brief 현재 스레드의 64비트 ID 입니다. **이 파일에서 OS 마다 달라지는 곳은 여기뿐입니다.**
         * @details Linux 의 `pthread_t` 는 정수라 그대로 캐스팅되지만, macOS 의 것은 불투명 포인터라 전용 API 로 받아야 합니다.
         *          나머지(sigaction · SA_SIGINFO · 대체 시그널 스택)는 POSIX 라 두 플랫폼이 같은 코드를 씁니다.
         */
        uint64 currentThreadId64Internal()
        {
    #if defined( SW_PLATFORM_MACOS )
            uint64_t threadId64{ 0 };
            ::pthread_threadid_np( nullptr, &threadId64 );
            return static_cast<uint64>( threadId64 );
    #else
            return static_cast<uint64>( ::pthread_self() );
    #endif
        }

        atomic<bool> s_bInstalled{ false };
        /**
         * @brief 지금 리포트를 쓰는 스레드의 ID 입니다(0 이면 아무도 쓰지 않는다).
         * @details 예전에는 bool 이라, 두 스레드가 거의 동시에 죽으면 두 번째 스레드가 "이미 보고 중" 을 보고 곧장 기본 동작으로 시그널을 다시
         *          올려 프로세스를 끝냈습니다 — 첫 스레드가 리포트를 쓰는 도중에.
         */
        atomic<uint64> s_reportingThreadId{ 0 };

        /**
         * @brief 대체 시그널 스택입니다.
         *
         * 스택 오버플로로 생긴 SIGSEGV 는 스택이 이미 바닥난 상태라 그 스택 위에서 핸들러를 실행할 수 없습니다. 핸들러에
         * 들어가는 순간 다시 폴트가 나고, 프로세스는 **아무 기록도 없이** 죽습니다. 정작 가장 알고 싶은 크래시가 그렇게
         * 사라집니다. 그래서 별도 스택을 깔고 SA_ONSTACK 으로 그 위에서 핸들러를 돌립니다.
         */
        // 최신 glibc 의 SIGSTKSZ 는 sysconf() 를 부르도록 바뀌어 상수가 아니다. 넉넉한 고정 크기를 쓴다. 리포트 경로가 스택에 올리는 것은
        // StringBuilder<8192> 와 DeepCallStack(64 프레임) 정도이고, 나머지는 힙이다.
        constexpr size_t kSignalStackSize  = 128 * 1024;
        constexpr int32  kArrFatalSignal[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };

        /**
         * @brief 이 스레드의 대체 시그널 스택입니다. **`sigaltstack` 은 스레드마다다** — 예전에는 `initialize` 를 부른 스레드 하나에만 정적
         *        배열을 깔아, 작업 스레드 · 렌더 스레드의 스택 오버플로는 기록 없이 죽었다. 스레드가 끝날 때 스택을 끄고 돌려준다.
         */
        struct ThreadSignalStackInternal
        {
            uint8* _pStack{ nullptr };

            ~ThreadSignalStackInternal() { release(); }

            void release()
            {
                if ( _pStack == nullptr )
                    return;
                stack_t disableStack{};
                disableStack.ss_flags = SS_DISABLE;
                sigaltstack( &disableStack, nullptr );
                Memory::free( _pStack );
                _pStack = nullptr;
            }
        };

        thread_local ThreadSignalStackInternal t_signalStack{};

        /**
         * @brief 폴트 종류와 콜 스택을 로그로 남깁니다.
         */
        void reportCrash( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext )
        {
            const uint64 selfThreadId = currentThreadId64Internal();
            uint64       expectedId   = 0;
            if ( s_reportingThreadId.compare_exchange_strong( expectedId, selfThreadId ) == false )
            {
                // 같은 스레드가 보고하다 또 죽었다 — 더 할 수 있는 것이 없다.
                if ( expectedId == selfThreadId )
                    return;
                // 다른 스레드가 보고 중이다. 그 스레드가 끝낼 때까지 기다린다(nanosleep 은 시그널 안에서 불러도 된다). 상한을 둔다.
                const timespec waitStep{ 0, 10 * 1000 * 1000 };
                for ( uint32 waitIndex = 0; waitIndex < 3000 && s_reportingThreadId.load() != 0; ++waitIndex )
                    ::nanosleep( &waitStep, nullptr );
                return;
            }

            // 컨텍스트를 **먼저** 쓴다. 아래 symbolize() 는 sw::string 을 값으로 반환하므로 반드시 할당하고, StringBuilder 도
            // 8KB 를 넘기면 힙으로 늘어난다. 힙이 깨져서 죽은 경우라면 거기서 다시 죽는다. 컨텍스트 쓰기는 fixed_string 과
            // fprintf 뿐이라 할당이 없다(formatstring 은 호출하는 쪽의 버퍼에 쓰므로 크래시 경로에서도 안전하다).
            // POSIX 에는 미니덤프가 없다. 코어 덤프는 `ulimit -c` 와 `kernel.core_pattern` 이 정하는 것이라 프로세스가 만들 수
            // 없다. 그래서 컨텍스트와 심볼 변환한 스택을 파일로 남기는 것이 여기서 할 수 있는 전부이고, 코어 덤프가 켜져
            // 있으면 세션 ID 로 그것과 짝지을 수 있다.
            writeCrashContextFile( pReason, pFaultAddress, static_cast<uint64>( ::getpid() ),
                                   currentThreadId64Internal() );

            // 본문은 세 플랫폼이 함께 쓴다(CrashContext.cpp). 미니덤프는 여기서 만들 수 없으므로 목록에도 넣지 않는다.
            writeCrashReport( pReason, pFaultAddress, pPlatformContext, false );

            s_reportingThreadId.store( 0 );
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
         * 예전에는 `std::signal` 을 썼습니다. 그것은 siginfo 도 ucontext 도 주지 않아서 폴트 주소가 항상 nullptr 이었고,
         * 스택도 폴트 지점이 아니라 핸들러 안에서 시작했습니다. Windows 쪽은 EXCEPTION_POINTERS 로 둘 다 받아 쓰고 있어서
         * 한쪽의 리포트 품질만 크게 떨어졌습니다.
         */
        void onFatalSignal( int32 signalNumber, siginfo_t* pSignalInfo, void* pPlatformContext )
        {
            const void* pFaultAddress = ( pSignalInfo != nullptr ) ? pSignalInfo->si_addr : nullptr;
            reportCrash( signalName( signalNumber ), pFaultAddress, pPlatformContext );

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

        // 스택 오버플로에서도 핸들러가 돌 수 있도록 이 스레드에 먼저 대체 스택을 깐다. 다른 스레드는 시작할 때 `initializeCurrentThread` 로 깐다.
        initializeCurrentThread();

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

    void CrashHandler::initializeCurrentThread()
    {
        if ( t_signalStack._pStack != nullptr )
            return;

        uint8* pStack = static_cast<uint8*>( Memory::allocate( kSignalStackSize ) );
        if ( pStack == nullptr )
            return;

        stack_t signalStack{};
        signalStack.ss_sp    = pStack;
        signalStack.ss_size  = kSignalStackSize;
        signalStack.ss_flags = 0;
        if ( sigaltstack( &signalStack, nullptr ) != 0 )
        {
            Memory::free( pStack );
            SW_LOG_WARNING( "sigaltstack failed — a stack overflow on this thread will not be reported" );
            return;
        }
        t_signalStack._pStack = pStack;
    }

    void CrashHandler::shutdown()
    {
        if ( s_bInstalled.exchange( false ) == false )
            return;

        struct sigaction restoreAction{};
        restoreAction.sa_handler = SIG_DFL;
        sigemptyset( &restoreAction.sa_mask );
        for ( int32 signalNumber : kArrFatalSignal )
            sigaction( signalNumber, &restoreAction, nullptr );

        // 이 스레드의 대체 스택도 걷어 낸다. 커널이 들고 있는 등록을 지워 두는 편이 뒤에 오는 핸들러(테스트 · 도구)와 엉키지 않는다.
        // 다른 스레드의 것은 그 스레드가 끝날 때 돌려준다.
        t_signalStack.release();

        CallStackCapture::shutdown();
    }
} // namespace sw

#endif
