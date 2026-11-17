/**
 * @file Subscription.cpp
 * @brief 구독의 제어 블록 — 잠금 · 목록 · 출처 포인터가 출처보다 오래 살고, 출처의 닫기가 진행 중인 해제를 기다립니다(감사 C-20).
 * @details 함수 몸통이 여기 있는 까닭: 해제 중 표시(스레드 지역 변수)가 프로세스에 하나여야 한다 — 헤더 인라인이면 모듈(DLL)마다 따로 생겨
 *          한 스레드가 게임 모듈에서 풀고 엔진에서 닫을 때 자기 해제를 못 알아보고 영영 기다린다.
 */
#include "pch.h"

#include "Core/Delegate/Subscription.h"

#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        /** @brief `_releasingCount` 의 맨 위 비트 — 닫는 쪽이 기다리고 있다. 해제는 이 비트가 서 있을 때만 깨운다(평소 해제에 시스템 콜이 없다). */
        constexpr uint32 kCloseWaiterBit = 1u << 31;

        /**
         * @brief 닫힌 출처의 블록 칸 값입니다. 할당된 블록의 주소일 수 없다(정렬이 맞지 않는다) — LLVM `DenseMap` 의 tombstone 키와 같은 자리.
         * @details 닫힘을 따로 깃발로 두면 출처마다 8 B 가 는다.
         */
        SubscriptionControl* getClosedControl() noexcept
        {
            return reinterpret_cast<SubscriptionControl*>( uintptr_t{ 1 } );
        }

        /** @brief 이 스레드가 지금 안에 든 `releaseSubscription` 한 겹입니다(스택에 둔다). */
        struct ReleaseFrame
        {
            const SubscriptionControl* _pControl;
            const ReleaseFrame*        _pOuter;
        };

        /** @brief 이 스레드가 지금 안에 든 가장 안쪽 해제입니다. 자기 해제 안에서 출처를 닫는 스레드가 자기를 기다리지 않게 센다(`LockedMulticastDelegate` 의 방송 깊이와 같은 규칙). */
        thread_local const ReleaseFrame* t_pCurrentRelease{ nullptr };

        /** @brief 이 스레드가 안에 든 @p pControl 의 해제 수입니다. */
        uint32 countOwnReleases( const SubscriptionControl* pControl ) noexcept
        {
            uint32 ownReleaseCount = 0;
            for ( const ReleaseFrame* pFrame = t_pCurrentRelease; pFrame != nullptr; pFrame = pFrame->_pOuter )
            {
                if ( pFrame->_pControl == pControl )
                    ++ownReleaseCount;
            }
            return ownReleaseCount;
        }
    } // namespace

    // ------------------------------------------------------------------------------
    // SubscriptionControl
    // ------------------------------------------------------------------------------
    void SubscriptionControl::releaseReference() noexcept
    {
        if ( _refCount.fetch_sub( 1, std::memory_order_acq_rel ) != 1 )
            return;
        const unique_ptr<SubscriptionControl> pOwned{ this }; // `issueSubscription` 의 `make_unique` 와 짝
    }

    bool SubscriptionControl::isLinkedLocked( const Subscription& subscription ) const noexcept
    {
        return subscription._pPrev != nullptr || _pHead == &subscription;
    }

    void SubscriptionControl::unlinkLocked( Subscription& subscription ) noexcept
    {
        if ( subscription._pPrev != nullptr )
            subscription._pPrev->_pNext = subscription._pNext;
        else
            _pHead = subscription._pNext;
        if ( subscription._pNext != nullptr )
            subscription._pNext->_pPrev = subscription._pPrev;
        subscription._pPrev = nullptr;
        subscription._pNext = nullptr;
        subscription._id    = 0;
    }

    void SubscriptionControl::unlinkAllLocked() noexcept
    {
        Subscription* pCurrent = _pHead;
        while ( pCurrent != nullptr )
        {
            Subscription* const pNext = pCurrent->_pNext;
            pCurrent->_pPrev          = nullptr;
            pCurrent->_pNext          = nullptr;
            pCurrent->_id             = 0;
            pCurrent                  = pNext;
        }
        _pHead = nullptr;
    }

    void SubscriptionControl::waitForReleases( uint32 ownReleaseCount ) noexcept
    {
        if ( ( _releasingCount.load( std::memory_order_acquire ) & ~kCloseWaiterBit ) <= ownReleaseCount )
            return;
        // 끝까지 기다린다 — 포기하면 해제가 소멸된 파생을 만진다. 표시를 먼저 세우고 수를 다시 보므로 깨움을 놓치지 않는다(Futex 는 값이 그대로일 때만 잔다).
        (void)_releasingCount.fetch_or( kCloseWaiterBit, std::memory_order_acq_rel );
        for ( ;; )
        {
            const uint32 releasingCount = _releasingCount.load( std::memory_order_acquire );
            if ( ( releasingCount & ~kCloseWaiterBit ) <= ownReleaseCount )
                return;
            Futex::wait( _releasingCount, releasingCount );
        }
    }

    // ------------------------------------------------------------------------------
    // Subscription
    // ------------------------------------------------------------------------------
    void Subscription::reset() noexcept
    {
        SubscriptionControl* const pControl = _pControl;
        if ( pControl == nullptr )
            return;
        _pControl = nullptr;

        SubscriptionSource* pSource{ nullptr };
        uint64              id{ 0 };
        {
            ScopedLock<SpinLock> lock{ pControl->_lock };
            if ( pControl->isLinkedLocked( *this ) )
            {
                id      = _id;
                pSource = pControl->_pSource; // 목록에 있으면 출처는 열려 있다(닫기가 목록을 비운다)
                pControl->unlinkLocked( *this );
                // 잠금 안에서 올린다 — 뒤에 잠금을 잡는 닫기가 이 수를 반드시 본다
                pControl->_releasingCount.fetch_add( 1, std::memory_order_relaxed );
            }
        }
        if ( pSource != nullptr )
        {
            const ReleaseFrame frame{ pControl, t_pCurrentRelease };
            t_pCurrentRelease = &frame;
            pSource->releaseSubscription( id );
            t_pCurrentRelease = frame._pOuter;
            // 해제가 끝났다 — 이 뒤로 출처를 만지지 않는다. 블록은 이 구독의 참조가 살려 둔다.
            const uint32 previousCount = pControl->_releasingCount.fetch_sub( 1, std::memory_order_acq_rel );
            if ( ( previousCount & kCloseWaiterBit ) != 0 )
                Futex::wakeAll( pControl->_releasingCount );
        }
        pControl->releaseReference();
    }

    bool Subscription::isActive() const noexcept
    {
        SubscriptionControl* const pControl = _pControl;
        if ( pControl == nullptr )
            return false;
        ScopedLock<SpinLock> lock{ pControl->_lock };
        return pControl->isLinkedLocked( *this );
    }

    void Subscription::takeFrom( Subscription& other ) noexcept
    {
        SubscriptionControl* const pControl = other._pControl;
        if ( pControl == nullptr )
            return;
        ScopedLock<SpinLock> lock{ pControl->_lock };
        _pControl       = pControl; // 블록 참조가 그대로 옮겨 온다
        other._pControl = nullptr;
        if ( pControl->isLinkedLocked( other ) )
        {
            _pPrev = other._pPrev;
            _pNext = other._pNext;
            _id    = other._id;
            if ( _pPrev != nullptr )
                _pPrev->_pNext = this;
            else
                pControl->_pHead = this;
            if ( _pNext != nullptr )
                _pNext->_pPrev = this;
        }
        other._pPrev = nullptr;
        other._pNext = nullptr;
        other._id    = 0;
    }

    // ------------------------------------------------------------------------------
    // SubscriptionSource
    // ------------------------------------------------------------------------------
    Subscription SubscriptionSource::issueSubscription( uint64 id ) noexcept
    {
        SubscriptionControl* pControl = _pControl.load( std::memory_order_acquire );
        SW_LOW_LEVEL_ASSERT( pControl != getClosedControl(), "issueSubscription on a closed SubscriptionSource" );
        if ( pControl == nullptr )
        {
            // 여러 스레드 출처(`AtomicSlot` · 로그 리스너)는 첫 구독을 동시에 낼 수 있다 — 먼저 건 블록이 이기고 진 쪽은 제 블록을 지운다.
            unique_ptr<SubscriptionControl> pCreated = make_unique<SubscriptionControl>();
            pCreated->_pSource                       = this;
            if ( _pControl.compare_exchange_strong( pControl, pCreated.get(), std::memory_order_acq_rel, std::memory_order_acquire ) )
                pControl = pCreated.release();
        }
        pControl->_refCount.fetch_add( 1, std::memory_order_relaxed );

        Subscription subscription;
        subscription._pControl = pControl;
        subscription._id       = id;
        {
            ScopedLock<SpinLock> lock{ pControl->_lock };
            subscription._pNext = pControl->_pHead;
            if ( pControl->_pHead != nullptr )
                pControl->_pHead->_pPrev = &subscription;
            pControl->_pHead = &subscription;
        }
        // 반환이 옮기면(NRVO 가 아니면) 이동 생성자가 목록 자리를 새 객체로 고친다.
        return subscription;
    }

    uint64 SubscriptionSource::findIssuedID( const Subscription& subscription ) const noexcept
    {
        SubscriptionControl* const pControl = _pControl.load( std::memory_order_acquire );
        if ( pControl == nullptr || pControl == getClosedControl() || subscription._pControl != pControl )
            return 0;
        ScopedLock<SpinLock> lock{ pControl->_lock };
        return pControl->isLinkedLocked( subscription ) ? subscription._id : 0;
    }

    void SubscriptionSource::detachSubscription( uint64 id ) noexcept
    {
        SubscriptionControl* const pControl = _pControl.load( std::memory_order_acquire );
        if ( pControl == nullptr || pControl == getClosedControl() )
            return;
        ScopedLock<SpinLock> lock{ pControl->_lock };
        for ( Subscription* pCurrent = pControl->_pHead; pCurrent != nullptr; pCurrent = pCurrent->_pNext )
        {
            if ( pCurrent->_id == id )
            {
                pControl->unlinkLocked( *pCurrent );
                return;
            }
        }
    }

    void SubscriptionSource::detachAllSubscriptions() noexcept
    {
        SubscriptionControl* const pControl = _pControl.load( std::memory_order_acquire );
        if ( pControl == nullptr || pControl == getClosedControl() )
            return;
        ScopedLock<SpinLock> lock{ pControl->_lock };
        pControl->unlinkAllLocked();
    }

    void SubscriptionSource::closeSubscriptions() noexcept
    {
        SubscriptionControl* const pControl = _pControl.exchange( getClosedControl(), std::memory_order_acq_rel );
        if ( pControl == nullptr || pControl == getClosedControl() )
            return;
        {
            ScopedLock<SpinLock> lock{ pControl->_lock };
            pControl->_pSource = nullptr; // 이 뒤로 어느 구독도 해제를 시작하지 않는다
            pControl->unlinkAllLocked();
        }
        pControl->waitForReleases( countOwnReleases( pControl ) );
        pControl->releaseReference();
    }

    void SubscriptionSource::takeSubscriptionsFrom( SubscriptionSource& other ) noexcept
    {
        SubscriptionControl* const pControl = other._pControl.exchange( nullptr, std::memory_order_acq_rel );
        if ( pControl == nullptr )
            return;
        if ( pControl == getClosedControl() )
        {
            other._pControl.store( pControl, std::memory_order_release ); // 닫힌 출처는 닫힌 채로 둔다 — 데려올 구독이 없다
            return;
        }
        {
            ScopedLock<SpinLock> lock{ pControl->_lock };
            pControl->_pSource = this;
        }
        _pControl.store( pControl, std::memory_order_release );
    }

    void SubscriptionSource::dropControl() noexcept
    {
        SubscriptionControl* const pControl = _pControl.load( std::memory_order_acquire );
        if ( pControl == nullptr || pControl == getClosedControl() )
            return;
        _pControl.store( nullptr, std::memory_order_release ); // 옮기는 출처는 한 스레드 형식뿐이라 이 칸을 다투는 쪽이 없다
        {
            ScopedLock<SpinLock> lock{ pControl->_lock };
            pControl->_pSource = nullptr;
            pControl->unlinkAllLocked();
        }
        pControl->releaseReference();
    }

    void SubscriptionSource::checkClosed() const noexcept
    {
        if ( _pControl.load( std::memory_order_acquire ) != getClosedControl() )
            SW_LOW_LEVEL_FATAL( "SubscriptionSource destroyed without closeSubscriptions - call it first in the derived destructor" );
    }
} // namespace sw
