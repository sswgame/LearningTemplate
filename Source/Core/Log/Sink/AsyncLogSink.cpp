#include "pch.h"

#include "Core/Log/Sink/AsyncLogSink.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/StringUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/LogContext.h"
#include "Core/Log/Sink/ConsoleLogOutput.h"
#include "Core/Log/Sink/FileLogOutput.h"
#include "Core/Log/Sink/ILogOutput.h"
#include "Core/Log/Sink/JSONLinesLogOutput.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Process/ThreadCrashStack.h"
#include "Core/String/fixed_string.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

namespace sw
{
    namespace
    {
        /**
         * @struct LogReentryDepth
         * @brief 이 스레드가 싱크 안에서 되돌아 들어온 깊이입니다. 스레드마다 다른 값이라 싱크 멤버로 둘 수 없어 한 `thread_local` 에 모읍니다.
         */
        struct LogReentryDepth
        {
            /// @brief 지금 리스너를 부르는 중인 방송 깊이입니다(리스너 안에서 또 로그를 쓰면 겹친다). 리스너 제거가 자기 자신을 기다리지 않게 한다.
            uint32 broadcast{ 0 };

            /**
             * @brief 지금 출력 장치의 `write` 를 부르는 중인 깊이입니다(`dispatchToOutputs`).
             * @details 안전망입니다. 이 싱크의 락은 로그를 부르는 구간에 쥐지 않게 짰지만(`snapshotOutputs`), 장치는 **자기 락을 쥔 채** `write` 하고
             *          그 안에서 남기는 로그를 싱크가 막을 형식이 없습니다. 그 로그가 같은 장치의 `write` 로 되돌아오면 장치 락에서 영구 정지하거나(재진입 불가)
             *          끝없이 되풀이합니다. 그래서 이 깊이가 0 이 아닐 때 들어온 로그는 큐에 넣지 않고 stderr 로 바로 씁니다(버리지 않는다).
             *          `open` · `close` 에는 걸지 않습니다 — 그 안의 로그는 정상 경로로 가야 하고, 장치가 락을 쥐지 않는 것이 계약입니다(`ILogOutput::open`).
             */
            uint32 deviceWrite{ 0 };

            /// @brief 지금 "잠금을 쥔 채 로그" 단언을 보고하는 중인 깊이입니다. 단언의 보고도 로그로 오므로 그 줄은 다시 단언하지 않는다.
            uint32 heldLockReport{ 0 };
        };
        thread_local LogReentryDepth t_reentryDepth{};

#if SW_WITH_DYNAMIC_MODULES
        /**
         * @struct GlobalLogListenerUnloadListener
         * @brief 전역 로그 리스너(`Logger::registerGlobalListener`)를 모듈 언로드 리스너 목록에 올립니다. 리스너는 정적이라 리스너도 정적 하나입니다.
         */
        struct GlobalLogListenerUnloadListener final
        {

            uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepLibraryMapped )
            {
                (void)outKeepLibraryMapped;
                return Logger::releaseGlobalListenerCodeWithin( pBegin, pEnd );
            }

        private:
            /** @brief 모듈 언로드 처리기 구독(`ModuleLibraryUtil::registerUnloadHandler`)입니다. 맨 끝 멤버라 먼저 사라져, 다른 멤버가 사라지는 동안 불리지 않는다. */
            SW_MODULE_UNLOAD_HANDLER( &GlobalLogListenerUnloadListener::onModuleUnloading, "log listeners" );
        };

        GlobalLogListenerUnloadListener s_globalLogListenerUnloadListener;
#endif

