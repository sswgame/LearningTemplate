/**
 * @file mutex.h
 * @brief 데드락 탐지를 내장한 뮤텍스 래퍼입니다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include <mutex>

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) ILockObserver — 잠금 시도 · 획득 · 해제를 듣는 쪽(교착 검출기 `DeadlockDetector`)
    //    mutex 는 이 인터페이스만 안다. 검출기는 컨테이너 · 호출 스택을 쓰므로 위층(Diagnostics)에 있다
    // ------------------------------------------------------------------------------
    /**
     * @brief `mutex` 의 잠금 사건을 받는 인터페이스입니다. `mutex::registerLockObserver` 로 겁니다.
     * @details `SW_ENABLE_DEADLOCK_DETECTION` 빌드에서만 불립니다. 부르는 쪽은 재진입 가드(`MutexReentryGuard`) 안이라,
     *          구현이 자기 `mutex` 를 잡아도 다시 불리지 않습니다.
     */
    class SW_API ILockObserver
    {
    public:
        ILockObserver() = default;
        /** @brief 가상 소멸자입니다. */
        virtual ~ILockObserver() = default;

        /** @brief 복사를 금지합니다. */
        ILockObserver( const ILockObserver& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        ILockObserver& operator=( const ILockObserver& ) = delete;

        /** @brief 락 획득을 시도하기 직전에 불립니다. */
        virtual void recordLockAttempt( void* pLock ) = 0;
        /** @brief 락을 얻은 뒤 불립니다. */
        virtual void recordLockAcquired( void* pLock ) = 0;
        /** @brief 락을 풀기 직전에 불립니다. */
        virtual void recordLockReleased( void* pLock ) = 0;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) mutex — std::mutex 래퍼. 디버그 빌드에서 데드락 사이클을 탐지한다
    // ------------------------------------------------------------------------------
    /**
     * @brief std::mutex 를 감싸 디버그 빌드에서 데드락 사이클을 탐지합니다.
     */
    class SW_API mutex
    {
    public:
        /** @brief 내부 std::mutex 만 준비합니다. */
        mutex() = default;
        /** @brief 잠금이 풀린 상태에서 파괴해야 합니다. */
        ~mutex() = default;

        /** @brief 복사를 금지합니다. */
        mutex( const mutex& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        mutex& operator=( const mutex& ) = delete;

        /** @brief 락을 얻을 때까지 기다립니다. */
        void lock();

        /** @brief 락을 한 번만 시도합니다. 얻으면 true 입니다. */
        bool try_lock();

        /** @brief 잡고 있는 락을 풉니다. */
        void unlock();

        /**
         * @brief 잠금 관찰자를 겁니다. 이미 다른 관찰자가 걸려 있으면 아무것도 하지 않습니다.
         * @param pObserver 뗄 때까지 살아 있어야 합니다.
         */
        static void registerLockObserver( ILockObserver* pObserver );
        /** @brief @p pObserver 가 걸린 관찰자이면 뗍니다. */
        static void unregisterLockObserver( ILockObserver* pObserver );
        /** @brief 걸린 잠금 관찰자입니다. 없으면 nullptr 입니다. */
        static ILockObserver* getLockObserver();

    private:
        void notifyLockAttempt();
        void notifyAcquired();
        void notifyReleased();

        std::mutex _mutex;
    };

} // namespace sw

namespace sw
{
#if defined( SW_ENABLE_DEADLOCK_DETECTION )
    struct MutexReentryGuard
    {
        MutexReentryGuard()
            : _bEntered{ _s_bInHook == false }
        {
            if ( _bEntered )
                _s_bInHook = true;
        }

        ~MutexReentryGuard()
        {
            if ( _bEntered )
                _s_bInHook = false;
        }

        explicit operator bool() const { return _bEntered; }

        bool _bEntered;

    private:
        /**
         * @brief 이 스레드가 지금 교착 감지 훅 안인지입니다. **프로그램(모듈)에 하나**여야 합니다.
         * @details 주의: 헤더의 익명 네임스페이스에 두면 번역 단위마다 따로 생깁니다(ODR 위반). 그러면 감지기 파일(`DeadlockDetector.cpp`)
         *          의 플래그가 부른 쪽 것과 달라, 감지기 자신의 `_mutex` 가 훅을 다시 타 방금 잡은 락을 또 잡으려다 스스로 멈출 수 있습니다.
         */
        static inline thread_local bool _s_bInHook{ false };
    };

    inline void mutex::notifyLockAttempt()
    {
        MutexReentryGuard guard;
        if ( guard )
        {
            ILockObserver* pObserver = getLockObserver();
            if ( pObserver != nullptr )
                pObserver->recordLockAttempt( this );
        }
    }

    inline void mutex::notifyAcquired()
    {
        MutexReentryGuard guard;
        if ( guard )
        {
            ILockObserver* pObserver = getLockObserver();
            if ( pObserver != nullptr )
                pObserver->recordLockAcquired( this );
        }
    }

    inline void mutex::notifyReleased()
    {
        MutexReentryGuard guard;
        if ( guard )
        {
            ILockObserver* pObserver = getLockObserver();
            if ( pObserver != nullptr )
                pObserver->recordLockReleased( this );
        }
    }
#else
    inline void mutex::notifyLockAttempt() {}
    inline void mutex::notifyAcquired() {}
    inline void mutex::notifyReleased() {}
#endif

    inline void mutex::lock()
    {
        notifyLockAttempt();
        _mutex.lock();
        notifyAcquired();
    }

    inline bool mutex::try_lock()
    {
        if ( _mutex.try_lock() == false )
            return false;

        notifyAcquired();
        return true;
    }

    inline void mutex::unlock()
    {
        notifyReleased();
        _mutex.unlock();
    }
} // namespace sw
