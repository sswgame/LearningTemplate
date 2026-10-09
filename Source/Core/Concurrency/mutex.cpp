#include "pch.h"

#include "Core/Concurrency/mutex.h"

#include "Core/Concurrency/atomic.h"

namespace sw
{
    namespace
    {
        /** @brief 걸린 잠금 관찰자(교착 검출기)입니다. 잠금마다 acquire 읽기 하나입니다. */
        atomic<ILockObserver*> s_pLockObserver{ nullptr };
    } // namespace
} // namespace sw

namespace sw
{
    void mutex::registerLockObserver( ILockObserver* pObserver )
    {
        ILockObserver* pExpected{ nullptr };
        s_pLockObserver.compare_exchange_strong( pExpected, pObserver, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    void mutex::unregisterLockObserver( ILockObserver* pObserver )
    {
        ILockObserver* pExpected = pObserver;
        s_pLockObserver.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    ILockObserver* mutex::getLockObserver()
    {
        return s_pLockObserver.load( std::memory_order_acquire );
    }
} // namespace sw
