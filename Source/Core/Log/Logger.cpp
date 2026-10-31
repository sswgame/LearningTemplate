#include "pch.h"

#include "Core/Log/Logger.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include <cstdio>

namespace sw
{
    namespace
    {
        /** @brief 프로세스 전역 활성 로그 싱크 포인터입니다. */
        atomic<ILogSink*> s_globalSink{ nullptr };

        /**
         * @brief 호출자 표의 키 — 경로의 **마지막 두 조각**(상위 폴더/파일 이름) 원문입니다. 구분자는 슬래시와 역슬래시를 같게 봅니다.
         * @details 파일 이름만 쓰면 다른 폴더의 같은 이름 파일(기반 폴더의 `X.cpp` 와 어느 키트의 `X.cpp`)이
         *          같은 키라, 나중에 등록된 이름이 두 파일의 로그에 모두 붙는다. 전체 경로는 쓰지 않는다 — 같은 헤더라도 TU 마다 `__FILE__` 의
         *          앞부분(절대 · 상대 · 구분자)이 다를 수 있다. 해시가 아니라 원문을 비교한다 — 해시가 부딪혀도 다른 파일의 Caller 이름을 붙이지 않는다.
         *          버퍼보다 길면 앞을 버리고 꼬리(파일 이름 쪽)를 남긴다.
         */
        struct CallerKey
        {
            utf8   _arrText[constant::kMaxBuffer128]{ 0 };
            size_t _length{ 0 };

            bool operator==( const CallerKey& other ) const
            {
                return _length == other._length && Memory::compare( _arrText, other._arrText, _length ) == 0;
            }
        };

        CallerKey computeCallerKey( string_view filePath )
        {
            size_t cut            = filePath.size();
            int32  separatorCount = 0;
            while ( cut > 0 )
            {
                const utf8 character = filePath[cut - 1];
                if ( ( character == '/' || character == '\\' ) && ++separatorCount == 2 )
                    break;
                --cut;
            }
            CallerKey key;
            key._length = MathUtil::min( filePath.size() - cut, sizeof( key._arrText ) );
            cut         = filePath.size() - key._length;
            for ( size_t index = 0; index < key._length; ++index )
            {
                const utf8 character = filePath[cut + index];
                key._arrText[index]  = character == '\\' ? '/' : character;
            }
            return key;
        }

        /** @brief 경로 키(`computeCallerKey`)별 로그 Caller 이름 항목입니다(힙 할당 없음). */
        struct CallerEntry
        {
            CallerKey _key{};
            utf8      _arrCallerName[constant::kMaxBuffer64]{ 0 };
        };

        /**
         * @brief 호출자 표의 칸 수입니다. 엔진 · 게임 · 에디터 · 키트 모듈의 `SW_LOG_CALLER` 가 모두 이 한 표에 들어옵니다.
         * @details 넘친 등록은 버려져 그 파일의 로그가 Caller 없이 분류된다(Output Log 의 Caller 필터가 그 줄을 못 가린다). 넘치면 첫 로그 줄에서 오류를 한 번 남긴다.
         */
        static constexpr size_t kMaxCallerEntries = 2048;
        CallerEntry             s_arrCallerEntry[kMaxCallerEntries]{};
        size_t                  s_callerEntryCount{ 0 };
        mutex                   s_callerMutex{};
        atomic<uint32>          s_droppedCallerCount{ 0 };          ///< 표가 차서 버린 등록 수
        atomic<bool>            s_bCallerOverflowReported{ false }; ///< 넘침 오류를 이미 남겼으면 true

