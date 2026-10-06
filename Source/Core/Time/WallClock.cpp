#include "pch.h"

#include "Core/Time/WallClock.h"

#include <chrono>

namespace sw
{
    int64 WallClock::nowUnixMilliseconds() noexcept
    {
        const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
        return static_cast<int64>( std::chrono::duration_cast<std::chrono::milliseconds>( sinceEpoch ).count() );
    }
} // namespace sw
