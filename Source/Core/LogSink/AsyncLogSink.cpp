#include "pch.h"

#include "Core/LogSink/AsyncLogSink.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Container/StringUtil.h"
#include "Core/Log/LogContext.h"
#include "Core/LogSink/ConsoleLogOutput.h"
#include "Core/LogSink/FileLogOutput.h"
#include "Core/LogSink/ILogOutput.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/fixed_string.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    namespace
    {
        /// @brief 이 스레드가 지금 리스너를 부르는 중인 방송 깊이입니다(리스너 안에서 또 로그를 쓰면 겹친다). 떼기가 자기 자신을 기다리지 않게 한다.
        thread_local uint32 t_broadcastDepth{ 0 };

        /**
         * @struct GlobalLogListenerUnloadListener
         * @brief 전역 로그 리스너(`Logger::addGlobalListener`)를 모듈 언로드 리스너 목록에 올립니다. 리스너는 정적이라 리스너도 정적 하나입니다.
         */
        struct GlobalLogListenerUnloadListener final : public IModuleUnloadListener
        {
            const utf8* getModuleUnloadListenerName() const override { return "log listeners"; }

            uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override
            {
                (void)outKeepImageMapped;
                return Logger::releaseGlobalListenerCodeWithin( pBegin, pEnd );
            }
        };

        GlobalLogListenerUnloadListener s_globalLogListenerUnloadListener;
    } // namespace
} // namespace sw

namespace sw
{
    AsyncLogSink::AsyncLogSink()
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
        , _arrBroadcastInFlight{}
        , _broadcastEpoch{ 0 }
        , _bIsRunning{ false }
        , _bInitialized{ false }
        , _arrCachedDateStr{}
    {
        // 기본 장치 두 개. 다른 구성이 필요하면 addOutput 으로 더 붙인다.
        auto fileOutput = make_unique<FileLogOutput>();
        _pFileOutput    = fileOutput.get();
        _listOutput.push_back( make_unique<ConsoleLogOutput>() );
        _listOutput.push_back( std::move( fileOutput ) );

        Logger::registerGlobalSink( this );
    }

    AsyncLogSink::~AsyncLogSink()
    {
        Logger::unregisterGlobalSink( this );
    }

    /**
     * @brief 출력 장치를 열고(로그 폴더 생성 포함) 비동기 작업 스레드를 시작합니다.
     */
    void AsyncLogSink::initialize()
    {
        if ( _bInitialized )
            return;

        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( unique_ptr<ILogOutput>& output : _listOutput )
            {
                // 열지 못한 출력은 쓰기를 무시한다 — 로거에는 자기 실패를 남길 곳이 없다.
                if ( output != nullptr )
                    (void)output->open(); // 열지 못한 출력은 쓰기를 무시한다 — 남길 곳이 없다
            }
        }

