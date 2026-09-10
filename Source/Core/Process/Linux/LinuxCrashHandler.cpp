#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/Process/CrashContext.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/StringBuilder.h"

#include <csignal>
#include <cstdio>

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <pthread.h>
    #include <unistd.h>

SW_LOG_CALLER( "LinuxCrashHandler" );
namespace sw
{
    namespace
    {
        atomic<bool> s_bInstalled{ false };
        /** @brief 핸들러 안에서 또 크래시가 나도 무한 재진입하지 않게 막습니다. */
        atomic<bool> s_bReporting{ false };

        /**
         * @brief 대체 시그널 스택.
         *
         * 스택 오버플로로 난 SIGSEGV 는 스택이 이미 바닥난 상태라, 핸들러를 그 스택 위에서 실행할 수
         * 없다 — 핸들러 진입 자체가 다시 폴트나고 프로세스는 **아무 기록도 없이** 죽는다. 정작 가장
         * 알고 싶은 크래시가 그렇게 사라진다. 별도 스택을 깔고 SA_ONSTACK 으로 거기서 돌린다.
         */
        // 최신 glibc 의 SIGSTKSZ 는 sysconf() 로 바뀌어 상수가 아니다 — 정적 배열에 쓸 수 없으므로
        // 넉넉한 고정 크기를 쓴다. 리포트 경로가 스택에 얹는 것은 StringBuilder<8192> 와
        // DeepCallStack(64 프레임) 정도이고, 나머지는 힙이다.
        constexpr size_t kSignalStackSize = 128 * 1024;
        uint8            s_arrSignalStack[kSignalStackSize];
        constexpr int32  kArrFatalSignal[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };

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
            writeCrashContextFile( pReason, pFaultAddress, static_cast<uint64>( ::getpid() ),
                                   static_cast<uint64>( ::pthread_self() ) );

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

        /**
         * @brief SA_SIGINFO 핸들러 — 폴트 주소와 레지스터 컨텍스트를 함께 받습니다.
         *
         * 예전엔 `std::signal` 을 썼다. 그건 siginfo 도 ucontext 도 주지 않아서 폴트 주소가 늘
         * nullptr 이었고, 스택도 폴트 지점이 아니라 핸들러 안에서 시작했다 — Windows 쪽은
         * EXCEPTION_POINTERS 로 둘 다 받아 쓰고 있어 리포트 품질이 한쪽만 크게 떨어졌다.
         */
        void onFatalSignal( int32 signalNumber, siginfo_t* pSignalInfo, void* pPlatformContext )
        {
            const void* pFaultAddress = ( pSignalInfo != nullptr ) ? pSignalInfo->si_addr : nullptr;
            reportCrash( signalName( signalNumber ), pFaultAddress, pPlatformContext );

            // 기본 동작으로 되돌려 다시 올린다 — 코어 덤프가 켜져 있으면 그때 남는다.
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

        // 스택 오버플로에서도 핸들러가 돌 수 있도록 먼저 대체 스택을 깐다.
        stack_t signalStack{};
        signalStack.ss_sp    = s_arrSignalStack;
        signalStack.ss_size  = kSignalStackSize;
        signalStack.ss_flags = 0;
        const bool bAltStack = ( sigaltstack( &signalStack, nullptr ) == 0 );
        if ( bAltStack == false )
        {
            SW_LOG_WARNING( "sigaltstack failed — 스택 오버플로 크래시는 기록되지 않습니다." );
        }

        struct sigaction action{};
        action.sa_sigaction = &onFatalSignal;
        sigemptyset( &action.sa_mask );
        action.sa_flags = SA_SIGINFO | SA_RESTART;
        if ( bAltStack )
            action.sa_flags |= SA_ONSTACK;

        for ( int32 signalNumber : kArrFatalSignal )
        {
            if ( sigaction( signalNumber, &action, nullptr ) != 0 )
            {
                SW_LOG_WARNING( "sigaction(%#) failed — 이 시그널은 기록되지 않습니다.", signalNumber );
            }
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
            sigaction( signalNumber, &restoreAction, nullptr );

        // 대체 스택도 걷는다 — s_arrSignalStack 은 정적이라 남아 있어도 되지만, 커널이 이 프로세스에
        // 대해 들고 있는 등록을 지워 두는 편이 뒤에 오는 핸들러(테스트·툴)와 얽히지 않는다.
        stack_t disableStack{};
        disableStack.ss_flags = SS_DISABLE;
        sigaltstack( &disableStack, nullptr );

        CallStackCapture::shutdown();
    }
} // namespace sw

#endif