        /**
         * @brief 스레드마다 둔 날짜 문자열 캐시입니다. 같은 초 안의 줄은 달력 변환(`WallClock::toLocalCalendar`) · 포맷을 다시 하지 않습니다.
         * @details 시각은 싱크와 상관없는 값이라 스레드마다 하나면 되고, 잠금이 필요 없습니다(전에는 싱크의 뮤텍스 하나를 모든 생산자가 줄마다 잡았다).
         */
        struct LogDateCache
        {
            std::time_t _timeSec{ -1 };
            int32       _year{ 0 };
            int32       _month{ 0 };
            int32       _day{ 0 };
            int32       _hour{ 0 };
            utf8        _arrDate[constant::kMaxBuffer32]{};
        };

        thread_local LogDateCache t_logDateCache{};

        struct AsyncLogSinkInternal
        {
            /** @brief 로그 줄에 찍는 시각 문자열과, 레코드가 나르는 달력 값(로그 파일 이름용)입니다. */
            struct DateStamp
            {
                fixed_string<constant::kMaxBuffer32> _text{};
                int32                                _year{ 0 };
                int32                                _month{ 0 };
                int32                                _day{ 0 };
                int32                                _hour{ 0 };
            };

            /** @brief 지금 시각을 읽습니다. 같은 초 안에서는 스레드의 캐시 문자열을 재사용합니다. */
            static DateStamp readDateStamp()
            {
                DateStamp         stamp{};
                LogDateCache&     cache   = t_logDateCache;
                const std::time_t timeSec = std::time( nullptr );
                if ( timeSec == cache._timeSec )
                {
                    stamp._text  = cache._arrDate;
                    stamp._year  = cache._year;
                    stamp._month = cache._month;
                    stamp._day   = cache._day;
                    stamp._hour  = cache._hour;
                    return stamp;
                }
                // 로그 줄은 날짜를 못 바꿔도 남긴다 — 바꾸지 못한 시각(플랫폼 범위 밖)은 0 날짜로 찍혀 눈에 띈다.
                LocalCalendarTime localTime{};
                (void)WallClock::toLocalCalendar( static_cast<int64>( timeSec ), localTime );
                stamp._year  = localTime._year;
                stamp._month = localTime._month;
                stamp._day   = localTime._day;
                stamp._hour  = localTime._hour;

                formatstring( cache._arrDate, sizeof( cache._arrDate ), "%#-%#-%# %#:%#:%#", stamp._year, stamp._month, stamp._day, stamp._hour, localTime._minute, localTime._second );
                stamp._text    = cache._arrDate;
                cache._timeSec = timeSec;
                cache._year    = stamp._year;
                cache._month   = stamp._month;
                cache._day     = stamp._day;
                cache._hour    = stamp._hour;
                return stamp;
            }

            /** @brief 시계 레이블(`Logger::setClockNumber` — 전용 서버의 틱, 리슨 호스트의 프레임)입니다. 정하지 않았으면 빈 글입니다. */
            static fixed_string<32> makeClockTag()
            {
                fixed_string<32> tickTag{};
                LogClock         clock{ LogClock::Tick };
                uint64           clockNumber{ 0 };
                if ( Logger::getClockNumber( clock, clockNumber ) )
                    formatstring( tickTag.data(), tickTag.capacity(), ( clock == LogClock::Frame ) ? "[frame=%#] " : "[tick=%#] ", clockNumber );
                return tickTag;
            }

            /**
             * @brief 줄의 종류 id(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`)를 본문 앞에 붙일 꼬리표 `[id:영역.사건] ` 로 만듭니다. id 가 없으면 빈 글입니다.
             * @details 자식 프로세스의 출력만 본 쪽(`AppScenarioTest` 의 로그 게이트)도 허용 목록의 id 로 줄을 가릴 수 있게 한다. 줄 전체가 버퍼를 넘으면 `formatLine` 이 자른다.
             */
            static fixed_string<128> makeIDTag( const utf8* pID )
            {
                fixed_string<128> idTag{};
                if ( StringUtil::isNullOrEmpty( pID ) == false )
                    formatstring( idTag.data(), idTag.capacity(), "%#%#%#", LogIDTag::kOpen, pID, LogIDTag::kClose );
                return idTag;
            }

