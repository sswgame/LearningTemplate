/**
 * @file SpinLock.h
 * @brief 짧게 돌다가 주소 대기로 잠드는 적응형 잠금입니다(이름은 사용처를 지키려고 그대로 둔다).
 */
#pragma once
#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) SpinLock — 짧은 구간용 적응형 잠금. 구간이 길고 교착 검출 · 계측이 필요하면 sw::mutex 를 쓴다
    //    다툼이 없으면 원자 연산 하나, 있으면 kSpinLimit 번 cpuPause 로 돌고, 그래도 못 얻으면 Futex 로 잠든다
    // ------------------------------------------------------------------------------
    /**
     * @class SpinLock
     * @brief 32 비트 상태 워드 하나로 된 적응형 잠금입니다. 짧게 돌다가 못 얻으면 그 워드의 주소에서 잠듭니다.
     * @details 순수 스핀(TTAS)이면 쥔 스레드가 선점될 때 기다리는 쪽 전부가 퀀텀 내내 코어를 태운다 — 코어보다 스레드가 많거나
     *          (Android big.LITTLE) 구간 안에서 힙 할당 · 사용자 생성자가 돌면 커진다. 언리얼이 `FCriticalSection` 스핀에서
     *          `UE::FMutex`(짧게 스핀한 뒤 주소 대기로 잠드는 잠금)로 옮긴 것과 같은 선택이다(상태 셋은 Drepper "Futexes Are Tricky" 의 mutex3).
     *          잠금 계약 분석(`SW_GUARDED_BY( _spinLock )`)이 보는 잠금이다 — 가드는 `ScopedLock<SpinLock>`(`ScopedLock.h`).
     *          교착 검출기 · 쥔 잠금 수는 거치지 않는다(다툼 없는 경로가 원자 연산 하나라 알림 비용이 구간보다 크다).
     *          상수 초기화된다(`LifetimeScope` 가 정적 객체 소멸자 안에서도 쓴다).
     */
    class SW_CAPABILITY( "SpinLock" ) SpinLock
    {
    public:
        /**
         * @brief 잠들기 전에 도는 `cpuPause` 횟수(약 2 us, `TaskManager` 의 대기 스핀과 같은 예산)입니다.
         * @details Config JSON 이 아니라 상수인 까닭: 설정을 읽기 전(정적 초기화) · 읽은 뒤(정적 소멸)에도 쓰는 Core 잠금이다. 언리얼 `FMutex` 도 상수다.
         */
        static constexpr uint32 kSpinLimit = 64;

        /** @brief 해제 상태로 둡니다. */
        SpinLock() = default;
        /** @brief 잠금이 풀린 상태에서 파괴해야 합니다. */
        ~SpinLock() = default;

        /** @brief 복사를 금지합니다. */
        SpinLock( const SpinLock& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        SpinLock& operator=( const SpinLock& ) = delete;

        // 잠금 자신의 구현은 원자 변수를 다루므로 분석하지 않는다(SW_NO_THREAD_SAFETY_ANALYSIS).
        /** @brief 락을 얻을 때까지 기다립니다. 짧게 돌고, 그래도 못 얻으면 잠듭니다. */
        void lock() SW_ACQUIRE_LOCK() SW_NO_THREAD_SAFETY_ANALYSIS
        {
            uint32 expected = kStateFree;
            if ( _state.compare_exchange_strong( expected, kStateLocked, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
            lockSlow();
        }

        /** @brief 락을 한 번만 시도합니다. 얻으면 true 입니다. */
        bool try_lock() SW_TRY_ACQUIRE_LOCK( true ) SW_NO_THREAD_SAFETY_ANALYSIS
        {
            uint32 expected = kStateFree;
            return ( _state.load( std::memory_order_relaxed ) == kStateFree ) &&
                   _state.compare_exchange_strong( expected, kStateLocked, std::memory_order_acquire, std::memory_order_relaxed );
        }

        /** @brief 잡고 있는 락을 풉니다. 잠든 대기자가 있었으면 하나를 깨웁니다. */
        void unlock() SW_RELEASE_LOCK() SW_NO_THREAD_SAFETY_ANALYSIS
        {
            if ( _state.exchange( kStateFree, std::memory_order_release ) == kStateLockedWithSleepers )
                Futex::wakeOne( _state );
        }

        /** @brief 지금 잠겨 있는지 확인합니다. */
        bool isLocked() const
        {
            return _state.load( std::memory_order_relaxed ) != kStateFree;
        }

    private:
        /** @brief 다툼이 있을 때의 경로입니다. 풀리기를 짧게 기다렸다가, 그래도 쥔 쪽이 있으면 "잠든 대기자 있음" 으로 바꾸고 잠듭니다. */
        void lockSlow() SW_NO_THREAD_SAFETY_ANALYSIS
        {
            for ( uint32 spin = 0; spin < kSpinLimit; ++spin )
            {
                sw::cpuPause();
                uint32 expected = kStateFree;
                if ( _state.load( std::memory_order_relaxed ) == kStateFree &&
                     _state.compare_exchange_weak( expected, kStateLocked, std::memory_order_acquire, std::memory_order_relaxed ) )
                    return;
            }
            // 잠에서 깬 쪽은 다른 잠든 대기자가 있는지 모르므로 "있음" 으로 얻는다 — 그래야 그가 풀 때 다음 대기자를 깨운다(깨움 유실 방지).
            while ( _state.exchange( kStateLockedWithSleepers, std::memory_order_acquire ) != kStateFree )
            {
                Futex::wait( _state, kStateLockedWithSleepers );
            }
        }

        static constexpr uint32 kStateFree               = 0; ///< 풀림
        static constexpr uint32 kStateLocked             = 1; ///< 잠김, 잠든 대기자 없음(푸는 쪽이 시스템 콜을 하지 않는다)
        static constexpr uint32 kStateLockedWithSleepers = 2; ///< 잠김, 잠든(또는 잠들려는) 대기자가 있을 수 있음

        atomic<uint32> _state{ kStateFree }; ///< 상태이자 `Futex` 가 잠드는 워드
    };

} // namespace sw
