#include "pch.h"

#include "Core/Log/Logger.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/LowLevelReporterSlot.h"
#include "Core/Log/ScopedDeferredLog.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include <cstdio>

namespace sw
{
    namespace
    {
        /** @brief 프로세스 전역 활성 로그 싱크 포인터입니다. */
        atomic<ILogSink*> s_globalSink{ nullptr };

        /**
         * @brief 싱크가 서기 전에 남은 줄 하나입니다(기동 초기 — 명령줄이 로거보다 먼저 선다). 싱크가 등록되면 넘긴다(`Logger::registerGlobalSink`).
         * @details 언리얼 FOutputDeviceRedirector 의 backlog 와 같다. 힙을 쓰지 않는다 — 메모리 계층이 서기 전일 수 있다. 본문만 사본을 두고, 사건(시각 · 순번 · 글자 상수 포인터)은 남긴 순간의 것을 넘긴다.
         */
        struct BacklogLine
        {
            LogEvent _event{};
            utf8     _arrMessage[constant::buffer::kMax512]{ 0 };
        };
        static constexpr uint32 kMaxBacklogLine = 64;
        BacklogLine             s_arrBacklog[kMaxBacklogLine]{};
        uint32                  s_backlogCount{ 0 };
        uint32                  s_droppedBacklogCount{ 0 }; ///< 칸이 차서 쌓지 못한 줄 수(넘길 때 한 줄로 알린다)
        mutex                   s_backlogMutex{};           ///< 쌓기 · 등록 · 넘기기를 한 줄로 세운다 — 등록 직후의 줄이 쌓인 채 남지 않는다

        /** @brief @p pText 를 @p arrOut 에 담기는 만큼 복사합니다(잘린다). */
        template <size_t N>
        void copyTruncated( const utf8* pText, utf8 ( &arrOut )[N] )
        {
            const size_t length = pText == nullptr ? 0 : MathUtil::min( static_cast<size_t>( StringUtil::strlen( pText ) ), N - 1 );
            if ( length > 0 )
                Memory::copy( arrOut, pText, length );
            arrOut[length] = '\0';
        }

        /// @brief 런타임 상세도입니다. 기본값은 Info 이고, 배포본은 컴파일 상한(Warning)이 더 낮아 자동으로 잘립니다.
        atomic<int32> s_runtimeVerbosity{ static_cast<int32>( LogLevel::Info ) };
        /// @brief 줄 꼬리표의 시계입니다. 낮은 63 비트가 번호 + 1(0 = 정하지 않음 — 꼬리표 없음), 맨 위 비트가 종류(`LogClock::Frame` 이면 1)입니다.
        atomic<uint64>   s_clockPacked{ 0 };
        constexpr uint64 kClockFrameBit   = uint64( 1 ) << 63;
        constexpr uint64 kClockNumberMask = kClockFrameBit - 1;

        /**
         * @brief 로그보다 아래 티어의 보고(`SW_LOW_LEVEL_*`)를 로그 줄로 넘깁니다. 싱크가 아직 없으면 false 를 돌려주어 stderr 로 가게 합니다(`reportLowLevel`).
         * @details 태그(`SW_LOG_TAG`)는 이 파일(Core)의 것, 곧 Engine 입니다. 보고한 곳은 줄의 파일 · 줄 번호가 가리킨다.
         */
        bool forwardLowLevelReport( internal::LowLevelReportLevel level, const utf8* pFile, int32 line, const utf8* pMessage, const utf8* pID ) noexcept
        {
            if ( s_globalSink.load( std::memory_order_acquire ) == nullptr )
                return false;

            const ScopedLeafLockExemption exemption; // 아래 티어의 보고는 부른 쪽이 잠금을 쥐었는지 모른다
            const LogLevel                logLevel = level == internal::LowLevelReportLevel::Warning ? LogLevel::Warning : LogLevel::Error;
            if ( Logger::shouldLog( logLevel ) )
                Logger::writeLogGlobal( logLevel, SW_LOG_TAG, pMessage, pFile, line, pID );
            if ( level == internal::LowLevelReportLevel::Fatal )
                Logger::flushGlobalForCrash(); // 곧 멈춘다(`reportFatal`) — 비동기 싱크가 쥔 줄을 내보낸다
            return true;
        }

