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
    //    조건 변수용 getStdMutex 는 되도록 쓰지 말 것
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
        void notifyIntended();
        void notifyAcquired();
        void notifyReleased();

        std::mutex _mutex;
    };

} // namespace sw

#include "Core/Concurrency/DeadlockDetector.h"

namespace sw
{
#if defined( SW_ENABLE_DEADLOCK_DETECTION )
    namespace
    {
        struct MutexReentryGuardInternal
        {
            static bool& flag()
            {
                static thread_local bool t_bInHook = false;
                return t_bInHook;
            }
        };
    } // namespace

    struct MutexReentryGuard
    {
        MutexReentryGuard()
            : _bEntered{ MutexReentryGuardInternal::flag() == false }
        {
            if ( _bEntered )
                MutexReentryGuardInternal::flag() = true;
        }

        ~MutexReentryGuard()
        {
            if ( _bEntered )
                MutexReentryGuardInternal::flag() = false;
        }

        explicit operator bool() const { return _bEntered; }

        bool _bEntered;
    };

    inline void mutex::notifyIntended()
    {
        MutexReentryGuard guard;
        if ( guard )
        {
            DeadlockDetector* pDetector = DeadlockDetector::getActive();
            if ( pDetector != nullptr )
                pDetector->recordLockIntended( this );
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
    inline void mutex::notifyIntended() {}
    inline void mutex::notifyAcquired() {}
    inline void mutex::notifyReleased() {}
#endif

    inline void mutex::lock()
    {
        notifyIntended();
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
