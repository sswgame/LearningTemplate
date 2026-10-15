#include "pch.h"

#include "Core/Concurrency/thread.h"

#include "Core/Concurrency/mutex.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
#endif

namespace sw
{
    namespace
    {
        struct ThreadInternal
        {
#if defined( SW_PLATFORM_WINDOWS )
            /** @brief PEB 안 `LoaderLock`(RTL_CRITICAL_SECTION*)의 위치입니다. 64비트 PEB 의 배치(x64 · ARM64 같음)이며 공개 헤더에는 이름이 없다. */
            static constexpr size_t kPebLoaderLockOffset = 0x110;
            /** @brief TEB 안 `ProcessEnvironmentBlock` 의 위치입니다(64비트). */
            static constexpr uint32 kTebProcessEnvironmentBlockOffset = 0x60;
#endif

            /** @brief 이 스레드가 로더 락을 쥐고 있으면(DllMain · DLL 언로드 중의 정적 소멸자) true 입니다. 리눅스는 늘 false 입니다. */
            static bool isLoaderLockHeldByThisThread()
            {
#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_X64 )
                const uint8* pPeb = reinterpret_cast<const uint8*>( __readgsqword( kTebProcessEnvironmentBlockOffset ) );
    #else
                const uint8* pPeb = reinterpret_cast<const uint8*>( __readx18qword( kTebProcessEnvironmentBlockOffset ) );
    #endif
                const RTL_CRITICAL_SECTION* pLoaderLock = *reinterpret_cast<RTL_CRITICAL_SECTION* const*>( pPeb + kPebLoaderLockOffset );
                return pLoaderLock != nullptr && reinterpret_cast<uintptr_t>( pLoaderLock->OwningThread ) == static_cast<uintptr_t>( GetCurrentThreadId() );
#else
                return false;
#endif
            }

            /** @brief 스레드 @p threadID 의 합류 키입니다. 홀수라 정렬된 `mutex` 주소와 겹치지 않는다. */
            static void* getJoinKey( std::thread::id threadID )
            {
                return reinterpret_cast<void*>( ( std::hash<std::thread::id>{}( threadID ) << 1 ) | 1 );
            }

            /** @brief 잠금 관찰자에 사건 하나를 알립니다. 탐지 빌드에서는 `mutex` 와 같은 재진입 가드 안에서 부른다. */
            static void notify( void ( ILockObserver::*pRecord )( void* ), void* pKey )
            {
#if defined( SW_ENABLE_DEADLOCK_DETECTION )
                MutexReentryGuard guard;
                if ( guard._bEntered == false )
                    return;
#endif
                ILockObserver* pObserver = mutex::getLockObserver();
                if ( pObserver != nullptr )
                    ( pObserver->*pRecord )( pKey );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void thread::join()
    {
        if ( ThreadInternal::isLoaderLockHeldByThisThread() )
        {
            // 로더 락 안의 join 은 끝나는 스레드가 같은 락을 기다려 영원히 멈춘다 — 단언하고 떼어 프로세스를 살린다.
            SW_ASSERT( false && "thread::join under the loader lock (DllMain or a module static destructor) - join from an owner with explicit lifetime" );
            _thread.detach();
            return;
        }
        void* const pKey = ThreadInternal::getJoinKey( _thread.get_id() );
        ThreadInternal::notify( &ILockObserver::recordLockAttempt, pKey );
        _thread.join();
        ThreadInternal::notify( &ILockObserver::recordLockAcquired, pKey );
        ThreadInternal::notify( &ILockObserver::recordLockReleased, pKey );
    }

    void thread::notifyStarted()
    {
        ThreadInternal::notify( &ILockObserver::recordLockAcquired, ThreadInternal::getJoinKey( std::this_thread::get_id() ) );
    }

    void thread::notifyExiting()
    {
        ThreadInternal::notify( &ILockObserver::recordLockReleased, ThreadInternal::getJoinKey( std::this_thread::get_id() ) );
    }
} // namespace sw
