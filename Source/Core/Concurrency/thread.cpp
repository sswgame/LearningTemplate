#include "pch.h"

#include "Core/Concurrency/thread.h"

#include "Core/Concurrency/mutex.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
    #include <sched.h>
    #include <sys/resource.h>
#else
    #error "Unsupported platform"
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
#elif defined( SW_PLATFORM_POSIX )
                return false;
#else
    #error "Unsupported platform"
#endif
            }

            /** @brief 스레드 @p threadID 의 합류 키입니다. 홀수라 정렬된 `mutex` 주소와 겹치지 않는다. */
            static void* getJoinKey( std::thread::id threadID )
            {
                return reinterpret_cast<void*>( ( std::hash<std::thread::id>{}( threadID ) << 1 ) | 1 );
            }

            /** @brief 잠금 관찰자에 사건 하나를 알립니다. 검사 구성에서는 `mutex` 와 같은 재진입 가드 안에서 부른다. */
            static void notify( void ( ILockObserver::*pRecord )( void* ), void* pKey )
            {
#if defined( SW_DO_CHECKS )
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

    uint32 thread::getUsableProcessorCount()
    {
        uint32 count = 0;
#if defined( SW_PLATFORM_WINDOWS )
        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask  = 0;
        if ( GetProcessAffinityMask( GetCurrentProcess(), &processMask, &systemMask ) != FALSE )
        {
            for ( uint64 mask = static_cast<uint64>( processMask ); mask != 0; mask &= mask - 1 )
            {
                ++count;
            }
        }
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
        cpu_set_t set;
        CPU_ZERO( &set );
        if ( sched_getaffinity( 0, sizeof( set ), &set ) == 0 )
            count = static_cast<uint32>( CPU_COUNT( &set ) );
#elif defined( SW_PLATFORM_WEB )
        count = std::thread::hardware_concurrency(); // navigator.hardwareConcurrency — 브라우저에는 친화도 마스크가 없다
#else
    #error "Unsupported platform"
#endif
        if ( count == 0 )
            count = std::thread::hardware_concurrency();
        return count == 0 ? 1 : count;
    }

    void ThreadUtil::setCurrentThreadTimeCritical()
    {
#if defined( SW_PLATFORM_WINDOWS )
        (void)SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL ); // 힌트다(선언 설명)
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
        constexpr int32 kAudioPriority = -16;                 // Android `THREAD_PRIORITY_AUDIO`. 0 번(자기)의 nice 는 리눅스에서 스레드마다다
        (void)setpriority( PRIO_PROCESS, 0, kAudioPriority ); // 힌트다(선언 설명)
#elif defined( SW_PLATFORM_WEB )
        // 브라우저 워커에는 우선순위가 없다
#else
    #error "Unsupported platform"
#endif
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
