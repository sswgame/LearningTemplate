#include "pch.h"

#include "Core/Time/WallClock.h"

#include <chrono>
#include <ctime>

namespace sw
{
    UnixMilliseconds WallClock::nowUnixMilliseconds() noexcept
    {
        const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
        return UnixMilliseconds{ static_cast<int64>( std::chrono::duration_cast<std::chrono::milliseconds>( sinceEpoch ).count() ) };
    }

    bool WallClock::toLocalCalendar( int64 unixSeconds, LocalCalendarTime& outCalendar ) noexcept
    {
        // 재진입 가능한 변형만 쓴다 — `localtime` 은 프로세스에 하나뿐인 정적 버퍼에 쓴다(TSan 이 로그 스레드 사이 레이스로 잡았다).
        const std::time_t timeSeconds = static_cast<std::time_t>( unixSeconds );
        std::tm           calendar{};
#if defined( SW_PLATFORM_WINDOWS )
        const bool bConverted = localtime_s( &calendar, &timeSeconds ) == 0;
#else
        const bool bConverted = localtime_r( &timeSeconds, &calendar ) != nullptr;
#endif
        if ( bConverted == false )
            return false;
        outCalendar._year   = calendar.tm_year + 1900;
        outCalendar._month  = calendar.tm_mon + 1;
        outCalendar._day    = calendar.tm_mday;
        outCalendar._hour   = calendar.tm_hour;
        outCalendar._minute = calendar.tm_min;
        outCalendar._second = calendar.tm_sec;
        return true;
    }
} // namespace sw
