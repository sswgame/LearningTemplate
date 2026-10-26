/**
 * @file SpinLock.h
 * @brief 짧은 임계 구역용 TTAS 스핀락입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/atomic.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) SpinLock — 짧은 구간 전용. 길면 sw::mutex 를 쓴다
    //    TTAS(Test and Test-And-Set), 기다리는 동안 sw::cpuPause() 로 스핀 중임을 CPU 에 알린다
    // ------------------------------------------------------------------------------
    /**
     * @class SpinLock
     * @brief sw::atomic<bool> 기반 TTAS 스핀락입니다. 아주 짧은 임계 구역에서 mutex 대신 씁니다.
     * @details 잠금 계약 분석(`SW_GUARDED_BY( _spinLock )`)이 보는 잠금이다 — 가드는 `ScopedLock<SpinLock>`(`ScopedLock.h`).
     *          교착 검출기 · 쥔 잠금 수는 거치지 않는다(구간이 몇 명령이라 알림 비용이 구간보다 크다).
     */
    class SW_CAPABILITY( "SpinLock" ) SpinLock
    {
    public:
        /** @brief 플래그를 해제 상태로 둡니다. */
        SpinLock() = default;
        /** @brief 잠금이 풀린 상태에서 파괴해야 합니다. */
        ~SpinLock() = default;

        /** @brief 복사를 금지합니다. */
        SpinLock( const SpinLock& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        SpinLock& operator=( const SpinLock& ) = delete;

        // 잠금 자신의 구현은 원자 변수를 다루므로 분석하지 않는다(SW_NO_THREAD_SAFETY_ANALYSIS).
        /** @brief 락을 얻을 때까지 돕니다(TTAS, 기다리는 동안 sw::cpuPause 로 양보). */
        void lock() SW_ACQUIRE_LOCK() SW_NO_THREAD_SAFETY_ANALYSIS
        {
            while ( _locked.exchange( true, std::memory_order_acquire ) )
            {
                while ( _locked.load( std::memory_order_relaxed ) )
                {
                    sw::cpuPause();
                }
            }
        }

        /** @brief 락을 한 번만 시도합니다. 얻으면 true 입니다. */
        bool try_lock() SW_TRY_ACQUIRE_LOCK( true ) SW_NO_THREAD_SAFETY_ANALYSIS
        {
            return ( _locked.load( std::memory_order_relaxed ) == false ) &&
                   ( _locked.exchange( true, std::memory_order_acquire ) == false );
        }

        /** @brief 잡고 있는 락을 풉니다. */
        void unlock() SW_RELEASE_LOCK() SW_NO_THREAD_SAFETY_ANALYSIS
        {
            _locked.store( false, std::memory_order_release );
        }

        /** @brief 지금 잠겨 있는지 확인합니다. */
        bool isLocked() const
        {
            return _locked.load( std::memory_order_relaxed );
        }

    private:
        atomic<bool> _locked{ false };
    };

} // namespace sw
