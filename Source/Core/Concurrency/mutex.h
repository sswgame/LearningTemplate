/**
 * @file mutex.h
 * @brief 데드락 탐지를 내장한 뮤텍스 래퍼입니다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include <mutex>

namespace sw
{
    class DeadlockDetector;

    // ------------------------------------------------------------------------------
    // 1) mutex — std::mutex 래퍼. 디버그 빌드에서 데드락 사이클을 탐지한다
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

    private:
        void notifyLockAttempt();
        void notifyAcquired();
        void notifyReleased();

        std::mutex _mutex;
    };

} // namespace sw

#include "Core/Concurrency/DeadlockDetector.h"

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
            DeadlockDetector* pDetector = DeadlockDetector::getActive();
            if ( pDetector != nullptr )
                pDetector->recordLockAttempt( this );
        }
    }

    inline void mutex::notifyAcquired()
    {
        MutexReentryGuard guard;
        if ( guard )
        {
            DeadlockDetector* pDetector = DeadlockDetector::getActive();
            if ( pDetector != nullptr )
                pDetector->recordLockAcquired( this );
        }
    }

    inline void mutex::notifyReleased()
    {
        MutexReentryGuard guard;
        if ( guard )
        {
            DeadlockDetector* pDetector = DeadlockDetector::getActive();
            if ( pDetector != nullptr )
                pDetector->recordLockReleased( this );
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