            /** @brief 로그 한 줄을 @p outBuffer 에 한 번만 포맷합니다(스택 버퍼라 힙 할당이 없다). 호출자(caller)가 없으면 그 칸을 뺍니다. */
            static void formatLine( fixed_string<constant::kMaxBuffer8192>& outBuffer, const DateStamp& stamp, LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pTickTag,
                                    const utf8* pContextTag, const utf8* pIDTag, const utf8* pMessage, const utf8* pFile, int32 line )
            {
                static constexpr const utf8* kArrHeader[] = { "Error", "Warning", "Info", "Trace" };
                static_assert( SW_COUNT_OF( kArrHeader ) == static_cast<uint32>( LogLevel::Count ), "LogLevel과 같아야 합니다" );
                const utf8* pHeader = kArrHeader[static_cast<size_t>( level )];
                if ( StringUtil::isNullOrEmpty( pCaller ) == false )
                {
                    formatstring( outBuffer.data(), outBuffer.capacity(), "[%#] [%#] [%#] [%#] %#%#- %#%#\n -> %#:%#\n", stamp._text.c_str(), pTag, pCaller, pHeader, pTickTag, pContextTag,
                                  pIDTag, pMessage, pFile, line );
                }
                else
                {
                    formatstring( outBuffer.data(), outBuffer.capacity(), "[%#] [%#] [%#] %#%#- %#%#\n -> %#:%#\n", stamp._text.c_str(), pTag, pHeader, pTickTag, pContextTag, pIDTag,
                                  pMessage, pFile, line );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AsyncLogSink::AsyncLogSink( string fixedLogFilePath )
        : _onLogWritten{}
        , _listListenerSubscription{}
        , _listOutput{}
        , _pFileOutput{ nullptr }
        , _queue{}
        , _workerThread{}
        , _cv{}
        , _mutex{}
        , _cvMutex{}
        , _listenerSnapshot{}
        , _arrBroadcastInFlight{}
        , _broadcastEpoch{ 0 }
        , _bIsRunning{ false }
        , _bInitialized{ false }
        , _bHasListener{ false }
        , _bWantsStructured{ false }
        , _bJSONLinesEnabled{ false }
        , _bWorkerSleeping{ false }
    {
        // 기본 장치 두 개. 다른 구성이 필요하면 addOutput 으로 더 붙인다.
        auto fileOutput = make_unique<FileLogOutput>( std::move( fixedLogFilePath ) );
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

        // 장치는 `_mutex` **밖에서** 연다. 열다가 남기는 로그(로그 폴더를 못 만든 오류 등)는 이 싱크로 되돌아와 같은 `_mutex` 를 잡는다 —
        // 락을 쥔 채 열면 같은 스레드가 영구 정지한다(재진입 불가).
        ILogOutput*  arrDevice[_s_kMaxOutput]{};
        const uint32 deviceCount = snapshotOutputs( arrDevice );

        uint32 failedCount{ 0 };
        for ( uint32 index = 0; index < deviceCount; ++index )
        {
            // 열지 못한 장치는 쓰기를 무시한다. 장치가 이유를 이미 남겼으므로(락 밖이라 남길 수 있다) 여기서는 몇 개인지만 알린다.
            if ( arrDevice[index]->open() == false )
                ++failedCount;
        }
        if ( failedCount > 0 )
            SW_LOG_WARNING( "%# of %# log outputs could not be opened - they drop every line", failedCount, deviceCount );

        _bIsRunning.store( true, std::memory_order_release );
        _workerThread = sw::thread( &AsyncLogSink::workerLoop, this );
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

        // 장치는 `_mutex` 밖에서 닫는다(`initialize` 와 같은 이유 — 장치가 닫다가 남기는 로그가 이 싱크의 `_mutex` 로 되돌아온다).
        ILogOutput*  arrDevice[_s_kMaxOutput]{};
        const uint32 deviceCount = snapshotOutputs( arrDevice );
        for ( uint32 index = 0; index < deviceCount; ++index )
        {
            arrDevice[index]->close();
        }

        {
            ScopedLock<mutex> lock{ _mutex };
            _onLogWritten.removeAll();
            _listListenerSubscription.clear(); // removeAll 이 이미 무효로 만들었다 — 푸는 일 없이 비운다
            _subscriptions.detachAll();        // 바깥 구독도 무효로 — 뒤늦은 해제가 비운 목록을 찾지 않는다
            rebuildListenerSnapshot();
        }
        _bInitialized = false;
    }

    /**
     * @brief 로그 한 줄을 기록합니다(writeLogInternal 로 넘깁니다).
     */
    void AsyncLogSink::writeLogWithID( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID )
    {
        writeLogInternal( level, pTag, pCaller, pMessage, pFile, line, pID );
    }

    /**
     * @brief 로그가 쓰일 때 알림을 받을 리스너를 등록합니다(예: ImGui 에디터 콘솔 창).
     */
    Subscription AsyncLogSink::registerLogWrittenListener( const LogWrittenDelegate& listener )
    {
        ScopedLock<mutex>    lock{ _mutex };
        const DelegateHandle handle = DelegateHandle::allocate();
        _listListenerSubscription.emplace_back( handle, _onLogWritten.add( listener ) );
        rebuildListenerSnapshot();
        return _subscriptions.issue( handle._id );
    }

    /**
     * @brief 리스너의 구독이 풀렸습니다. 항목을 떼고, 떼기 **전에** 시작한 방송이 끝나기를 기다립니다.
     */
    void AsyncLogSink::releaseSubscription( uint64 id )
    {
        const DelegateHandle handle{ id };
        uint32               retiredSlot{ 0 };
        {
            ScopedLock<mutex> lock{ _mutex };
            // 안쪽 구독(멀티캐스트 항목)을 지우면 그 소멸자가 항목을 뺀다(_mutex 안이라 다른 등록 · 해제와 겹치지 않는다).
            const std::optional<size_t> index = VectorUtil::findIndexBy<&pair<DelegateHandle, Subscription>::first>( _listListenerSubscription, handle );
            if ( index.has_value() )
                VectorUtil::removeAtSwap( _listListenerSubscription, *index );
            rebuildListenerSnapshot();
            retiredSlot = retireBroadcastSlot();
        }
        waitForRetiredBroadcasts( retiredSlot );
    }

#if SW_WITH_DYNAMIC_MODULES
    uint32 AsyncLogSink::removeListenerCodeWithin( const void* pBegin, const void* pEnd )
    {
        // 모듈을 내리기 직전에 불린다. 다른 스레드의 방송이 그 모듈 코드 안에 있는 채로 이미지를 내리면 안 된다 — 끝날 때까지 기다린다.
        uint32 releasedCount{ 0 };
        uint32 retiredSlot{ 0 };
        {
            ScopedLock<mutex> lock{ _mutex };
            releasedCount = _onLogWritten.removeCodeWithin( pBegin, pEnd );
            rebuildListenerSnapshot();
            retiredSlot = retireBroadcastSlot();
        }
        waitForRetiredBroadcasts( retiredSlot );
        return releasedCount;
    }
#endif

    uint32 AsyncLogSink::retireBroadcastSlot()
    {
        const uint32 retiredSlot = _broadcastEpoch & 1u;
        ++_broadcastEpoch;
        return retiredSlot;
    }

    void AsyncLogSink::rebuildListenerSnapshot()
    {
        const bool bBound = _onLogWritten.isBound();
        _listenerSnapshot.reset();
        if ( bBound )
            _listenerSnapshot = sw::make_shared<LogWrittenMulticast>( _onLogWritten );
        _bHasListener.store( bBound, std::memory_order_release );
    }

    void AsyncLogSink::waitForRetiredBroadcasts( uint32 retiredSlot )
    {
        if ( t_reentryDepth.broadcast > 0 )
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
        const bool bStructured = output->needsStructuredFields();

        // 락 안에서는 목록만 만진다. 거절 경고(`SW_LOG_WARNING`)는 이 싱크로 되돌아와 같은 `_mutex` 를 잡고(`broadcastToListeners` ·
        // `dispatchToOutputs`), 리스너가 있으면 방송까지 하므로 락 밖에서 남긴다.
        // 상한을 **여기서** 확인한다. 디스패치는 고정 배열로 복사해 가므로, 일단 받아 두면 열어 놓고도 한 줄도 받지 못하는
        // 장치가 생긴다. 그 실패는 붙인 쪽에서 보이지 않는다.
        bool bRejected{ false };
        {
            ScopedLock<mutex> lock{ _mutex };
            bRejected = _listOutput.size() >= _s_kMaxOutput;
        }

        if ( bRejected == false )
        {
            // 열기도 락 밖이다 — 장치가 열다가 남기는 로그가 되돌아온다.
            const bool bNeedsOpen = _bInitialized;
            if ( bNeedsOpen )
                (void)output->open(); // 열지 못한 출력은 쓰기를 무시한다 — 장치가 이유를 남긴다

            // 열기 사이에 다른 스레드가 자리를 채웠을 수 있어 같은 락 안에서 다시 확인하고 넣는다.
            ScopedLock<mutex> lock{ _mutex };
            bRejected = _listOutput.size() >= _s_kMaxOutput;
            if ( bRejected == false )
            {
                _listOutput.push_back( std::move( output ) );
                if ( bStructured )
                    _bWantsStructured.store( true, std::memory_order_release );
            }
        }

        if ( bRejected )
        {
            SW_LOG_WARNING( "At most %# log outputs are allowed. Rejecting the extra one.", _s_kMaxOutput );
            return false;
        }
        return true;
    }

    bool AsyncLogSink::enableJSONLinesOutput()
    {
        if ( _bJSONLinesEnabled.exchange( true, std::memory_order_acq_rel ) )
            return true;
        if ( addOutput( make_unique<JSONLinesLogOutput>() ) == false )
        {
            _bJSONLinesEnabled.store( false, std::memory_order_release );
            return false;
        }
        // 장치가 달린 뒤에 콘솔을 끈다 — 그 사이의 줄은 두 모양으로 나가도 사라지지는 않는다.
        ConsoleLogOutput::setMuted( true );
        return true;
    }

    string_view AsyncLogSink::getLogFolderPath()
    {
        if ( _pFileOutput == nullptr )
            return {};
        return _pFileOutput->getLogFolderPath();
    }

    string AsyncLogSink::getLogFilePath()
    {
        if ( _pFileOutput == nullptr )
            return {};
        return _pFileOutput->getCurrentLogFilePath();
    }

    void AsyncLogSink::workerLoop()
    {
        SW_MEMORY_SCOPE( EngineMisc );
        // 이 스레드에서 스택이 넘쳐도 크래시 리포트가 남게 한다(ThreadCrashStack::initializeCurrentThread 설명).
        ThreadCrashStack::initializeCurrentThread();
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
                UniqueLock<mutex> lock{ _cvMutex };
                // 잠든다고 알린 **뒤에** 큐를 본다(아래 술어). 생산자는 넣은 뒤 이 값을 보므로 둘 중 하나는 상대를 본다.
                _bWorkerSleeping.store( true, std::memory_order_seq_cst );
                std::atomic_thread_fence( std::memory_order_seq_cst );
                _cv.wait_for( lock, std::chrono::milliseconds( 5 ), [this]
                {
                    return _bIsRunning.load( std::memory_order_relaxed ) == false || _queue.empty() == false;
                } );
                _bWorkerSleeping.store( false, std::memory_order_relaxed );
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
        ILogOutput*  arrDevice[_s_kMaxOutput]{};
        const uint32 deviceCount = snapshotOutputs( arrDevice );

        ++t_reentryDepth.deviceWrite;
        for ( uint32 index = 0; index < deviceCount; ++index )
        {
            arrDevice[index]->write( record );
        }
        --t_reentryDepth.deviceWrite;
    }

    uint32 AsyncLogSink::snapshotOutputs( ILogOutput* ( &outArrDevice )[_s_kMaxOutput] )
    {
        // `_mutex` 는 **잎 락**이다 — 이 안에서는 목록과 리스너 사본만 만지고 로그 · 장치 · 리스너를 부르지 않는다. 부르면 그 로그가 이 싱크로
        // 되돌아와 같은 `_mutex` 를 잡는다(재진입 불가 → 같은 스레드가 영구 정지). 장치 포인터를 복사해 락 밖에서 부르는 것이 그 규칙의 모양이다.
        uint32            deviceCount{ 0 };
        ScopedLock<mutex> lock{ _mutex };
        for ( unique_ptr<ILogOutput>& output : _listOutput )
        {
            if ( output != nullptr && deviceCount < _s_kMaxOutput )
                outArrDevice[deviceCount++] = output.get();
        }
        return deviceCount;
    }

    void AsyncLogSink::broadcastToListeners( const LogEntry& entry )
    {
        // 메모리 리스너(에디터 콘솔 UI · 테스트 캡처)의 불변 사본 참조를 잡고 락 밖에서 알린다(줄마다 목록을 복사하지 않는다). 그 세대 칸에
        // "부르는 중" 을 올려 두어, 리스너 제거가 이 방송이 끝날 때까지 기다리게 한다(`waitForRetiredBroadcasts`).
        shared_ptr<const LogWrittenMulticast> listenerSnapshot;
        uint32                                broadcastSlot{ 0 };
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _listenerSnapshot != nullptr )
            {
                listenerSnapshot = _listenerSnapshot;
                broadcastSlot    = _broadcastEpoch & 1u;
                _arrBroadcastInFlight[broadcastSlot].fetch_add( 1, std::memory_order_acq_rel );
            }
        }
        if ( listenerSnapshot == nullptr )
            return;
        ++t_reentryDepth.broadcast;
        listenerSnapshot->broadcastSnapshot( entry );
        --t_reentryDepth.broadcast;
        _arrBroadcastInFlight[broadcastSlot].fetch_sub( 1, std::memory_order_acq_rel );
    }