        /// @brief 기동 때 건 보고자의 등록이다. 칸이 상수 초기화돼 정적 초기화 순서를 타지 않고, 프로세스가 끝날 때 토큰이 떼어 낸다(그 뒤 보고는 stderr).
        internal::LowLevelReporterRegistration s_lowLevelReporterRegistration = internal::bindLowLevelReporter( &forwardLowLevelReport );
    } // namespace
} // namespace sw

namespace sw
{
    void Logger::flushGlobalForCrash()
    {
        ScopedDeferredLog::flushForCrash();
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink != nullptr )
            pSink->waitUpToCrash();
    }

    void Logger::setRuntimeVerbosity( LogLevel level )
    {
        s_runtimeVerbosity.store( static_cast<int32>( level ), std::memory_order_relaxed );
    }

    LogLevel Logger::getRuntimeVerbosity()
    {
        return static_cast<LogLevel>( s_runtimeVerbosity.load( std::memory_order_relaxed ) );
    }

    void Logger::setClockNumber( LogClock clock, uint64 number )
    {
        // 번호는 63 비트에 담는다 — 2^63 틱 · 프레임은 오지 않는다.
        const uint64 kindBit = ( clock == LogClock::Frame ) ? kClockFrameBit : 0;
        s_clockPacked.store( kindBit | ( ( number + 1 ) & kClockNumberMask ), std::memory_order_relaxed );
    }

    void Logger::clearClockNumber()
    {
        s_clockPacked.store( 0, std::memory_order_relaxed );
    }

    bool Logger::getClockNumber( LogClock& outClock, uint64& outNumber )
    {
        const uint64 packed        = s_clockPacked.load( std::memory_order_relaxed );
        const uint64 numberPlusOne = packed & kClockNumberMask;
        outClock                   = ( numberPlusOne != 0 && ( packed & kClockFrameBit ) != 0 ) ? LogClock::Frame : LogClock::Tick;
        outNumber                  = numberPlusOne != 0 ? numberPlusOne - 1 : 0;
        return numberPlusOne != 0;
    }

    void Logger::setGlobalSink( ILogSink* pSink )
    {
        s_globalSink.store( pSink, std::memory_order_release );
    }

    ILogSink* Logger::getGlobalSink()
    {
        return s_globalSink.load( std::memory_order_acquire );
    }

    void Logger::printAssertFailureToStderr( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line, [[maybe_unused]] const utf8* pFunction ) noexcept
    {
        std::fprintf( stderr, "\n[SW_LOG_ASSERT] %s\n  %s\n  at %s:%d\n  in %s\n", pExpression, pMessage, pFile, line, pFunction );
        std::fflush( stderr );
    }

    bool Logger::reportAssertFailure( const utf8* pTag, const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line, [[maybe_unused]] const utf8* pFunction ) noexcept
    {
        const ScopedLeafLockExemption exemption; // 단언은 잠금을 쥔 자리에서도 진다 — 보고가 잎 잠금 단언으로 덮이지 않게
#if defined( SW_DO_CHECKS )
        printAssertFailureToStderr( pExpression, pMessage, pFile, line, pFunction != nullptr ? pFunction : "" );
#endif
        if ( shouldLog( LogLevel::Error ) )
        {
            utf8 arrLine[constant::buffer::kMax8192];
#if defined( SW_DO_CHECKS )
            formatstring( arrLine, constant::buffer::kMax8192,
                          "ASSERT failed\n"
                          "Expression : %#\n"
                          "Message    : %#\n"
                          "FileName   : %#\n"
                          "Function   : %#\n"
                          "Line       : %#",
                          pExpression, pMessage, pFile, pFunction != nullptr ? pFunction : "", line );
#else
            formatstring( arrLine, constant::buffer::kMax8192,
                          "ASSERT failed\n"
                          "Expression : %#\n"
                          "Message    : %#\n"
                          "FileName   : %#\n"
                          "Line       : %#",
                          pExpression, pMessage, pFile, line );
#endif
            writeLogGlobal( LogLevel::Error, pTag, arrLine, pFile, line, SW_ASSERT_LOG_ID );
        }
#if defined( SW_DO_CHECKS )
        return internal::tryCaptureAssert() == false && internal::shouldBreakOnAssert( pExpression, pMessage, pFile, line );
#else
        return false;
#endif
    }

    void Logger::writeLogGlobal( LogLevel level, const utf8* pTag, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID )
    {
        writeEventGlobal( LogEvent{ level, pTag, pMessage, pFile, line, pID } );
    }

    void Logger::writeEventGlobal( const LogEvent& event )
    {
        if ( ScopedDeferredLog::tryDefer( event ) )
            return;
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink == nullptr )
        {
            // 싱크가 없으면(기동 초기, 로거 없는 도구) 줄을 잃지 않게 stderr 에 남긴다. 상세도는 부르는 쪽이 이미 걸렀다.
            const utf8* pLevelName = event._level == LogLevel::Error ? "Error" : event._level == LogLevel::Warning ? "Warning"
                                                                           : event._level == LogLevel::Info        ? "Info"
                                                                                                                   : "Trace";
            std::fprintf( stderr, "[%s][%s] %s:%d: %s\n", pLevelName, event._pTag != nullptr ? event._pTag : "", event._pFile != nullptr ? event._pFile : "", event._line, event._pMessage != nullptr ? event._pMessage : "" );
            std::fflush( stderr );

            ScopedLock<mutex> lock{ s_backlogMutex };
            pSink = s_globalSink.load( std::memory_order_acquire );
            if ( pSink == nullptr )
            {
                if ( s_backlogCount == kMaxBacklogLine )
                {
                    ++s_droppedBacklogCount;
                    return;
                }
                BacklogLine& backlog = s_arrBacklog[s_backlogCount++];
                backlog._event       = event.withMessage( nullptr ); // 본문은 아래 칸에 — 넘길 때 잇는다
                copyTruncated( event._pMessage, backlog._arrMessage );
                return;
            }
            // stderr 에 쓰는 사이 싱크가 섰다 — 아래로 내려가 그 싱크에도 쓴다.
        }

        pSink->writeEvent( event );
    }

    Subscription Logger::registerGlobalListener( const LogWrittenDelegate& listener )
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        // 리스너를 붙일 곳이 없는데 조용히 빈 구독을 돌려주면 부르는 쪽은 줄이 오지 않는 이유를 알 길이 없다. 순서 위반을 보고하고(Debug 는 멈춘다) 빈 구독을 돌려준다.
        SW_LOW_LEVEL_ASSERT( pSink != nullptr, "Logger::registerGlobalListener needs a global log sink - create the sink before attaching listeners" );
        if ( pSink == nullptr )
            return Subscription{};

        return pSink->registerLogWrittenListener( listener );
    }

    void Logger::registerGlobalSink( ILogSink* pSink )
    {
        // 싱크가 서기 전에 쌓인 줄을 남은 순서대로 넘긴다 — 로그 파일에 기동 첫 줄부터 남는다. 쌓인 줄을 다 넘긴 뒤에야 싱크를 세우므로 새 줄이 앞지르지 않는다.
        // 넘기는 일은 잠금 밖에서 한다(잎 잠금).
        vector<BacklogLine> listTaken;
        uint32              droppedCount = 0;
        for ( ;; )
        {
            {
                ScopedLock<mutex> lock{ s_backlogMutex };
                if ( s_globalSink.load( std::memory_order_acquire ) != nullptr )
                    return;
                droppedCount += s_droppedBacklogCount;
                s_droppedBacklogCount = 0;
                if ( s_backlogCount == 0 )
                {
                    s_globalSink.store( pSink, std::memory_order_release );
                    break;
                }
                listTaken.assign( s_arrBacklog, s_arrBacklog + s_backlogCount );
                s_backlogCount = 0;
            }
            for ( const BacklogLine& backlog : listTaken )
            {
                pSink->writeEvent( backlog._event.withMessage( backlog._arrMessage ) );
            }
        }
        if ( droppedCount > 0 )
            pSink->writeEvent( LogEvent{ LogLevel::Warning, "Log", "Lines logged before the log sink existed overflowed the backlog - only the first ones were kept (raise kMaxBacklogLine)", __FILE__, __LINE__ } );
    }

    void Logger::unregisterGlobalSink( ILogSink* pSink )
    {
        ILogSink* pExpected = pSink;
        s_globalSink.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );
    }
} // namespace sw
