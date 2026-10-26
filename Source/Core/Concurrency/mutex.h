/**
 * @file mutex.h
 * @brief 엔진의 잠금 타입(`sw::mutex` · `sw::shared_mutex`)과 잠금 가드(`ScopedLock` · `ScopedSharedLock` · `UniqueLock`, `ScopedLock.h`)입니다.
 * @details 잠금은 잎 잠금이다 — 쥔 채 로그 · 콜백 · 장치 · 외부 호출을 하지 않는다(결정 5-3). 그 규칙을 세 겹으로 지킨다.
 *          - 형식: 잠금 타입은 `SW_CAPABILITY` 이고(`ThreadSafety.h`), clang 빌드는 `-Wthread-safety` 로 `SW_GUARDED_BY` · `SW_REQUIRES_LOCK` 계약을 컴파일 때 본다.
 *          - 실행(Debug): 이 스레드가 쥔 잠금 수(`mutex::getHeldLockDepth`)를 세고 로거가 0 을 단언한다 — 호출 그래프를 건너는 위반은 형식이 못 본다.
 *          - 교착 검출(`SW_ENABLE_DEADLOCK_DETECTION`): 독점 잠금의 시도 · 획득 · 해제를 관찰자에 알린다.
 *          `std::mutex` · `std::shared_mutex` 를 직접 쓰면 셋이 모두 꺼지므로 `CheckStdMutex` 게이트가 막는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadSafety.h"

#include <mutex>
#include <shared_mutex>

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) ILockObserver — 잠금 시도 · 획득 · 해제를 듣는 쪽(교착 검출기 `DeadlockDetector`)
    //    mutex · thread 는 이 인터페이스만 안다. 검출기는 컨테이너 · 호출 스택을 쓰므로 위층(Diagnostics)에 있다
    // ------------------------------------------------------------------------------
    /**
     * @brief 대기 사건을 받는 인터페이스입니다 — `mutex` 의 잠금과 `thread` 의 합류(스레드가 자기 합류 키를 잡고 있다가 끝날 때 놓는다). `mutex::registerLockObserver` 로 겁니다.
     * @details `mutex` 는 `SW_ENABLE_DEADLOCK_DETECTION` 빌드에서만 부르고, `thread` 는 늘 부른다(시작 · 끝 · 합류는 드물다). 탐지 빌드에서는
     *          재진입 가드(`MutexReentryGuard`) 안에서 불리므로 구현이 자기 `mutex` 를 잡아도 다시 불리지 않습니다. 키 `pLock` 은 주소로만 쓴다.
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
    // 2) mutex — std::mutex 래퍼. 교착 검출 빌드에서 잠금 사이클을 탐지하고 Debug 에서 쥔 잠금 수를 센다
    // ------------------------------------------------------------------------------
    /**
     * @brief std::mutex 를 감싸 교착 검출 빌드에서 잠금 사이클을 탐지하고, Debug 에서 이 스레드가 쥔 잠금 수를 셉니다.
     */
    class SW_API SW_CAPABILITY( "mutex" ) mutex
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
        void lock() SW_ACQUIRE_LOCK();

        /** @brief 락을 한 번만 시도합니다. 얻으면 true 입니다. */
        bool try_lock() SW_TRY_ACQUIRE_LOCK( true );

        /** @brief 잡고 있는 락을 풉니다. */
        void unlock() SW_RELEASE_LOCK();

        /**
         * @brief 잠금 관찰자를 겁니다. 이미 다른 관찰자가 걸려 있으면 아무것도 하지 않습니다.
         * @param pObserver 뗄 때까지 살아 있어야 합니다.
         */
        static void registerLockObserver( ILockObserver* pObserver );
        /** @brief @p pObserver 가 걸린 관찰자이면 뗍니다. */
        static void unregisterLockObserver( ILockObserver* pObserver );
        /** @brief 걸린 잠금 관찰자입니다. 없으면 nullptr 입니다. */
        static ILockObserver* getLockObserver();

        /**
         * @brief 이 스레드가 지금 쥔 `mutex` · `shared_mutex` 수입니다(공유 잠금 포함). Debug 만 세고 다른 구성은 늘 0 입니다.
         * @details 관찰자(교착 검출기)의 훅 안에서는 0 입니다 — 검출기의 보고는 잠금 진단 자체라 잎 규칙 밖이다. 로거가 0 을 단언합니다.
         */
        static uint32 getHeldLockDepth();

    private:
        friend class shared_mutex;

#if defined( SW_DEBUG )
        /** @brief 이 스레드가 쥔 잠금 수를 @p delta 만큼 바꿉니다. 수는 Engine 이미지의 TU 하나(`mutex.cpp`)에 있다 — 헤더에 두면 모듈마다 따로 센다. */
        static void changeHeldLockDepth( int32 delta );
#else
        static void changeHeldLockDepth( int32 /*delta*/ ) {}
