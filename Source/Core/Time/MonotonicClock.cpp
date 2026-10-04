#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
#else
    #include <time.h>
#endif

namespace sw
{
    int64 MonotonicClock::readCounter() noexcept
    {
#if defined( SW_PLATFORM_WINDOWS )
        LARGE_INTEGER counter{};
        QueryPerformanceCounter( &counter );
        return static_cast<int64>( counter.QuadPart );
#elif defined( SW_PLATFORM_LINUX )
        timespec time{};
        clock_gettime( CLOCK_MONOTONIC, &time );
        return static_cast<int64>( time.tv_sec ) * constant::kNanosecondsPerSecond + static_cast<int64>( time.tv_nsec );
#else
    #error "Unsupported platform"
#endif
    }

    int64 MonotonicClock::getCountsPerSecond() noexcept
    {
#if defined( SW_PLATFORM_WINDOWS )
        static const int64 s_countsPerSecond = []()
        {
            LARGE_INTEGER frequency{};
            QueryPerformanceFrequency( &frequency );
            return static_cast<int64>( frequency.QuadPart );
        }();
        return s_countsPerSecond;
#else
        return constant::kNanosecondsPerSecond; // Linux 는 readCounter 가 이미 나노초다
#endif
    }

    int64 MonotonicClock::nowNanoseconds() noexcept
    {
        return countsToNanoseconds( readCounter(), getCountsPerSecond() );
    }
} // namespace sw
