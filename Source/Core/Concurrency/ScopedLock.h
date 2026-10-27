/**
 * @file ScopedLock.h
 * @brief 잠금 계약 분석(clang `-Wthread-safety`)이 알아보는 잠금 가드입니다 — `ScopedLock` · `ScopedSharedLock` · `UniqueLock` · `ScopedLockPair` · `ScopedOptionalLock`.
 * @details `std::scoped_lock` · `lock_guard` · `unique_lock` · `shared_lock` 은 MSVC STL 에 능력 주석이 없어 분석이 그 범위를 잠긴 범위로 보지 못한다.
 *          그래서 엔진 코드는 이 가드로 잠그고, `std::` 가드 직접 사용은 `CheckStdMutex` 가 막는다. 잠금 타입(`sw::mutex` · `sw::shared_mutex` · `SpinLock`)과
 *          함께 쓴다 — `lock` · `unlock`(공유는 `lock_shared` · `unlock_shared`)이 있는 타입이면 된다.
 */
#pragma once
#include "Core/Concurrency/ThreadSafety.h"

#include <mutex>

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 잠금 가드 — 분석(-Wthread-safety)이 알아보는 RAII. std::scoped_lock 은 표준 라이브러리에 주석이 없어 분석이 못 본다
    // ------------------------------------------------------------------------------
    /**
     * @brief 생성에서 @p TLock 을 독점으로 잡고 소멸에서 풉니다. `SW_GUARDED_BY` 멤버를 만지는 자리는 이 가드를 씁니다.
     */
    template <class TLock>
    class SW_SCOPED_CAPABILITY ScopedLock
    {
    public:
        explicit ScopedLock( TLock& lock ) SW_ACQUIRE_LOCK( lock )
            : _lock{ lock }
        {
            _lock.lock();
        }

        ~ScopedLock() SW_RELEASE_LOCK()
        {
            _lock.unlock();
        }

        ScopedLock( const ScopedLock& )            = delete;
        ScopedLock& operator=( const ScopedLock& ) = delete;

    private:
        TLock& _lock;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 생성에서 @p TLock 을 공유로 잡고 소멸에서 풉니다.
     */
    template <class TLock>
    class SW_SCOPED_CAPABILITY ScopedSharedLock
    {
    public:
        explicit ScopedSharedLock( TLock& lock ) SW_ACQUIRE_SHARED_LOCK( lock )
            : _lock{ lock }
        {
            _lock.lock_shared();
        }

        ~ScopedSharedLock() SW_RELEASE_LOCK()
        {
            _lock.unlock_shared();
        }

        ScopedSharedLock( const ScopedSharedLock& )            = delete;
        ScopedSharedLock& operator=( const ScopedSharedLock& ) = delete;

    private:
        TLock& _lock;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 범위 안에서 놓았다 다시 잡을 수 있는 독점 가드입니다(`std::unique_lock` 자리). 조건 변수(`condition_variable_any::wait`)에 넘기고,
     *        잠금 밖에서 할 일 앞뒤로 `unlock` · `lock` 합니다. 늦게 잡을 때는 `std::defer_lock` 으로 만들고 `lock` · `try_lock` 합니다.
     * @details 옮기거나(이동) 함수 밖으로 돌려줄 수 없다 — 분석이 잠금이 어디 있는지 범위로 따라가게 한다. 가드 자신의 본문은 쥠 여부가 값(`_bOwned`)이라
     *          분석하지 않는다(SW_NO_THREAD_SAFETY_ANALYSIS) — 쓰는 쪽은 선언의 주석으로 분석된다.
     */
    template <class TLock>
    class SW_SCOPED_CAPABILITY UniqueLock
    {
    public:
        explicit UniqueLock( TLock& lock ) SW_ACQUIRE_LOCK( lock ) SW_NO_THREAD_SAFETY_ANALYSIS
            : _lock{ lock },
              _bOwned{ true }
        {
            _lock.lock();
        }

        UniqueLock( TLock& lock, std::defer_lock_t ) SW_EXCLUDES_LOCK( lock ) SW_NO_THREAD_SAFETY_ANALYSIS
            : _lock{ lock },
              _bOwned{ false }
        {
        }

        ~UniqueLock() SW_RELEASE_LOCK() SW_NO_THREAD_SAFETY_ANALYSIS
        {
            if ( _bOwned )
                _lock.unlock();
        }

        UniqueLock( const UniqueLock& )            = delete;
        UniqueLock& operator=( const UniqueLock& ) = delete;

        void lock() SW_ACQUIRE_LOCK() SW_NO_THREAD_SAFETY_ANALYSIS
        {
            _lock.lock();
            _bOwned = true;
        }

        [[nodiscard]] bool try_lock() SW_TRY_ACQUIRE_LOCK( true ) SW_NO_THREAD_SAFETY_ANALYSIS
        {
            _bOwned = _lock.try_lock();
            return _bOwned;
        }

        void unlock() SW_RELEASE_LOCK() SW_NO_THREAD_SAFETY_ANALYSIS
        {
            _bOwned = false;
            _lock.unlock();
        }

        bool owns_lock() const { return _bOwned; }

    private:
        TLock& _lock;
        bool   _bOwned;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 같은 종류의 잠금 둘을 교착 없이(`std::lock`) 독점으로 잡고 소멸에서 풉니다(`std::scoped_lock<A, A>` 자리).
     */
    template <class TLock>
    class SW_SCOPED_CAPABILITY ScopedLockPair
    {
    public:
        ScopedLockPair( TLock& first, TLock& second ) SW_ACQUIRE_LOCK( first, second ) SW_NO_THREAD_SAFETY_ANALYSIS
            : _first{ first },
              _second{ second }
        {
            std::lock( _first, _second );
        }

        ~ScopedLockPair() SW_RELEASE_LOCK() SW_NO_THREAD_SAFETY_ANALYSIS
        {
            _first.unlock();
            _second.unlock();
        }

        ScopedLockPair( const ScopedLockPair& )            = delete;
        ScopedLockPair& operator=( const ScopedLockPair& ) = delete;

    private:
        TLock& _first;
        TLock& _second;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 있을 수도 없을 수도 있는 잠금(@p pLock 이 null 이면 아무것도 하지 않는다)을 범위 동안 잡습니다.
     * @details 백엔드마다 다른 바깥 잠금(예: Vulkan 큐 제출 잠금, 다른 백엔드는 없음)을 인터페이스가 `TLock*` 로 내줄 때 쓴다 —
     *          빈 `std::unique_lock` 을 값으로 돌려주던 자리다. 잡을지가 실행 중에 정해지므로 분석에는 능력으로 알리지 않는다(이 잠금이 지키는
     *          `SW_GUARDED_BY` 멤버를 두지 않는 바깥 잠금에만 쓴다).
     */
    template <class TLock>
    class ScopedOptionalLock
    {
    public:
        explicit ScopedOptionalLock( TLock* pLock ) SW_NO_THREAD_SAFETY_ANALYSIS
            : _pLock{ pLock }
        {
            if ( _pLock != nullptr )
                _pLock->lock();
        }

        ~ScopedOptionalLock() SW_NO_THREAD_SAFETY_ANALYSIS
        {
            if ( _pLock != nullptr )
                _pLock->unlock();
        }

        ScopedOptionalLock( const ScopedOptionalLock& )            = delete;
        ScopedOptionalLock& operator=( const ScopedOptionalLock& ) = delete;

    private:
        TLock* const _pLock;
    };
} // namespace sw