        /**
         * @brief 싱크가 서기 전에 남은 줄 하나입니다(기동 초기 — 명령줄이 로거보다 먼저 선다). 싱크가 등록되면 넘긴다(`Logger::registerGlobalSink`).
         * @details 언리얼 FOutputDeviceRedirector 의 backlog 와 같다. 힙을 쓰지 않는다 — 메모리 계층이 서기 전일 수 있다. 태그 · 파일 · id 는 글자 상수라 포인터만 둔다.
         */
        struct BacklogLine
        {
            LogLevel    _level{ LogLevel::Info };
            const utf8* _pTag{ nullptr };
            const utf8* _pFile{ nullptr };
            const utf8* _pID{ nullptr };
            int32       _line{ 0 };
            bool        _bHasCaller{ false };
            utf8        _arrCaller[constant::kMaxBuffer64]{ 0 };
            utf8        _arrMessage[constant::kMaxBuffer512]{ 0 };
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
         * @brief 경로의 파일 이름에서 확장자를 뺀 글을 @p arrStem 에 담습니다(`.../LinearAllocator.cpp` → `LinearAllocator`).
         */
        void copyFileStem( const utf8* pFile, utf8 ( &arrStem )[constant::kMaxBuffer64] )
        {
            const string_view path{ pFile };
            const size_t      separator = path.find_last_of( "/\\" );
            const size_t      nameBegin = separator == string_view::npos ? 0 : separator + 1;
            const size_t      dot       = path.find_last_of( '.' );
            const size_t      nameEnd   = dot == string_view::npos || dot < nameBegin ? path.size() : dot;
            const size_t      length    = MathUtil::min( nameEnd - nameBegin, sizeof( arrStem ) - 1 );
            Memory::copy( arrStem, path.data() + nameBegin, length );
            arrStem[length] = '\0';
        }

        /**
         * @brief 로그보다 아래 티어의 보고(`SW_LOW_LEVEL_*`)를 로그 줄로 넘깁니다. 싱크가 아직 없으면 false 를 돌려주어 stderr 로 가게 합니다(`reportLowLevel`).
         * @details Caller 는 `SW_LOG_*` 와 같은 방식으로 **보고한 파일**에서 정해집니다. 그 파일이 `SW_LOG_CALLER` 를 등록했으면 그 이름이고, 낮은 층 파일은
         *          `Logger.h` 를 쓸 수 없어 등록하지 못하므로 파일 이름(확장자 뺀)이 Caller 가 됩니다. 태그(`SW_LOG_TAG`)는 이 파일(Core)의 것, 곧 Engine 입니다.
         */
        bool forwardLowLevelReport( internal::LowLevelReportLevel level, const utf8* pFile, int32 line, const utf8* pMessage, const utf8* pID ) noexcept
        {
            if ( s_globalSink.load( std::memory_order_acquire ) == nullptr )
                return false;

            const LogLevel logLevel = level == internal::LowLevelReportLevel::Warning ? LogLevel::Warning : LogLevel::Error;
            if ( Logger::shouldLog( logLevel ) )
            {
                utf8        arrStem[constant::kMaxBuffer64];
                const utf8* pCaller = Logger::getCaller( pFile );
                if ( pCaller == nullptr )
                {
                    copyFileStem( pFile, arrStem );
                    pCaller = arrStem;
                }
                Logger::writeLogGlobal( logLevel, SW_LOG_TAG, pCaller, pMessage, pFile, line, pID );
            }
            if ( level == internal::LowLevelReportLevel::Fatal )
                Logger::flushGlobalForCrash(); // 곧 멈춘다(`reportFatal`) — 비동기 싱크가 쥔 줄을 내보낸다
            return true;
        }

