/**
 * @file LogEvent.cpp
 * @brief 로그 사건의 시각 · 순번 · 시계 · 스레드 · 문맥을 남기는 순간에 정합니다(`LogMetadata` · `LogEvent`, 선언은 `LogTypes.h`).
 */
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/LogTypes.h"
#include "Core/Log/Logger.h"
#include "Core/Time/WallClock.h"

namespace sw
{
    namespace
    {
        atomic<uint64> s_nextLogSequence{ 1 };
        atomic<uint32> s_nextLogThreadID{ 1 };

        /** @brief 이 스레드의 번호입니다. 0 이면 아직 로그를 남긴 적이 없다. */
        thread_local uint32 t_logThreadID{ 0 };
    } // namespace

    uint64 LogMetadata::getNextSequence()
    {
        return s_nextLogSequence.load( std::memory_order_acquire );
    }

    LogMetadata LogMetadata::captureNow()
    {
        if ( t_logThreadID == 0 )
            t_logThreadID = s_nextLogThreadID.fetch_add( 1, std::memory_order_relaxed );

        LogMetadata metadata;
        metadata._sequence  = s_nextLogSequence.fetch_add( 1, std::memory_order_acq_rel );
        metadata._time      = WallClock::nowUnixMilliseconds();
        metadata._bHasClock = Logger::getClockNumber( metadata._clock, metadata._clockNumber );
        metadata._context   = LogContext::getCurrent();
        metadata._threadID  = t_logThreadID;
        return metadata;
    }

    LogEvent::LogEvent( LogLevel level, const utf8* pTag, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID )
        : _metadata{ LogMetadata::captureNow() }
        , _pTag{ pTag }
        , _pMessage{ pMessage }
        , _pFile{ pFile }
        , _pID{ pID }
        , _line{ line }
        , _level{ level }
    {
    }
} // namespace sw
