#include "pch.h"

#include "Core/Concurrency/Futex.h"

#include "Core/Common/StdHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
#elif defined( SW_PLATFORM_LINUX )
    #include <linux/futex.h>
    #include <sys/syscall.h>
    #include <sys/time.h>
    #include <unistd.h>
#else
    #include <condition_variable>
    #include <mutex>
#endif

namespace sw
{
    namespace
    {
        // `WaitOnAddress` · `futex` 는 주소와 4바이트만 본다. 래퍼가 값 하나만 들고 있어야 래퍼의 주소가 곧 값의 주소가 된다.
        static_assert( sizeof( atomic<uint32> ) == sizeof( uint32 ), "atomic<uint32> must be exactly the 32-bit word" );
        static_assert( std::is_standard_layout_v<atomic<uint32>>, "atomic<uint32> must be standard layout for address waits" );

#if !defined( SW_PLATFORM_WINDOWS ) && !defined( SW_PLATFORM_LINUX )
        /**
         * @brief 주소 대기 기능이 없는 플랫폼의 폴백입니다. 주소 해시로 고른 버킷의 뮤텍스와 조건 변수를 씁니다.
         * @details 깨우는 쪽도 같은 버킷의 뮤텍스를 잡고 알리므로, 잠드는 쪽이 뮤텍스 안에서 값을 다시 확인하고 잠드는 사이에
         *          깨움이 새지 않습니다. 버킷이 겹치면 이유 없이 깨어날 뿐인데, 이는 약속한 동작 안에 있습니다.
         */
        struct FutexFallbackInternal
        {
            static constexpr uint32 kBucketCount = 64;

            struct Bucket
            {
                std::mutex              _mutex;
                std::condition_variable _cv;
            };

            static Bucket& bucketOf( const void* pAddress )
            {
                static Bucket   s_arrBucket[kBucketCount];
                const uintptr_t value = reinterpret_cast<uintptr_t>( pAddress );
                return s_arrBucket[( value >> 2 ) & ( kBucketCount - 1 )];
            }
        };
#endif
    } // namespace
} // namespace sw

namespace sw
{
    void Futex::wait( atomic<uint32>& word, uint32 expected )
    {
#if defined( SW_PLATFORM_WINDOWS )
        WaitOnAddress( &word, &expected, sizeof( uint32 ), INFINITE );
#elif defined( SW_PLATFORM_LINUX )
        syscall( SYS_futex, reinterpret_cast<uint32*>( &word ), FUTEX_WAIT_PRIVATE, expected, nullptr, nullptr, 0 );
#else
        FutexFallbackInternal::Bucket& bucket = FutexFallbackInternal::bucketOf( &word );
        std::unique_lock<std::mutex>   lock{ bucket._mutex };
        if ( word.load( std::memory_order_acquire ) == expected )
            bucket._cv.wait( lock );
#endif
    }

    bool Futex::waitFor( atomic<uint32>& word, uint32 expected, uint32 timeoutMilli )
    {
#if defined( SW_PLATFORM_WINDOWS )
        return WaitOnAddress( &word, &expected, sizeof( uint32 ), timeoutMilli ) != FALSE;
#elif defined( SW_PLATFORM_LINUX )
        struct timespec timeout;
        timeout.tv_sec     = static_cast<time_t>( timeoutMilli / 1000u );
        timeout.tv_nsec    = static_cast<int64>( ( timeoutMilli % 1000u ) * 1000000u );
        const int64 result = syscall( SYS_futex, reinterpret_cast<uint32*>( &word ), FUTEX_WAIT_PRIVATE, expected, &timeout, nullptr, 0 );
        return result == 0 || word.load( std::memory_order_acquire ) != expected;
#else
        FutexFallbackInternal::Bucket& bucket = FutexFallbackInternal::bucketOf( &word );
        std::unique_lock<std::mutex>   lock{ bucket._mutex };
        if ( word.load( std::memory_order_acquire ) != expected )
            return true;
        return bucket._cv.wait_for( lock, std::chrono::milliseconds( timeoutMilli ) ) == std::cv_status::no_timeout;
#endif
    }

    void Futex::wakeOne( atomic<uint32>& word )
    {
#if defined( SW_PLATFORM_WINDOWS )
        WakeByAddressSingle( &word );
#elif defined( SW_PLATFORM_LINUX )
        syscall( SYS_futex, reinterpret_cast<uint32*>( &word ), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0 );
#else
        FutexFallbackInternal::Bucket& bucket = FutexFallbackInternal::bucketOf( &word );
        {
            std::scoped_lock<std::mutex> lock{ bucket._mutex };
        }
        bucket._cv.notify_all(); // 버킷을 나눠 쓰므로 하나만 깨우면 엉뚱한 주소의 대기자가 깨어날 수 있다
#endif
    }

    void Futex::wakeAll( atomic<uint32>& word )
    {
#if defined( SW_PLATFORM_WINDOWS )
        WakeByAddressAll( &word );
#elif defined( SW_PLATFORM_LINUX )
        syscall( SYS_futex, reinterpret_cast<uint32*>( &word ), FUTEX_WAKE_PRIVATE, INT32_MAX, nullptr, nullptr, 0 );
#else
        FutexFallbackInternal::Bucket& bucket = FutexFallbackInternal::bucketOf( &word );
        {
            std::scoped_lock<std::mutex> lock{ bucket._mutex };
        }
        bucket._cv.notify_all();
#endif
    }
} // namespace sw
