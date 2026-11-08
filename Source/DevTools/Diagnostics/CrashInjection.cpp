#include "pch.h"

#include "DevTools/Diagnostics/CrashInjection.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Process/ThreadCrashStack.h"

#include <cstdio>
#include <cstdlib>
#include <thread>

namespace sw
{

    namespace
    {
        struct CrashInjectionInternal
        {
            /** @brief 생성자에서 순수 가상 함수를 (한 단계 건너) 부르는 기반 클래스입니다(`CrashInjectionKind::PureVirtualCall`). */
            struct PureCallProbeBase
            {
                PureCallProbeBase() { callThrough(); }
                virtual ~PureCallProbeBase() = default;
                /** @brief 생성자에서 바로 부르면 컴파일러가 막는다. 한 단계 건너 부른다. */
                void         callThrough() { onProbe(); }
                virtual void onProbe() = 0;
            };

            struct PureCallProbe final : PureCallProbeBase
            {
                void onProbe() override {}
            };

            /** @brief stderr 를 쥐고 @p pHeld 를 올린 뒤 영원히 잡니다. @p pHeld 는 부른 쪽의 지역 값이다 — 그 쪽은 올라온 것을 본 뒤 죽는다(이 스레드보다 먼저 사라지지 않는다). */
            [[noreturn]] static void holdStderrForever( atomic<bool>* pHeld )
            {
#if defined( SW_PLATFORM_WINDOWS )
                _lock_file( stderr );
#elif defined( SW_PLATFORM_POSIX )
                flockfile( stderr );
#else
    #error "Unsupported platform"
#endif
                pHeld->store( true );
                while ( true )
                {
                    ThreadUtil::sleepForSeconds( 1 * 3600.0 );
                }
            }

            /** @brief @p bStop 은 재귀를 멈추는 척하는 값이다(부른 쪽의 지역 volatile) — 컴파일러가 끝없는 재귀를 없애거나 경고로 막지 못하게 한다. */
            static uint32 overflowStack( uint32 depth, const volatile bool& bStop )
            {
                volatile uint8 arrPad[constant::buffer::kMax4096];
                arrPad[depth % constant::buffer::kMax4096] = static_cast<uint8>( depth );
                if ( bStop )
                    return 0;
                // 재귀 뒤에 값을 더해 꼬리 호출로 바뀌지 않게 한다(꼬리 호출이면 스택이 자라지 않는다).
                return overflowStack( depth + 1, bStop ) + arrPad[0];
            }

            static void overflowStackOnWorker()
            {
                ThreadCrashStack::initializeCurrentThread();
                volatile bool bStop = false;
                overflowStack( 0, bStop );
            }
        };
    } // namespace

    void CrashInjector::crash( CrashInjectionKind kind )
    {
        switch ( kind )
        {
            case CrashInjectionKind::AccessViolation:
            {
                SW_LOG_ERROR_ID( "CrashInjection.WritingThroughNullPointer", "[CrashTest] writing through a null pointer" );
                volatile int32* pNull = nullptr;
                *pNull                = 1;
                break;
            }
            case CrashInjectionKind::StackOverflow:
            {
                SW_LOG_ERROR_ID( "CrashInjection.OverflowingStackThread", "[CrashTest] overflowing the stack of this thread" );
                volatile bool bStop = false;
                CrashInjectionInternal::overflowStack( 0, bStop );
                break;
            }
            case CrashInjectionKind::WorkerStackOverflow:
            {
                SW_LOG_ERROR_ID( "CrashInjection.OverflowingStackNewThread", "[CrashTest] overflowing the stack of a new thread" );
                std::thread worker( &CrashInjectionInternal::overflowStackOnWorker );
                worker.join();
                break;
            }
            case CrashInjectionKind::Abort:
            {
                SW_LOG_ERROR_ID( "CrashInjection.CallingStdAbort", "[CrashTest] calling std::abort()" );
                std::abort();
            }
            case CrashInjectionKind::PureVirtualCall:
            {
                SW_LOG_ERROR_ID( "CrashInjection.CallingPureVirtualFunction", "[CrashTest] calling a pure virtual function during construction" );
                CrashInjectionInternal::PureCallProbe probe;
                break;
            }
            case CrashInjectionKind::AssertFailure:
            {
                SW_LOG_ERROR_ID( "CrashInjection.FailingSWASSERT", "[CrashTest] failing SW_ASSERT" );
                [[maybe_unused]] volatile bool bProbeHolds = false; // volatile 이라 컴파일러가 단언을 상수로 접지 못한다(단언이 빠지는 구성은 읽지 않는다)
                SW_ASSERT( bProbeHolds );
                break;
            }
            case CrashInjectionKind::StderrHeld:
            {
                SW_LOG_ERROR_ID( "CrashInjection.WritingThroughNullPointer2", "[CrashTest] writing through a null pointer while another thread holds stderr" );
                atomic<bool> bStderrHeld{ false };
                std::thread  holder( &CrashInjectionInternal::holdStderrForever, &bStderrHeld );
                holder.detach();
                while ( bStderrHeld.load() == false )
                {
                    ThreadUtil::yield();
                }
                volatile int32* pNull = nullptr;
                *pNull                = 1;
                break;
            }
            case CrashInjectionKind::None:
            case CrashInjectionKind::Count:
            {
                break;
            }
        }
    }
} // namespace sw