    void AsyncLogSink::submitRecord( LogRecord&& record )
    {
        // 초기화 전이거나 큐가 가득 차면 이 스레드에서 바로 쓴다.
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

        // 작업 스레드가 잠들려 하거나 잠들어 있을 때만 깨운다(줄마다 조건 변수의 내부 잠금을 잡지 않는다). 넣기 → 펜스 → 확인이 작업 스레드의
        // 알리기 → 펜스 → 큐 확인과 짝을 이룬다. `_cvMutex` 를 한 번 잡았다 놓아, 작업 스레드가 큐 확인과 잠들기 사이에 있으면 잠든 뒤에 깨우게 한다.
        std::atomic_thread_fence( std::memory_order_seq_cst );
        if ( _bWorkerSleeping.load( std::memory_order_relaxed ) )
        {
            {
                ScopedLock<mutex> lock{ _cvMutex };
            }
            _cv.notify_one();
        }

        // 위의 "실행 중" 확인과 넣기 사이에 종료가 시작됐으면, 작업 스레드는 이미 큐를 마지막으로 비우고 끝났을 수 있다 — 그러면 이 줄은
        // 큐에 영영 남는다. 여기서 직접 비운다(장치가 이미 닫혔으면 장치가 조용히 버린다).
        if ( _bIsRunning.load( std::memory_order_acquire ) == false )
            flushQueue();
    }

