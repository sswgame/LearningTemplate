#include "pch.h"

#include "Core/Concurrency/mutex.h"

#include "Core/Concurrency/RealtimeSection.h"
#include "Core/Concurrency/atomic.h"

namespace sw
{
    namespace
    {
        /** @brief 걸린 잠금 관찰자(교착 검출기)입니다. 잠금마다 acquire 읽기 하나입니다. */
        atomic<ILockObserver*> s_pLockObserver{ nullptr };
#if defined( SW_DO_CHECKS )
        /** @brief 이 스레드가 쥔 `mutex` · `shared_mutex` 수입니다(`mutex::getHeldLockDepth`). */
        thread_local uint32 t_heldLockDepth{ 0 };
        /** @brief 잎 잠금 규칙에서 뺀 범위의 깊이입니다(`ScopedLeafLockExemption`). 0 보다 크면 `getHeldLockDepth` 는 0 입니다. */
        thread_local uint32 t_leafLockExemptionDepth{ 0 };
#endif
    } // namespace

    ScopedLeafLockExemption::ScopedLeafLockExemption()
    {
#if defined( SW_DO_CHECKS )
        ++t_leafLockExemptionDepth;
#endif
    }

    ScopedLeafLockExemption::~ScopedLeafLockExemption()
    {
#if defined( SW_DO_CHECKS )
        --t_leafLockExemptionDepth;
#endif
    }
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

#if defined( SW_DO_CHECKS )
    atomic<bool> mutex::_s_bLockTracking{ false };

    void mutex::enableLockTracking()
    {
        _s_bLockTracking.store( true, std::memory_order_relaxed );
    }
#else
    void mutex::enableLockTracking()
    {
    }
#endif

    uint32 mutex::getHeldLockDepth()
    {
#if defined( SW_DO_CHECKS )
        if ( MutexReentryGuard::isInHook() )
            return 0;
        return t_leafLockExemptionDepth != 0 ? 0 : t_heldLockDepth;
#else
        return 0;
#endif
    }

#if defined( SW_DO_CHECKS )
    void mutex::changeHeldLockDepth( int32 delta )
    {
        t_heldLockDepth = static_cast<uint32>( static_cast<int32>( t_heldLockDepth ) + delta );
        if ( delta > 0 )
            RealtimeSection::recordLock(); // 실시간 구간(오디오 장치 콜백)의 잠금을 센다 — 획득마다 여기를 지난다
    }
#endif
} // namespace sw
