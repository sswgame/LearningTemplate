#include "pch.h"

#include "Core/Log/Logger.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Log/ConsoleLogOutput.h"
#include "Core/Log/FileLogOutput.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 프로세스 전역 활성 로그 싱크 포인터 */
        atomic<ILogSink*> s_globalSink{ nullptr };

        /** @brief 파일명 해시별 로그 Caller 이름 엔트리 (동적 힙 메모리 할당 0건) */
        struct CallerEntry
        {
            uint64 _fileHash{ 0 };
            utf8   _arrCallerName[constant::kMaxBuffer64]{ 0 };
        };

        static constexpr size_t kMaxCallerEntries = 512;
        CallerEntry             s_arrCallerEntry[kMaxCallerEntries]{};
        size_t                  s_callerEntryCount{ 0 };
        mutex                   s_callerMutex{};

        /// @brief 런타임 상세도. 기본 Info — 배포본은 컴파일 상한(Warning)이 더 낮아 자동으로 잘린다.
        atomic<int32> s_runtimeVerbosity{ static_cast<int32>( LogLevel::Info ) };

    } // namespace

    Logger::Logger()
        : _onLogWritten{}
        , _listOutput{}
        , _pFileOutput{ nullptr }
        , _queue{}
        , _workerThread{}
        , _cv{}
        , _mutex{}
        , _cvMutex{}
        , _timeMutex{}
        , _cachedTimeSec{ 0 }
        , _cachedYear{ 0 }
        , _cachedMonth{ 0 }
        , _cachedDay{ 0 }
        , _cachedHour{ 0 }
        , _bIsRunning{ false }
        , _bInitialized{ false }
        , _arrCachedDateStr{}
    {
        // 기본 장치 둘. 다른 구성이 필요하면 addOutput 으로 더 붙인다.
        auto fileOutput = make_unique<FileLogOutput>();
        _pFileOutput    = fileOutput.get();
        _listOutput.push_back( make_unique<ConsoleLogOutput>() );
        _listOutput.push_back( std::move( fileOutput ) );

        ILogSink* pExpected{ nullptr };
        s_globalSink.compare_exchange_strong( pExpected, this, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    Logger::~Logger()
    {
        ILogSink* pExpected = this;
        s_globalSink.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    /**
     * @brief 로거를 초기화하고 로그 저장 폴더 생성 및 비동기 작업 스레드를 시작합니다.
     */
    void Logger::initialize()
    {
        if ( _bInitialized )
            return;

        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( unique_ptr<ILogOutput>& output : _listOutput )
            {
                if ( output != nullptr )
                    output->open();
            }
        }

        _bIsRunning.store( true, std::memory_order_release );
        _workerThread = std::thread( &Logger::workerLoop, this );
        _bInitialized = true;
    }

    /**
     * @brief 큐에 남은 로그를 플러시하고 열려 있는 로그 파일 스트림을 닫습니다.
     */
    void Logger::shutdown()
    {
        if ( _bInitialized == false )
            return;

        _bIsRunning.store( false, std::memory_order_release );
        _cv.notify_all();

        if ( _workerThread.joinable() )
            _workerThread.join();

        flushQueue();

        std::scoped_lock<mutex> lock{ _mutex };
        for ( unique_ptr<ILogOutput>& output : _listOutput )
        {
            if ( output != nullptr )
                output->close();
        }
        _onLogWritten.removeAll();
        _bInitialized = false;
    }

    /**
     * @brief 포맷팅된 로그 메시지를 파일 및 콘솔에 기록합니다.
     */
    void Logger::writeLog( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line )
    {
        writeLogInternal( level, pTag, pCaller, pMessage, pFile, line );
    }

    /**
     * @brief 로그 작성 이벤트를 수신할 델리게이트 리스너를 등록합니다. (예: ImGui 에디터 콘솔 창)
     */
    DelegateHandle Logger::addLogWrittenListener( const LogWrittenDelegate& listener )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _onLogWritten.add( listener );
    }

    /**
     * @brief 등록된 로그 리스너를 해제합니다.
     */
    void Logger::removeLogWrittenListener( const DelegateHandle& handle )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _onLogWritten.remove( handle );
    }

    void Logger::registerCaller( string_view filePath, string_view callerName ) noexcept
    {
        string_view fileName;
        FileUtil::getFileNamePart( filePath, fileName );
        const uint64            fileHash = StringUtil::computeHash64( fileName );
        std::scoped_lock<mutex> lock{ s_callerMutex };

        for ( size_t index = 0; index < s_callerEntryCount; ++index )
        {
            if ( s_arrCallerEntry[index]._fileHash == fileHash )
            {
                const size_t copyLen = MathUtil::min( callerName.size(), sizeof( s_arrCallerEntry[index]._arrCallerName ) - 1 );
                Memory::copy( s_arrCallerEntry[index]._arrCallerName, callerName.data(), copyLen );
                s_arrCallerEntry[index]._arrCallerName[copyLen] = '\0';
                return;
            }
        }

        if ( s_callerEntryCount < kMaxCallerEntries )
        {
            s_arrCallerEntry[s_callerEntryCount]._fileHash = fileHash;
            const size_t copyLen                           = MathUtil::min( callerName.size(), sizeof( s_arrCallerEntry[s_callerEntryCount]._arrCallerName ) - 1 );
            Memory::copy( s_arrCallerEntry[s_callerEntryCount]._arrCallerName, callerName.data(), copyLen );
            s_arrCallerEntry[s_callerEntryCount]._arrCallerName[copyLen] = '\0';
            ++s_callerEntryCount;
        }
    }

    const utf8* Logger::getCaller( const utf8* pFile )
    {
        if ( StringUtil::isNullOrEmpty( pFile ) )
            return nullptr;
        string_view fileName;
        FileUtil::getFileNamePart( string_view{ pFile }, fileName );
        const uint64            fileHash = StringUtil::computeHash64( fileName );
        std::scoped_lock<mutex> lock{ s_callerMutex };
        for ( size_t index = 0; index < s_callerEntryCount; ++index )
        {
            if ( s_arrCallerEntry[index]._fileHash == fileHash )
                return s_arrCallerEntry[index]._arrCallerName;
        }
        return nullptr;
    }

    void Logger::addOutput( unique_ptr<ILogOutput> output )
    {
        if ( output == nullptr )
            return;

        const bool bNeedsOpen = _bInitialized;
        if ( bNeedsOpen )
            output->open();

        std::scoped_lock<mutex> lock{ _mutex };
        _listOutput.push_back( std::move( output ) );
    }

    const string& Logger::getLogFolderPath()
    {
        static const string s_empty{};
        return ( _pFileOutput != nullptr ) ? _pFileOutput->getLogFolderPath() : s_empty;
    }

    void Logger::setRuntimeVerbosity( LogLevel level )
    {
        s_runtimeVerbosity.store( static_cast<int32>( level ), std::memory_order_relaxed );
    }

    LogLevel Logger::getRuntimeVerbosity()
    {
        return static_cast<LogLevel>( s_runtimeVerbosity.load( std::memory_order_relaxed ) );
    }

    void Logger::setGlobalSink( ILogSink* pSink )
    {
        s_globalSink.store( pSink, std::memory_order_release );
    }

    ILogSink* Logger::getGlobalSink()
    {
        return s_globalSink.load( std::memory_order_acquire );
    }

    void Logger::writeLogGlobal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line )
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink == nullptr )
            return;

        pSink->writeLog( level, pTag, pCaller, pMessage, pFile, line );
    }

    DelegateHandle Logger::addGlobalListener( const LogWrittenDelegate& listener )
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink == nullptr )
            return {};

        return pSink->addLogWrittenListener( listener );
    }

    void Logger::removeGlobalListener( const DelegateHandle& handle )
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink == nullptr )
            return;

        pSink->removeLogWrittenListener( handle );
    }

    void Logger::workerLoop()
    {
        while ( _bIsRunning.load( std::memory_order_acquire ) || _queue.empty() == false )
        {
            LogRecord record;
            bool      bProcessedAny = false;
            while ( _queue.dequeue( record ) )
            {
                bProcessedAny = true;
                dispatchToOutputs( record );
            }

            if ( bProcessedAny == false && _bIsRunning.load( std::memory_order_acquire ) )
            {
                std::unique_lock<mutex> lock{ _cvMutex };
                _cv.wait_for( lock, std::chrono::milliseconds( 5 ), [this]
                {
                    return _bIsRunning.load( std::memory_order_relaxed ) == false || _queue.empty() == false;
                } );
            }
        }
    }

    void Logger::flushQueue()
    {
        LogRecord record;
        while ( _queue.dequeue( record ) )
        {
            dispatchToOutputs( record );
        }
    }

    void Logger::dispatchToOutputs( const LogRecord& record )
    {
        // 목록만 잠깐 잠그고 **쓰기는 락 밖에서** 한다 — 장치가 저마다 제 락을 갖고 있고,
        // 느린 파일 I/O 가 콘솔을 막지 않게 하는 것이 이 분리의 목적이다.
        ILogOutput* arrDevice[8]{};
        uint32      deviceCount{ 0 };
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( unique_ptr<ILogOutput>& output : _listOutput )
            {
                if ( output != nullptr && deviceCount < SW_COUNT_OF( arrDevice ) )
                    arrDevice[deviceCount++] = output.get();
            }
        }

        for ( uint32 index = 0; index < deviceCount; ++index )
            arrDevice[index]->write( record );
    }

    void Logger::writeLogInternal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line )
    {
        // 1단계: 타임스탬프 계산 및 포맷팅 (동일 초 내에서는 캐시된 문자열 재사용)
        int32 year{ 0 }, month{ 0 }, day{ 0 }, hour{ 0 };

        fixed_string<constant::kMaxBuffer32> dateStr{};
        {
            std::scoped_lock<mutex> lock{ _timeMutex };
            const std::time_t       timeSec = std::time( nullptr );
            if ( timeSec == _cachedTimeSec )
            {
                dateStr = _arrCachedDateStr;
                year    = _cachedYear;
                month   = _cachedMonth;
                day     = _cachedDay;
                hour    = _cachedHour;
            }
            else
            {
                std::tm localTime{};
#if defined( SW_PLATFORM_WINDOWS )
                localtime_s( &localTime, &timeSec );
#else
                std::tm* pLocalTime = std::localtime( &timeSec );
                if ( pLocalTime != nullptr )
                    localTime = *pLocalTime;
#endif
                year               = localTime.tm_year + 1900;
                month              = localTime.tm_mon + 1;
                day                = localTime.tm_mday;
                hour               = localTime.tm_hour;
                const int32 minute = localTime.tm_min;
                const int32 second = localTime.tm_sec;

                formatstring( _arrCachedDateStr, sizeof( _arrCachedDateStr ), "%#-%#-%# %#:%#:%#", year, month, day, hour, minute, second );
                dateStr        = _arrCachedDateStr;
                _cachedTimeSec = timeSec;
                _cachedYear    = year;
                _cachedMonth   = month;
                _cachedDay     = day;
                _cachedHour    = hour;
            }
        }

        static constexpr const utf8* kArrHeader[] = { "Error", "Warning", "Info", "Trace" };
        static_assert( SW_COUNT_OF( kArrHeader ) == static_cast<uint32>( LogLevel::Count ), "LogLevel과 같아야 합니다" );

        const size_t levelIndex       = static_cast<size_t>( level );
        const utf8*  pEffectiveTag    = ( StringUtil::isNullOrEmpty( pTag ) ) ? constant::kDefaultLogTag : pTag;
        const utf8*  pEffectiveFile   = ( StringUtil::isNullOrEmpty( pFile ) ) ? constant::kDefaultLogFile : pFile;
        const utf8*  pEffectiveMsg    = ( pMessage != nullptr ) ? pMessage : "";
        const utf8*  pEffectiveCaller = pCaller;
        if ( StringUtil::isNullOrEmpty( pEffectiveCaller ) && pFile != nullptr )
        {
            pEffectiveCaller = getCaller( pFile );
        }

        // 2단계: 스택 8KB fixed_string 버퍼에 1회 포맷팅 (동적 힙 메모리 할당 0건)
        fixed_string<constant::kMaxBuffer8192> formattedBuffer{};
        if ( StringUtil::isNullOrEmpty( pEffectiveCaller ) == false )
        {
            formatstring( formattedBuffer.data(), formattedBuffer.capacity(),
                          "[%#] [%#] [%#] [%#] - %#\n -> %#:%#\n",
                          dateStr.c_str(), pEffectiveTag, pEffectiveCaller, kArrHeader[levelIndex], pEffectiveMsg, pEffectiveFile, line );
        }
        else
        {
            formatstring( formattedBuffer.data(), formattedBuffer.capacity(),
                          "[%#] [%#] [%#] - %#\n -> %#:%#\n",
                          dateStr.c_str(), pEffectiveTag, kArrHeader[levelIndex], pEffectiveMsg, pEffectiveFile, line );
        }

        // 3단계: 64비트 SWAR 기반 고속 UTF-8 검증 및 Non-UTF8(ANSI/CP949) 한글 안전 자동 변환
        string      fallbackUtf8;
        const utf8* pFormattedBuffer = formattedBuffer.c_str();
        if ( StringUtil::isValidUTF8( pFormattedBuffer ) == false )
        {
            fallbackUtf8     = StringUtil::localeToUtf8( pFormattedBuffer );
            pFormattedBuffer = fallbackUtf8.c_str();
        }

        // 4단계: 인메모리 리스너(에디터 콘솔 UI/테스트 캡처) 스냅샷 복사 후 락 밖에서 안전하게 전파
        LogWrittenMulticast listenersCopy;
        bool                bHasListeners{ false };
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _onLogWritten.isBound() )
            {
                listenersCopy = _onLogWritten;
                bHasListeners = true;
            }
        }

        if ( bHasListeners && listenersCopy.isBound() )
        {
            LogEntry entry;
            entry._level     = level;
            entry._tag       = pEffectiveTag;
            entry._caller    = ( pEffectiveCaller != nullptr ) ? pEffectiveCaller : "";
            entry._message   = pEffectiveMsg;
            entry._file      = pEffectiveFile;
            entry._line      = line;
            entry._timeStamp = dateStr.c_str();
            listenersCopy.broadcast( entry );
        }

        // 5단계: 비동기 I/O 큐 인큐 (초기화 전이거나 큐가 가득 차면 이 스레드에서 바로 쓴다)
        LogRecord record;
        record._level     = level;
        record._formatted = pFormattedBuffer;
        record._year      = year;
        record._month     = month;
        record._day       = day;
        record._hour      = hour;

        if ( _bInitialized == false || _bIsRunning.load( std::memory_order_relaxed ) == false )
        {
            dispatchToOutputs( record );
            return;
        }

        if ( _queue.enqueue( std::move( record ) ) == false )
        {
            // enqueue 가 실패했으면 record 는 옮겨지지 않았다 — 그대로 동기로 쓴다.
            dispatchToOutputs( record );
            return;
        }

        _cv.notify_one();
    }

} // namespace sw