    void AsyncLogSink::writeLogInternal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID )
    {
        using Internal = AsyncLogSinkInternal;
        if ( t_reentryDepth.deviceWrite > 0 )
        {
            // 출력 장치가 `write` 안에서 로그를 남겼다 — 장치의 락과 이 줄이 되돌아갈 `write` 가 맞물려 멈추므로 장치를 거치지 않고 stderr 로 바로 쓴다.
            utf8 arrLine[constant::kMaxBuffer32]{};
            formatstring( arrLine, sizeof( arrLine ), "%#", line );
            std::fputs( "[Logger] Log written from inside a log output, redirected to stderr: ", stderr );
            std::fputs( pMessage != nullptr ? pMessage : "", stderr );
            std::fputs( " -> ", stderr );
            std::fputs( pFile != nullptr ? pFile : "", stderr );
            std::fputs( ":", stderr );
            std::fputs( arrLine, stderr );
            std::fputs( "\n", stderr );
            return;
        }
        // 잠금은 잎 잠금이다(결정 5-3) — 쥔 채 남긴 로그는 그 잠금을 다시 잡는 싱크 · 장치 · 리스너와 맞물려 멈출 수 있다.
        // 형식(-Wthread-safety)은 호출 그래프를 건너는 위반을 못 보므로 Debug 에서 쥔 잠금 수를 센다. 줄은 그대로 남긴다.
        // 단언은 `SW_ENFORCE_LEAF_LOCK_LOG`(CMake 옵션, 기본 꺼짐)일 때만 건다 — 아직 고치지 못한 위반 자리(Telemetry · RESP · 메트릭 등록 …)가 남아 있어
        // 켜면 테스트가 줄줄이 멈춘다. 자리를 다 고친 뒤 옵션을 켜고 이 주석을 지운다(NEXT 의 lockfmt 후속).
#if defined( SW_ENFORCE_LEAF_LOCK_LOG )
        const bool bLoggedUnderLock = mutex::getHeldLockDepth() != 0 && t_reentryDepth.heldLockReport == 0;
        if ( bLoggedUnderLock )
        {
            ++t_reentryDepth.heldLockReport;
            SW_LOW_LEVEL_ASSERT( bLoggedUnderLock == false, "Logged while holding a sw::mutex (%u held) - locks are leaf locks, log after releasing: %s",
                                 mutex::getHeldLockDepth(), pMessage != nullptr ? pMessage : "" );
            --t_reentryDepth.heldLockReport;
        }
#endif
        // 로그 레코드는 부른 쪽이 아니라 로거의 몫이다. 리스너(에디터 콘솔 등)는 자기 태그를 붙인다.
        SW_MEMORY_SCOPE( EngineMisc );
        // 1단계: 타임스탬프를 계산하고 포맷한다(같은 초 안에서는 캐시한 문자열을 재사용한다)
        const Internal::DateStamp stamp = Internal::readDateStamp();

        const utf8* pEffectiveTag    = ( StringUtil::isNullOrEmpty( pTag ) ) ? constant::kDefaultLogTag : pTag;
        const utf8* pEffectiveFile   = ( StringUtil::isNullOrEmpty( pFile ) ) ? constant::kDefaultLogFile : pFile;
        const utf8* pEffectiveMsg    = ( pMessage != nullptr ) ? pMessage : "";
        const utf8* pEffectiveCaller = pCaller;
        if ( StringUtil::isNullOrEmpty( pEffectiveCaller ) && pFile != nullptr )
            pEffectiveCaller = Logger::getCaller( pFile );

        // 문맥(요청 추적 id · 주체)은 부른 스레드의 것이라 큐에 넣기 전에 읽는다. 문맥이 없으면 꼬리표가 빈 글이라 줄은 바이트가 같다.
        const LogContext& context = LogContext::getCurrent();
        utf8              arrContextTag[LogContext::kMaxTagSize];
        (void)context.formatTag( arrContextTag, LogContext::kMaxTagSize );
        const fixed_string<32> tickTag = Internal::makeClockTag();

        // 2단계: 스택의 8KB fixed_string 버퍼에 한 번만 포맷한다(힙 할당 없음)
        fixed_string<constant::kMaxBuffer8192> formattedBuffer{};
        const fixed_string<128>                idTag = Internal::makeIDTag( pID );
        Internal::formatLine( formattedBuffer, stamp, level, pEffectiveTag, pEffectiveCaller, tickTag.c_str(), arrContextTag, idTag.c_str(), pEffectiveMsg, pEffectiveFile, line );

        // 3단계: 64비트 SWAR 로 UTF-8 인지 빠르게 검증하고, 아니면 **잘못된 바이트만** `\xNN` 으로 바꾼다. 줄 전체를 로캘 변환하면
        //        C 로캘(아무도 `setlocale` 을 부르지 않는다)이라 멀쩡한 한글까지 깨지거나(Windows) 줄이 빈다(glibc).
        string      fallbackUTF8;
        const utf8* pFormattedBuffer = formattedBuffer.c_str();
        if ( StringUtil::isValidUTF8( pFormattedBuffer ) == false )
        {
            fallbackUTF8     = StringUtil::escapeInvalidUTF8( string_view{ pFormattedBuffer } );
            pFormattedBuffer = fallbackUTF8.c_str();
        }

        // 4단계: 리스너에게 락 밖에서 알린다. 리스너가 없으면 락도 잡지 않는다.
        if ( _bHasListener.load( std::memory_order_acquire ) )
        {
            LogEntry entry;
            entry._level     = level;
            entry._tag       = pEffectiveTag;
            entry._caller    = ( pEffectiveCaller != nullptr ) ? pEffectiveCaller : "";
            entry._message   = pEffectiveMsg;
            entry._file      = pEffectiveFile;
            entry._line      = line;
            entry._pID       = pID;
            entry._timeStamp = stamp._text.c_str();
            entry._context   = context;
            broadcastToListeners( entry );
        }

        // 5단계: 비동기 I/O 큐에 넣는다
        LogRecord record;
        record._level     = level;
        record._formatted = pFormattedBuffer;
        record._year      = stamp._year;
        record._month     = stamp._month;
        record._day       = stamp._day;
        record._hour      = stamp._hour;
        if ( _bWantsStructured.load( std::memory_order_acquire ) )
        {
            // 메시지만 UTF-8 이 아닐 수 있다(태그 · 호출자 · 파일은 소스의 글자 상수). 잘못된 바이트는 줄 전체와 같은 방식으로 바꾼다.
            string      escapedMessage;
            string_view message{ pEffectiveMsg };
            if ( StringUtil::isValidUTF8( pEffectiveMsg ) == false )
            {
                escapedMessage = StringUtil::escapeInvalidUTF8( message );
                message        = escapedMessage;
            }
            JSONLogFields fields;
            fields._time             = stamp._text.c_str();
            fields._unixMilliseconds = WallClock::nowUnixMilliseconds().count();
            fields._level            = level;
            fields._tag              = pEffectiveTag;
            fields._caller           = pEffectiveCaller != nullptr ? string_view{ pEffectiveCaller } : string_view{};
            fields._message          = message;
            fields._id               = pID != nullptr ? string_view{ pID } : string_view{};
            fields._file             = pEffectiveFile;
            fields._line             = line;
            fields._context          = context;
            LogClock clock{ LogClock::Tick };
            fields._bHasClock = Logger::getClockNumber( clock, fields._clockNumber );
            fields._clock     = clock;
            JSONLinesLogOutput::appendLine( record._json, fields );
        }
        submitRecord( std::move( record ) );
    }
} // namespace sw
