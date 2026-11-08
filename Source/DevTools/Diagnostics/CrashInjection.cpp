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
            /// @brief `CrashInjectionKind::AssertFailure` 가 거짓으로 만드는 단언 식입니다(컴파일러가 단언을 상수로 접지 못하게 volatile).
            ///        단언이 빠지는 빌드(Release · Shipping)에서는 읽는 곳이 없다.
            [[maybe_unused]] static inline volatile bool s_bAssertProbeHolds{ false };
            /// @brief `holdStderrForever` 가 stderr 를 쥐었는지입니다(죽는 시험 프로세스 안에서만 쓴다).
            static inline atomic<bool> s_bStderrHeld{ false };
            /// @brief `overflowStack` 의 재귀를 멈추는 척하는 값입니다. 컴파일러가 끝없는 재귀를 없애거나 경고로 막지 못하게 합니다.
            static inline volatile bool s_bStopStackOverflow{ false };

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

            [[noreturn]] static void holdStderrForever()
            {
#if defined( SW_PLATFORM_WINDOWS )
                _lock_file( stderr );
#elif defined( SW_PLATFORM_POSIX )
                flockfile( stderr );
#else
    #error "Unsupported platform"
#endif
                s_bStderrHeld.store( true );
                while ( true )
                {
                    ThreadUtil::sleepForSeconds( 1 * 3600.0 );
                }
            }

            static uint32 overflowStack( uint32 depth )
            {
                volatile uint8 arrPad[constant::buffer::kMax4096];
                arrPad[depth % constant::buffer::kMax4096] = static_cast<uint8>( depth );
                if ( s_bStopStackOverflow )
                    return 0;
                // 재귀 뒤에 값을 더해 꼬리 호출로 바뀌지 않게 한다(꼬리 호출이면 스택이 자라지 않는다).
                return overflowStack( depth + 1 ) + arrPad[0];
            }

            static void overflowStackOnWorker()
            {
                ThreadCrashStack::initializeCurrentThread();
                overflowStack( 0 );
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
                CrashInjectionInternal::overflowStack( 0 );
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
                SW_ASSERT( CrashInjectionInternal::s_bAssertProbeHolds );
                break;
            }
            case CrashInjectionKind::StderrHeld:
            {
                SW_LOG_ERROR_ID( "CrashInjection.WritingThroughNullPointer2", "[CrashTest] writing through a null pointer while another thread holds stderr" );
                std::thread holder( &CrashInjectionInternal::holdStderrForever );
                holder.detach();
                while ( CrashInjectionInternal::s_bStderrHeld.load() == false )
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