        /// @brief 기동 때 보고자를 건다. 포인터 하나를 상수 초기화된 원자에 넣을 뿐이라 정적 초기화 순서를 타지 않는다.
        [[maybe_unused]] const bool s_bLowLevelReporterInstalled = []() noexcept
        {
            internal::setLowLevelReporter( &forwardLowLevelReport );
            return true;
        }();
    } // namespace
} // namespace sw

namespace sw
{
    void Logger::flushGlobalForCrash()
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink != nullptr )
            pSink->flushForCrash();
    }

    void Logger::registerCaller( string_view filePath, string_view callerName ) noexcept
    {
        const CallerKey   fileKey = computeCallerKey( filePath );
        ScopedLock<mutex> lock{ s_callerMutex };

        for ( size_t index = 0; index < s_callerEntryCount; ++index )
        {
            if ( s_arrCallerEntry[index]._key == fileKey )
            {
                const size_t copyLen = MathUtil::min( callerName.size(), sizeof( s_arrCallerEntry[index]._arrCallerName ) - 1 );
                Memory::copy( s_arrCallerEntry[index]._arrCallerName, callerName.data(), copyLen );
                s_arrCallerEntry[index]._arrCallerName[copyLen] = '\0';
                return;
            }
        }

        if ( s_callerEntryCount < kMaxCallerEntries )
        {
            s_arrCallerEntry[s_callerEntryCount]._key = fileKey;
            const size_t copyLen                      = MathUtil::min( callerName.size(), sizeof( s_arrCallerEntry[s_callerEntryCount]._arrCallerName ) - 1 );
            Memory::copy( s_arrCallerEntry[s_callerEntryCount]._arrCallerName, callerName.data(), copyLen );
            s_arrCallerEntry[s_callerEntryCount]._arrCallerName[copyLen] = '\0';
            ++s_callerEntryCount;
            return;
        }
        s_droppedCallerCount.fetch_add( 1, std::memory_order_relaxed ); // 정적 초기화 중일 수 있어 여기서는 로그를 남기지 않는다
    }

    const utf8* Logger::getCaller( const utf8* pFile )
    {
        if ( StringUtil::isNullOrEmpty( pFile ) )
            return nullptr;
        const CallerKey   fileKey = computeCallerKey( string_view{ pFile } );
        ScopedLock<mutex> lock{ s_callerMutex };
        for ( size_t index = 0; index < s_callerEntryCount; ++index )
        {
            if ( s_arrCallerEntry[index]._key == fileKey )
                return s_arrCallerEntry[index]._arrCallerName;
        }
        return nullptr;
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

    void Logger::printAssertFailureToStderr( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pFunction ) noexcept
    {
        std::fprintf( stderr, "\n[SW_LOG_ASSERT] %s\n  %s\n  at %s:%d\n  in %s\n", pExpression, pMessage, pFile, line, pFunction );
        std::fflush( stderr );
    }

    void Logger::writeLogGlobal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID )
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink == nullptr )
        {
            // 싱크가 없으면(기동 초기, 로거 없는 도구) 줄을 잃지 않게 stderr 에 남긴다. 상세도는 부르는 쪽이 이미 걸렀다.
            const utf8* pLevelName = level == LogLevel::Error ? "Error" : level == LogLevel::Warning ? "Warning"
                                                                    : level == LogLevel::Info        ? "Info"
                                                                                                     : "Trace";
            std::fprintf( stderr, "[%s][%s] %s:%d: %s\n", pLevelName, pTag != nullptr ? pTag : "", pFile != nullptr ? pFile : "", line, pMessage != nullptr ? pMessage : "" );
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
                backlog._level       = level;
                backlog._pTag        = pTag;
                backlog._pFile       = pFile;
                backlog._pID         = pID;
                backlog._line        = line;
                backlog._bHasCaller  = pCaller != nullptr;
                copyTruncated( pCaller, backlog._arrCaller ); // 호출자는 부른 쪽 스택에 있을 수 있다(낮은 층 보고의 파일 이름)
                copyTruncated( pMessage, backlog._arrMessage );
                return;
            }
            // stderr 에 쓰는 사이 싱크가 섰다 — 아래로 내려가 그 싱크에도 쓴다.
        }

        const bool bCallerDropped = s_droppedCallerCount.load( std::memory_order_relaxed ) > 0;
        if ( bCallerDropped && s_bCallerOverflowReported.exchange( true ) == false )
            pSink->writeLog( LogLevel::Error, "Log", nullptr, "The log caller table is full - SW_LOG_CALLER registrations were dropped (raise kMaxCallerEntries)",
                             __FILE__, __LINE__ );
        pSink->writeLogWithID( level, pTag, pCaller, pMessage, pFile, line, pID );
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
        ScopedLock<mutex> lock{ s_backlogMutex };
        ILogSink*         pExpected{ nullptr };
        if ( s_globalSink.compare_exchange_strong( pExpected, pSink, std::memory_order_acq_rel, std::memory_order_relaxed ) == false )
            return;

        // 싱크가 서기 전에 쌓인 줄을 남은 순서대로 넘긴다 — 로그 파일에 기동 첫 줄부터 남는다.
        for ( uint32 index = 0; index < s_backlogCount; ++index )
        {
            const BacklogLine& backlog = s_arrBacklog[index];
            pSink->writeLogWithID( backlog._level, backlog._pTag, backlog._bHasCaller ? backlog._arrCaller : nullptr, backlog._arrMessage, backlog._pFile,
                                   backlog._line, backlog._pID );
        }
        if ( s_droppedBacklogCount > 0 )
            pSink->writeLog( LogLevel::Warning, "Log", nullptr, "Lines logged before the log sink existed overflowed the backlog - only the first ones were kept (raise kMaxBacklogLine)",
                             __FILE__, __LINE__ );
        s_backlogCount        = 0;
        s_droppedBacklogCount = 0;
    }

    void Logger::unregisterGlobalSink( ILogSink* pSink )
    {
        ILogSink* pExpected = pSink;
        s_globalSink.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );
    }
} // namespace sw
