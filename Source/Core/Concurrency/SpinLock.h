/**
 * @file SpinLock.h
 * @brief 짧은 임계 구역용 TTAS 스핀락입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
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
     */
    class SpinLock
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

        /** @brief 락을 얻을 때까지 돕니다(TTAS, 기다리는 동안 sw::cpuPause 로 양보). */
        void lock()
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
        bool try_lock()
        {
            return ( _locked.load( std::memory_order_relaxed ) == false ) &&
                   ( _locked.exchange( true, std::memory_order_acquire ) == false );
        }

        /** @brief 잡고 있는 락을 풉니다. */
        void unlock()
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
