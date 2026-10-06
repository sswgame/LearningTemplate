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

    void MonotonicClock::sleepUntilNanoseconds( int64 deadlineNanoseconds ) noexcept
    {
        for ( ;; )
        {
            const int64 remaining = deadlineNanoseconds - nowNanoseconds();
            if ( remaining <= 0 )
                return;
#if defined( SW_PLATFORM_WINDOWS )
            // 스레드마다 타이머 하나(만들기 비용을 틱마다 내지 않는다). 프로세스 끝에 OS 가 거둔다.
            thread_local HANDLE s_hTimer = CreateWaitableTimerExW( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
            if ( s_hTimer != nullptr )
            {
                LARGE_INTEGER dueTime{};
                dueTime.QuadPart = -( remaining / 100 ); // 음수 = 상대, 100 ns 단위
                if ( dueTime.QuadPart == 0 )
                    dueTime.QuadPart = -1;
                if ( SetWaitableTimer( s_hTimer, &dueTime, 0, nullptr, nullptr, FALSE ) != FALSE )
                {
                    (void)WaitForSingleObject( s_hTimer, INFINITE );
                    continue;
                }
            }
            Sleep( static_cast<DWORD>( ( remaining + 999999 ) / 1000000 ) );
#else
            timespec request{};
            request.tv_sec  = static_cast<time_t>( remaining / constant::kNanosecondsPerSecond );
            request.tv_nsec = static_cast<decltype( request.tv_nsec )>( remaining % constant::kNanosecondsPerSecond );
            (void)clock_nanosleep( CLOCK_MONOTONIC, 0, &request, nullptr ); // 신호에 깨면 루프가 남은 시간을 다시 잰다
#endif
        }
    }
} // namespace sw
