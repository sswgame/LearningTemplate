#include "pch.h"

#include "Core/Time/CpuClock.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
#elif defined( SW_PLATFORM_MACOS )
    #include <mach/mach_time.h>
#else
    #include <time.h>
#endif

namespace sw
{
    namespace
    {
        struct CpuClockInternal
        {
#if defined( SW_PLATFORM_MACOS )
            /** @brief `mach_absolute_time` 의 틱 → 나노초 비율입니다(프로세스에서 한 번 읽습니다). */
            static const mach_timebase_info_data_t& getTimebase() noexcept
            {
                static const mach_timebase_info_data_t s_timebase = []()
                {
                    mach_timebase_info_data_t info{};
                    mach_timebase_info( &info );
                    return info;
                }();
                return s_timebase;
            }
#endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    int64 CpuClock::readCounter() noexcept
    {
#if defined( SW_PLATFORM_WINDOWS )
        LARGE_INTEGER counter{};
        QueryPerformanceCounter( &counter );
        return static_cast<int64>( counter.QuadPart );
#elif defined( SW_PLATFORM_LINUX )
        timespec time{};
        clock_gettime( CLOCK_MONOTONIC, &time );
        return static_cast<int64>( time.tv_sec ) * constant::kNanosecondsPerSecond + static_cast<int64>( time.tv_nsec );
#elif defined( SW_PLATFORM_MACOS )
        const mach_timebase_info_data_t& timebase = CpuClockInternal::getTimebase();
        return static_cast<int64>( mach_absolute_time() * timebase.numer / timebase.denom );
#else
    #error "Unsupported platform"
#endif
    }

    int64 CpuClock::getCountsPerSecond() noexcept
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
        return constant::kNanosecondsPerSecond; // Linux · macOS 는 readCounter 가 이미 나노초다
#endif
    }

    int64 CpuClock::nowNanoseconds() noexcept
    {
        return countsToNanoseconds( readCounter(), getCountsPerSecond() );
    }
} // namespace sw
