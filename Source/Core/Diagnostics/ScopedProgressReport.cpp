#include "pch.h"

#include "Core/Diagnostics/ScopedProgressReport.h"

#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include <chrono>

namespace sw
{
    ScopedProgressReport::ScopedProgressReport( string what )
        : _what{ std::move( what ) }
        , _startNanos{ MonotonicClock::nowNanoseconds() }
        , _thread{ ThreadCreateKey{}, &ScopedProgressReport::run, this }
    {
    }

    ScopedProgressReport::~ScopedProgressReport()
    {
        {
            const ScopedLock<mutex> lock{ _mutex };
            _bDone = true;
        }
        _cv.notify_one();
        _thread.join();
    }

    void ScopedProgressReport::run()
    {
        UniqueLock<mutex> lock{ _mutex };
        while ( _cv.wait_for( lock, std::chrono::seconds( kIntervalSeconds ), [this]() SW_REQUIRES_LOCK( _mutex )
        { return _bDone; } ) == false )
        {
            const int64 elapsedSeconds = ( MonotonicClock::nowNanoseconds() - _startNanos ) / constant::time::kNanosecondsPerSecond;
            SW_LOG_INFO_ID( "ProgressReport.StillWorking", "%# still running after %# s", _what.c_str(), elapsedSeconds );
        }
    }
} // namespace sw