        _bIsRunning.store( true, std::memory_order_release );
        _workerThread = std::thread( &AsyncLogSink::workerLoop, this );
        _bInitialized = true;
    }

    /**
     * @brief 큐에 남은 로그를 모두 쓰고, 작업 스레드를 멈춘 뒤 출력 장치를 닫습니다.
     */
    void AsyncLogSink::shutdown()
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
     * @brief 로그 한 줄을 기록합니다(writeLogInternal 로 넘깁니다).
     */
    void AsyncLogSink::writeLog( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line )
    {
        writeLogInternal( level, pTag, pCaller, pMessage, pFile, line );
    }

    /**
     * @brief 로그가 쓰일 때 알림을 받을 리스너를 등록합니다(예: ImGui 에디터 콘솔 창).
     */
    DelegateHandle AsyncLogSink::addLogWrittenListener( const LogWrittenDelegate& listener )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _onLogWritten.add( listener );
    }

    /**
     * @brief 등록된 로그 리스너를 해제합니다.
     */
    void AsyncLogSink::removeLogWrittenListener( const DelegateHandle& handle )
    {
        uint32 retiredSlot{ 0 };
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _onLogWritten.remove( handle );
            retiredSlot = retireBroadcastSlot();
        }
        waitForRetiredBroadcasts( retiredSlot );
    }

    uint32 AsyncLogSink::releaseListenerCodeWithin( const void* pBegin, const void* pEnd )
    {
        // 모듈을 내리기 직전에 불린다. 다른 스레드의 방송이 그 모듈 코드 안에 있는 채로 이미지를 내리면 안 된다 — 끝날 때까지 기다린다.
        uint32 releasedCount{ 0 };
        uint32 retiredSlot{ 0 };
        {
            std::scoped_lock<mutex> lock{ _mutex };
            releasedCount = _onLogWritten.removeCodeWithin( pBegin, pEnd );
            retiredSlot   = retireBroadcastSlot();
        }
        waitForRetiredBroadcasts( retiredSlot );
        return releasedCount;
    }

    uint32 AsyncLogSink::retireBroadcastSlot()
    {
        const uint32 retiredSlot = _broadcastEpoch & 1u;
        ++_broadcastEpoch;
        return retiredSlot;
    }

    void AsyncLogSink::waitForRetiredBroadcasts( uint32 retiredSlot )
    {
        if ( t_broadcastDepth > 0 )
            return;

        const Deadline deadline = Deadline::afterMilliseconds( 2000 );
        while ( _arrBroadcastInFlight[retiredSlot].load( std::memory_order_acquire ) != 0 )
        {
            if ( deadline.isExpired() )
            {
                std::fputs( "[Logger] Timed out waiting for in-flight log broadcasts after removing a listener\n", stderr );
                return;
            }
            std::this_thread::yield();
        }
    }

    void AsyncLogSink::flushForCrash()
    {
        // 크래시 경로다. 로거 락을 쥔 채 죽은 스레드가 있으면 기다리지 않고 포기한다.
        ILogOutput* arrDevice[_s_kMaxOutput]{};
        uint32      deviceCount{ 0 };
        if ( _mutex.try_lock() == false )
            return;
        for ( unique_ptr<ILogOutput>& output : _listOutput )
        {
            if ( output != nullptr && deviceCount < SW_COUNT_OF( arrDevice ) )
                arrDevice[deviceCount++] = output.get();
        }
        _mutex.unlock();

        // 큐는 여러 스레드가 함께 꺼내도 되는 큐다. 작업 스레드가 동시에 꺼내도 한 줄은 한 번만 쓰인다.
        LogRecord record;
        while ( _queue.dequeue( record ) )
        {
            for ( uint32 index = 0; index < deviceCount; ++index )
            {
                arrDevice[index]->write( record );
            }
        }
        for ( uint32 index = 0; index < deviceCount; ++index )
        {
            arrDevice[index]->flushWithoutWaiting();
        }
    }

    bool AsyncLogSink::addOutput( unique_ptr<ILogOutput> output )
    {
        if ( output == nullptr )
            return false;

        {
            // 상한을 **여기서** 확인한다. 디스패치는 고정 배열로 복사해 가므로, 일단 받아 두면 열어 놓고도 한 줄도 받지 못하는
            // 장치가 생긴다. 그 실패는 붙인 쪽에서 보이지 않는다.
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _listOutput.size() >= _s_kMaxOutput )
            {
                SW_LOG_WARNING( "로그 출력 장치는 최대 %#개입니다. 더 붙일 수 없어 거절합니다.", _s_kMaxOutput );
                return false;
            }
        }

        const bool bNeedsOpen = _bInitialized;
        if ( bNeedsOpen )
            (void)output->open(); // 위와 같다 — 열지 못한 출력은 쓰기를 무시한다

        std::scoped_lock<mutex> lock{ _mutex };
        _listOutput.push_back( std::move( output ) );
        return true;
    }

    const string& AsyncLogSink::getLogFolderPath()
    {
        static const string s_empty{};
        return ( _pFileOutput != nullptr ) ? _pFileOutput->getLogFolderPath() : s_empty;
    }

    void AsyncLogSink::workerLoop()
    {
        SW_MEMORY_SCOPE( EngineMisc );
        // 이 스레드에서 스택이 넘쳐도 크래시 리포트가 남게 한다(CrashHandler::initializeCurrentThread 설명).
        CrashHandler::initializeCurrentThread();
        ThreadName::setCurrentThreadName( "Logger" );
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

    void AsyncLogSink::flushQueue()
    {
        LogRecord record;
        while ( _queue.dequeue( record ) )
        {
            dispatchToOutputs( record );
        }
    }

    void AsyncLogSink::dispatchToOutputs( const LogRecord& record )
    {
        // 목록만 잠깐 잠그고 **쓰기는 락 밖에서** 한다. 장치마다 자기 락이 있고, 느린 파일 I/O 가 콘솔을 막지 않게 하는
        // 것이 이렇게 나눈 목적이다.
        ILogOutput* arrDevice[_s_kMaxOutput]{};
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
        {
            arrDevice[index]->write( record );
        }
    }

    void AsyncLogSink::writeLogInternal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line )
    {
        // 로그 레코드는 부른 쪽이 아니라 로거의 몫이다. 리스너(에디터 콘솔 등)는 자기 태그를 건다.
        SW_MEMORY_SCOPE( EngineMisc );
        // 1단계: 타임스탬프를 계산하고 포맷한다(같은 초 안에서는 캐시한 문자열을 재사용한다)
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
            pEffectiveCaller = Logger::getCaller( pFile );

        // 문맥(요청 추적 id · 주체)은 부른 스레드의 것이라 큐에 넣기 전에 읽는다. 문맥이 없으면 꼬리표가 빈 글이라 줄은 바이트가 같다.
        const LogContext& context = LogContext::getCurrent();
        utf8              arrContextTag[LogContext::kMaxTagSize];
        (void)context.formatTag( arrContextTag, LogContext::kMaxTagSize );

        // 2단계: 스택의 8KB fixed_string 버퍼에 한 번만 포맷한다(힙 할당 없음)
        fixed_string<constant::kMaxBuffer8192> formattedBuffer{};
        if ( StringUtil::isNullOrEmpty( pEffectiveCaller ) == false )
        {
            formatstring( formattedBuffer.data(), formattedBuffer.capacity(),
                          "[%#] [%#] [%#] [%#] %#- %#\n -> %#:%#\n",
                          dateStr.c_str(), pEffectiveTag, pEffectiveCaller, kArrHeader[levelIndex], arrContextTag, pEffectiveMsg, pEffectiveFile, line );
        }
        else
        {
            formatstring( formattedBuffer.data(), formattedBuffer.capacity(),
                          "[%#] [%#] [%#] %#- %#\n -> %#:%#\n",
                          dateStr.c_str(), pEffectiveTag, kArrHeader[levelIndex], arrContextTag, pEffectiveMsg, pEffectiveFile, line );
        }

        // 3단계: 64비트 SWAR 로 UTF-8 인지 빠르게 검증하고, 아니면 **잘못된 바이트만** `\xNN` 으로 바꾼다. 줄 전체를 로캘 변환하면
        //        C 로캘(아무도 `setlocale` 을 부르지 않는다)이라 멀쩡한 한글까지 깨지거나(Windows) 줄이 빈다(glibc).
        string      fallbackUtf8;
        const utf8* pFormattedBuffer = formattedBuffer.c_str();
        if ( StringUtil::isValidUtf8( pFormattedBuffer ) == false )
        {
            fallbackUtf8     = StringUtil::escapeInvalidUtf8( string_view{ pFormattedBuffer } );
            pFormattedBuffer = fallbackUtf8.c_str();
        }

        // 4단계: 메모리 리스너(에디터 콘솔 UI · 테스트 캡처)를 스냅샷으로 복사한 뒤 락 밖에서 알린다. 복사한 세대 칸에 "부르는 중" 을
        //        올려 두어, 떼기가 이 방송이 끝날 때까지 기다리게 한다(`waitForRetiredBroadcasts`).
        LogWrittenMulticast listenersCopy;
        bool                bHasListeners{ false };
        uint32              broadcastSlot{ 0 };
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _onLogWritten.isBound() )
            {
                listenersCopy = _onLogWritten;
                bHasListeners = true;
                broadcastSlot = _broadcastEpoch & 1u;
                _arrBroadcastInFlight[broadcastSlot].fetch_add( 1, std::memory_order_acq_rel );
            }
        }

        if ( bHasListeners )
        {
            LogEntry entry;
            entry._level     = level;
            entry._tag       = pEffectiveTag;
            entry._caller    = ( pEffectiveCaller != nullptr ) ? pEffectiveCaller : "";
            entry._message   = pEffectiveMsg;
            entry._file      = pEffectiveFile;
            entry._line      = line;
            entry._timeStamp = dateStr.c_str();
            entry._context   = context;
            ++t_broadcastDepth;
            listenersCopy.broadcast( entry );
            --t_broadcastDepth;
            _arrBroadcastInFlight[broadcastSlot].fetch_sub( 1, std::memory_order_acq_rel );
        }

        // 5단계: 비동기 I/O 큐에 넣는다(초기화 전이거나 큐가 가득 차면 이 스레드에서 바로 쓴다)
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
            // enqueue 가 실패했으면 record 는 옮겨지지 않았다. 그대로 동기로 쓴다.
            dispatchToOutputs( record );
            return;
        }

        _cv.notify_one();

        // 위의 "실행 중" 확인과 넣기 사이에 종료가 시작됐으면, 작업 스레드는 이미 큐를 마지막으로 비우고 끝났을 수 있다 — 그러면 이 줄은
        // 큐에 영영 남는다. 여기서 직접 비운다(장치가 이미 닫혔으면 장치가 조용히 버린다).
        if ( _bIsRunning.load( std::memory_order_acquire ) == false )
            flushQueue();
    }
} // namespace sw