#endif

        static void notifyLockAttempt( void* pLock );
        static void notifyAcquired( void* pLock );
        static void notifyReleased( void* pLock );

        std::mutex _mutex;
    };

    // ------------------------------------------------------------------------------
    // 3) shared_mutex — std::shared_mutex 래퍼. 독점 잠금은 mutex 와 같이 알리고, 공유 잠금은 수만 센다
    // ------------------------------------------------------------------------------
    /**
     * @brief std::shared_mutex 를 감쌉니다. 독점 잠금은 `mutex` 와 같이 관찰자에 알리고 쥔 잠금 수를 셉니다.
     * @details 공유 잠금은 관찰자에 알리지 않습니다 — 검출기는 주인이 하나인 잠금만 모델링해, 읽기 둘을 알리면 주인 표가 서로 덮인다. 쥔 잠금 수는 셉니다.
     */
    class SW_CAPABILITY( "shared_mutex" ) shared_mutex
    {
    public:
        /** @brief 내부 std::shared_mutex 만 준비합니다. */
        shared_mutex() = default;
        /** @brief 잠금이 풀린 상태에서 파괴해야 합니다. */
        ~shared_mutex() = default;

        /** @brief 복사를 금지합니다. */
        shared_mutex( const shared_mutex& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        shared_mutex& operator=( const shared_mutex& ) = delete;

        /** @brief 독점 락을 얻을 때까지 기다립니다. */
        void lock() SW_ACQUIRE_LOCK();
        /** @brief 독점 락을 한 번만 시도합니다. 얻으면 true 입니다. */
        bool try_lock() SW_TRY_ACQUIRE_LOCK( true );
        /** @brief 독점 락을 풉니다. */
        void unlock() SW_RELEASE_LOCK();

        /** @brief 공유 락을 얻을 때까지 기다립니다. */
        void lock_shared() SW_ACQUIRE_SHARED_LOCK();
        /** @brief 공유 락을 한 번만 시도합니다. 얻으면 true 입니다. */
        bool try_lock_shared() SW_TRY_ACQUIRE_SHARED_LOCK( true );
        /** @brief 공유 락을 풉니다. */
        void unlock_shared() SW_RELEASE_SHARED_LOCK();

    private:
        std::shared_mutex _mutex;
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

        /** @brief 이 스레드가 지금 관찰자 훅 안인지입니다. */
        static bool isInHook() { return _s_bInHook; }

        bool _bEntered;

    private:
        /**
         * @brief 이 스레드가 지금 교착 감지 훅 안인지입니다. **프로그램(모듈)에 하나**여야 합니다.
         * @details 주의: 헤더의 익명 네임스페이스에 두면 번역 단위마다 따로 생깁니다(ODR 위반). 그러면 감지기 파일(`DeadlockDetector.cpp`)
         *          의 플래그가 부른 쪽 것과 달라, 감지기 자신의 `_mutex` 가 훅을 다시 타 방금 잡은 락을 또 잡으려다 스스로 멈출 수 있습니다.
         */
        static inline thread_local bool _s_bInHook{ false };
    };

    inline void mutex::notifyLockAttempt( void* pLock )
    {
        MutexReentryGuard guard;
        if ( guard )
        {
            ILockObserver* pObserver = getLockObserver();
            if ( pObserver != nullptr )
                pObserver->recordLockAttempt( pLock );
        }
    }

    inline void mutex::notifyAcquired( void* pLock )
    {
        MutexReentryGuard guard;
        if ( guard )
        {
            ILockObserver* pObserver = getLockObserver();
            if ( pObserver != nullptr )
                pObserver->recordLockAcquired( pLock );
        }
    }

    inline void mutex::notifyReleased( void* pLock )
    {
        MutexReentryGuard guard;
        if ( guard )
        {
            ILockObserver* pObserver = getLockObserver();
            if ( pObserver != nullptr )
                pObserver->recordLockReleased( pLock );
        }
    }
#else
    inline void mutex::notifyLockAttempt( void* /*pLock*/ )
    {
    }
    inline void mutex::notifyAcquired( void* /*pLock*/ )
    {
    }
    inline void mutex::notifyReleased( void* /*pLock*/ )
    {
    }
#endif

    // 잠금 타입 자신의 구현은 주석 없는 원시 잠금(std::mutex)을 부르므로 분석하지 않는다(SW_NO_THREAD_SAFETY_ANALYSIS).
    SW_NO_THREAD_SAFETY_ANALYSIS inline void mutex::lock()
    {
        notifyLockAttempt( this );
        _mutex.lock();
        notifyAcquired( this );
        mutex::changeHeldLockDepth( 1 );
    }

    SW_NO_THREAD_SAFETY_ANALYSIS inline bool mutex::try_lock()
    {
        if ( _mutex.try_lock() == false )
            return false;

        notifyAcquired( this );
        mutex::changeHeldLockDepth( 1 );
        return true;
    }

    SW_NO_THREAD_SAFETY_ANALYSIS inline void mutex::unlock()
    {
        mutex::changeHeldLockDepth( -1 );
        notifyReleased( this );
        _mutex.unlock();
    }

    SW_NO_THREAD_SAFETY_ANALYSIS inline void shared_mutex::lock()
    {
        mutex::notifyLockAttempt( this );
        _mutex.lock();
        mutex::notifyAcquired( this );
        mutex::changeHeldLockDepth( 1 );
    }

    SW_NO_THREAD_SAFETY_ANALYSIS inline bool shared_mutex::try_lock()
    {
        if ( _mutex.try_lock() == false )
            return false;

        mutex::notifyAcquired( this );
        mutex::changeHeldLockDepth( 1 );
        return true;
    }

    SW_NO_THREAD_SAFETY_ANALYSIS inline void shared_mutex::unlock()
    {
        mutex::changeHeldLockDepth( -1 );
        mutex::notifyReleased( this );
        _mutex.unlock();
    }

    SW_NO_THREAD_SAFETY_ANALYSIS inline void shared_mutex::lock_shared()
    {
        _mutex.lock_shared();
        mutex::changeHeldLockDepth( 1 );
    }

    SW_NO_THREAD_SAFETY_ANALYSIS inline bool shared_mutex::try_lock_shared()
    {
        if ( _mutex.try_lock_shared() == false )
            return false;

        mutex::changeHeldLockDepth( 1 );
        return true;
    }

    SW_NO_THREAD_SAFETY_ANALYSIS inline void shared_mutex::unlock_shared()
    {
        mutex::changeHeldLockDepth( -1 );
        _mutex.unlock_shared();
    }
} // namespace sw
