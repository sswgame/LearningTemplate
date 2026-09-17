#include "pch.h"

#include "Core/String/StringUtil.h"

#include "TestFramework/TestFramework.h"

SW_LOG_CALLER( "TestLog" );

namespace
{
    // LogCapture 는 **모든 빌드**에서 쓴다 — 로그가 Debug 전용이 아니게 됐으므로 검증도 그래야 한다.
    /** @brief 로그 매크로가 실제로 기록한 내용을 확인할 때 붙이는 메시지 접두사입니다. */
    constexpr const utf8* kCapturePrefix = "[TestLog] ";

    /**
     * @class LogCapture
     * @brief 전역 싱크에 리스너를 붙여 접두사가 맞는 LogEntry만 모으는 테스트 싱크
     * @note 접두사로 걸러 다른 스레드/서브시스템의 로그가 섞이지 않게 합니다.
     */
    class LogCapture
    {
    public:
        /** @brief 전역 싱크에 리스너를 등록합니다. */
        LogCapture()
        {
            _handle = sw::Logger::addGlobalListener( SW_DELEGATE_LAMBDA(
                sw::LogWrittenDelegate,
                [this]( const sw::LogEntry& entry )
            {
                if ( sw::StringUtil::startsWith( entry._message, kCapturePrefix ) )
                    _listEntry.push_back( entry );
            } ) );
        }

        /** @brief 전역 싱크에서 리스너를 뗍니다. */
        ~LogCapture()
        {
            sw::Logger::removeGlobalListener( _handle );
        }

        LogCapture( const LogCapture& )            = delete;
        LogCapture& operator=( const LogCapture& ) = delete;

        /** @brief 리스너가 실제로 등록되었는지 반환합니다. */
        bool isAttached() const
        {
            return _handle.isValid();
        }

        /** @brief 수집된 로그 목록입니다. */
        [[maybe_unused]] const sw::vector<sw::LogEntry>& getEntries() const
        {
            return _listEntry;
        }

        /** @brief 수집된 로그 개수입니다. */
        uint32 getCount() const
        {
            return static_cast<uint32>( _listEntry.size() );
        }

    private:
        sw::vector<sw::LogEntry> _listEntry;
        sw::DelegateHandle       _handle;
    };

// 이 캡처는 Debug 전용 테스트에서만 쓴다 — Verbose/Trace 가 컴파일되지 않는 빌드에서는 잡을 로그가 없다.
// 쓰는 쪽과 같은 가드 안에 둔다(밖에 두면 아무도 안 쓰는 멤버가 된다).
#if defined( SW_DEBUG )
    /**
     * @class ThreadSafeLogCapture
     * @brief 멀티스레드 동시 로깅 테스트용 스레드 안전 로그 캡처 싱크
     */
    class ThreadSafeLogCapture
    {
    public:
        ThreadSafeLogCapture()
        {
            _handle = sw::Logger::addGlobalListener( SW_DELEGATE_LAMBDA(
                sw::LogWrittenDelegate,
                [this]( const sw::LogEntry& entry )
            {
                if ( sw::StringUtil::startsWith( entry._message, kCapturePrefix ) )
                {
                    std::scoped_lock<sw::mutex> lock{ _mutex };
                    _listEntry.push_back( entry );
                }
            } ) );
        }

        ~ThreadSafeLogCapture()
        {
            sw::Logger::removeGlobalListener( _handle );
        }

        ThreadSafeLogCapture( const ThreadSafeLogCapture& )            = delete;
        ThreadSafeLogCapture& operator=( const ThreadSafeLogCapture& ) = delete;

        bool isAttached() const
        {
            return _handle.isValid();
        }

        [[maybe_unused]] sw::vector<sw::LogEntry> getEntries() const
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            return _listEntry;
        }

        uint32 getCount() const
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            return static_cast<uint32>( _listEntry.size() );
        }

    private:
        mutable sw::mutex        _mutex;
        sw::vector<sw::LogEntry> _listEntry;
        sw::DelegateHandle       _handle;
    };
#endif // SW_DEBUG
} // namespace

// ------------------------------------------------------------------------------
// 1) Core_Log — 초기화·폴더·매크로
// ------------------------------------------------------------------------------
/**
 * @brief [LogTest] 로거 초기화
 */
SW_TEST_CASE( LogTest, LoggerInitialized )
{
    const sw::string& folderPath = sw::Logger::getGlobalSink()->getLogFolderPath();
    SW_EXPECT_FALSE( folderPath.empty() );
}

/**
 * @brief [LogTest] 로그 폴더 존재
 */
SW_TEST_CASE( LogTest, LogFolderExists )
{
    const sw::string& folderPath = sw::Logger::getGlobalSink()->getLogFolderPath();
    if ( folderPath.empty() == false )
        SW_EXPECT_TRUE( sw::FileUtil::directoryExists( folderPath ) );
}

/**
 * @brief [LogTest] 각 레벨의 로그가 싱크까지 전달됨
 */
SW_TEST_CASE( LogTest, WriteLogDeliversEveryLevel )
{
#if defined( SW_DEBUG )
    LogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    // 기본 런타임 상세도는 Info 라 Trace 가 버려진다 — 이 테스트는 **네 레벨 모두**를 보는 것이
    // 목적이므로 잠깐 올렸다가 되돌린다(컴파일 상한과 런타임 상세도는 다른 축이다).
    const sw::LogLevel previousVerbosity = sw::Logger::getRuntimeVerbosity();
    sw::Logger::setRuntimeVerbosity( sw::LogLevel::Trace );
    const struct VerbosityRestore
    {
        sw::LogLevel _previous;
        ~VerbosityRestore() { sw::Logger::setRuntimeVerbosity( _previous ); }
    } verbosityRestore{ previousVerbosity };

    SW_LOG_INFO( "[TestLog] Info level log" );
    SW_LOG_WARNING( "[TestLog] Warning level log" );
    SW_LOG_ERROR( "[TestLog] Error level log" );
    SW_LOG_TRACE( "[TestLog] Trace level log" );

    constexpr sw::LogLevel expectedLevels[] = {
        sw::LogLevel::Info,
        sw::LogLevel::Warning,
        sw::LogLevel::Error,
        sw::LogLevel::Trace,
    };
    constexpr const utf8* expectedMessages[] = {
        "[TestLog] Info level log",
        "[TestLog] Warning level log",
        "[TestLog] Error level log",
        "[TestLog] Trace level log",
    };
    constexpr uint32 expectedCount = static_cast<uint32>( SW_COUNT_OF( expectedLevels ) );

    SW_ASSERT_EQUAL( expectedCount, capture.getCount() );

    for ( uint32 logIndex = 0; logIndex < expectedCount; ++logIndex )
    {
        const sw::LogEntry& entry = capture.getEntries()[logIndex];
        SW_EXPECT_EQUAL( static_cast<uint32>( expectedLevels[logIndex] ), static_cast<uint32>( entry._level ) );
        SW_EXPECT_STREQ( expectedMessages[logIndex], entry._message );
        SW_EXPECT_STREQ( SW_LOG_TAG, entry._tag );
        SW_EXPECT_FALSE( entry._file.empty() );
        SW_EXPECT_TRUE( entry._line > 0 );
    }
#else
    SW_TEST_SKIP( "SW_LOG_* is compiled out when SW_DEBUG is undefined" );
#endif
}

/**
 * @brief [LogTest] 런타임 상세도가 그보다 덜 심각한 줄을 버리는지
 * @details 배포본에서 고객에게 상세도를 올려 재현을 받는 것이 이 값의 용도다. 컴파일 상한과는 다른
 *          축이다 — 상한은 "무엇을 남길 수 있나", 이 값은 "지금 무엇을 남길까" 를 정한다.
 */
SW_TEST_CASE( LogTest, RuntimeVerbosityDropsLessSevere )
{
    LogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    const sw::LogLevel previousVerbosity = sw::Logger::getRuntimeVerbosity();
    const struct VerbosityRestore
    {
        sw::LogLevel _previous;
        ~VerbosityRestore() { sw::Logger::setRuntimeVerbosity( _previous ); }
    } verbosityRestore{ previousVerbosity };

    // Error 만 남기게 하면 Warning 아래는 버려져야 한다.
    sw::Logger::setRuntimeVerbosity( sw::LogLevel::Error );
    SW_EXPECT_TRUE( sw::Logger::shouldLog( sw::LogLevel::Error ) );
    SW_EXPECT_TRUE( sw::Logger::shouldLog( sw::LogLevel::Warning ) == false );

    SW_LOG_ERROR( "[TestLog] kept" );
    SW_LOG_WARNING( "[TestLog] dropped" );
    SW_LOG_INFO( "[TestLog] dropped" );
    SW_EXPECT_EQUAL( 1u, capture.getCount() );

    // Warning 까지 올리면 배포본 기본(컴파일 상한 Warning)과 같은 상태가 된다.
    sw::Logger::setRuntimeVerbosity( sw::LogLevel::Warning );
    SW_EXPECT_TRUE( sw::Logger::shouldLog( sw::LogLevel::Warning ) );
    SW_EXPECT_TRUE( sw::Logger::shouldLog( sw::LogLevel::Info ) == false );
}

/**
 * @brief [LogTest] Error/Warning 은 **모든 빌드**에 컴파일되는지
 * @details 예전에는 SW_DEBUG 가 아니면 매크로가 통째로 사라져 배포본에 로그가 하나도 없었다.
 *          이 단언이 그 회귀를 막는다 — 상한이 Warning 밑으로 내려가면 여기서 걸린다.
 */
SW_TEST_CASE( LogTest, ErrorAndWarningSurviveEveryBuild )
{
    static_assert( SW_LOG_COMPILED_VERBOSITY >= 1, "Error/Warning 은 어떤 빌드에서도 컴파일되어야 한다" );
    SW_EXPECT_TRUE( SW_LOG_LEVEL_COMPILED( 0 ) );
    SW_EXPECT_TRUE( SW_LOG_LEVEL_COMPILED( 1 ) );
}

/**
 * @brief [LogTest] 로그 레벨 개수
 */
SW_TEST_CASE( LogTest, LogLevelCount )
{
    constexpr uint32 expected = 4u;
    SW_EXPECT_EQUAL( expected, static_cast<uint32>( sw::LogLevel::Count ) );
}

/**
 * @brief [LogTest] 로그 매크로가 %# 인자를 치환해 기록함
 */
SW_TEST_CASE( LogTest, LogMacrosFormatArguments )
{
#if defined( SW_DEBUG )
    LogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    // 기본 런타임 상세도는 Info 라 Trace 가 버려진다 — 이 테스트는 **네 레벨 모두**를 보는 것이
    // 목적이므로 잠깐 올렸다가 되돌린다(컴파일 상한과 런타임 상세도는 다른 축이다).
    const sw::LogLevel previousVerbosity = sw::Logger::getRuntimeVerbosity();
    sw::Logger::setRuntimeVerbosity( sw::LogLevel::Trace );
    const struct VerbosityRestore
    {
        sw::LogLevel _previous;
        ~VerbosityRestore() { sw::Logger::setRuntimeVerbosity( _previous ); }
    } verbosityRestore{ previousVerbosity };

    SW_LOG_INFO( "[TestLog] SW_LOG_INFO %#", 123 );
    SW_LOG_WARNING( "[TestLog] SW_LOG_WARNING %#", "warning" );
    SW_LOG_ERROR( "[TestLog] SW_LOG_ERROR %#", 404 );
    SW_LOG_TRACE( "[TestLog] SW_LOG_TRACE %#", 3.14f );

    SW_ASSERT_EQUAL( 4u, capture.getCount() );
    SW_EXPECT_STREQ( "[TestLog] SW_LOG_INFO 123", capture.getEntries()[0]._message );
    SW_EXPECT_STREQ( "[TestLog] SW_LOG_WARNING warning", capture.getEntries()[1]._message );
    SW_EXPECT_STREQ( "[TestLog] SW_LOG_ERROR 404", capture.getEntries()[2]._message );

    // 실수 자릿수는 기본 정밀도에 달려 있으므로 앞부분만 확인합니다.
    const sw::string& traceMessage = capture.getEntries()[3]._message;
    SW_EXPECT_TRUE( traceMessage.rfind( "[TestLog] SW_LOG_TRACE 3.14", 0 ) == 0 );
#else
    SW_TEST_SKIP( "SW_LOG_* is compiled out when SW_DEBUG is undefined" );
#endif
}

/**
 * @brief [LogTest] 비-UTF8(ANSI/CP949) 문자열 전달 시에도 안전하게 처리됨
 */
SW_TEST_CASE( LogTest, NonUtf8FallbackSafety )
{
#if defined( SW_DEBUG )
    LogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    // 유효하지 않은 UTF-8 바이트 시퀀스 (0xFF, 0xFE 등)
    const utf8 invalidUtf8Bytes[] = { '[', 'T', 'e', 's', 't', 'L', 'o', 'g', ']', ' ', 'B', 'a', 'd', ':', static_cast<utf8>( 0xFF ), static_cast<utf8>( 0xFE ), '\0' };
    SW_LOG_INFO( "%#", invalidUtf8Bytes );

    SW_ASSERT_EQUAL( 1u, capture.getCount() );
    SW_EXPECT_TRUE( capture.getEntries()[0]._message.rfind( "[TestLog] Bad:", 0 ) == 0 );
#else
    SW_TEST_SKIP( "SW_LOG_* is compiled out when SW_DEBUG is undefined" );
#endif
}

/**
 * @brief [LogTest] Caller 지정 및 수신 검증
 */
SW_TEST_CASE( LogTest, LogCallerHandling )
{
#if defined( SW_DEBUG )
    LogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    SW_LOG_INFO( "[TestLog] Custom caller message" );
    SW_LOG_WARNING( "[TestLog] Warning from caller" );

    SW_ASSERT_EQUAL( 2u, capture.getCount() );
    SW_EXPECT_STREQ( "TestLog", capture.getEntries()[0]._caller );
    SW_EXPECT_STREQ( "[TestLog] Custom caller message", capture.getEntries()[0]._message );
    SW_EXPECT_STREQ( "TestLog", capture.getEntries()[1]._caller );
    SW_EXPECT_STREQ( "[TestLog] Warning from caller", capture.getEntries()[1]._message );
#else
    SW_TEST_SKIP( "SW_LOG_* is compiled out when SW_DEBUG is undefined" );
#endif
}

/**
 * @brief [LogTest] 8개 스레드 동시 대량 로깅 스트레스 테스트 (데이터 레이스/크래시 검증)
 */
SW_TEST_CASE( LogTest, ConcurrentMultiThreadedLogging )
{
#if defined( SW_DEBUG )
    ThreadSafeLogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    constexpr uint32 kThreadCount       = 8;
    constexpr uint32 kLogsPerThread     = 100;
    constexpr uint32 kExpectedTotalLogs = kThreadCount * kLogsPerThread;

    std::thread arrWorker[kThreadCount];
    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        arrWorker[threadIndex] = std::thread( [threadIndex]
        {
            for ( uint32 logIndex = 0; logIndex < kLogsPerThread; ++logIndex )
            {
                SW_LOG_INFO( "[TestLog] Thread %# Log %#", threadIndex, logIndex );
            }
        } );
    }

    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        if ( arrWorker[threadIndex].joinable() )
            arrWorker[threadIndex].join();
    }

    SW_EXPECT_EQUAL( kExpectedTotalLogs, capture.getCount() );
#else
    SW_TEST_SKIP( "SW_LOG_* is compiled out when SW_DEBUG is undefined" );
#endif
}

/**
 * @brief [LogTest] 큐 용량(4096) 초과 시 동기 폴백 안전성 검증
 */
SW_TEST_CASE( LogTest, QueueOverflowFallbackStress )
{
#if defined( SW_DEBUG )
    ThreadSafeLogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    constexpr uint32 kHeavyLogCount = 6000;
    for ( uint32 logIndex = 0; logIndex < kHeavyLogCount; ++logIndex )
    {
        SW_LOG_INFO( "[TestLog] Heavy burst log %#", logIndex );
    }

    SW_EXPECT_EQUAL( kHeavyLogCount, capture.getCount() );
#else
    SW_TEST_SKIP( "SW_LOG_* is compiled out when SW_DEBUG is undefined" );
#endif
}

/**
 * @brief [LogTest] 로거 shutdown 시 큐 잔여 로그 플러시(Drain) 일관성 검증
 */
SW_TEST_CASE( LogTest, ShutdownQueueDrainConsistency )
{
#if defined( SW_DEBUG )
    ThreadSafeLogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    constexpr uint32 kDrainCount = 500;
    for ( uint32 logIndex = 0; logIndex < kDrainCount; ++logIndex )
    {
        SW_LOG_INFO( "[TestLog] Drain check %#", logIndex );
    }

    SW_EXPECT_EQUAL( kDrainCount, capture.getCount() );
#else
    SW_TEST_SKIP( "SW_LOG_* is compiled out when SW_DEBUG is undefined" );
#endif
}

/**
 * @brief [LogTest] 로깅 진행 중 동시 리스너 등록/해제 동시성 검증
 */
SW_TEST_CASE( LogTest, ConcurrentListenerAttachDetach )
{
#if defined( SW_DEBUG )
    std::thread emitter( []
    {
        for ( uint32 index = 0; index < 500; ++index )
        {
            SW_LOG_TRACE( "[TestLog] Background emitter message %#", index );
        }
    } );

    // 리스너 등록/해제 100회 반복
    for ( uint32 iteration = 0; iteration < 100; ++iteration )
    {
        sw::DelegateHandle handle = sw::Logger::addGlobalListener( SW_DELEGATE_LAMBDA(
            sw::LogWrittenDelegate,
            []( const sw::LogEntry& ) {} ) );
        SW_EXPECT_TRUE( handle.isValid() );
        sw::Logger::removeGlobalListener( handle );
    }

    if ( emitter.joinable() )
        emitter.join();
#else
    SW_TEST_SKIP( "SW_LOG_* is compiled out when SW_DEBUG is undefined" );
#endif
}

namespace
{
    /** @brief 아무 데도 쓰지 않고 받은 줄 수만 세는 시험용 출력 장치. */
    class CountingLogOutput final : public sw::ILogOutput
    {
    public:
        bool open() override { return true; }
        void close() override {}
        void write( const sw::LogRecord& ) override { ++_writeCount; }

        uint32 _writeCount{ 0 };
    };
} // namespace

/**
 * @brief [LogTest] 출력 장치 상한을 넘기면 **거절하고 경고한다** (조용히 삼키지 않는다)
 * @details 디스패치는 잠금 안에서 포인터만 고정 배열로 떠 와 락 밖에서 쓴다 — 느린 파일 I/O 가
 *          콘솔을 막지 않게 하는 분리다. 그 배열이 8개에서 잘리는데 `addOutput` 은 그 사실을 몰라서,
 *          9번째부터는 **받아서 `open` 까지 해 놓고 한 줄도 주지 않았다.** 붙인 자리에서는 보이지 않는
 *          실패다. 이제 상한에서 거절한다 — 그래서 "달린 장치는 반드시 받는다" 가 참이 된다.
 */
SW_TEST_CASE( LogTest, OutputsBeyondTheCapAreRejectedNotSilentlyIgnored )
{
    sw::Logger logger;
    logger.initialize();

    // 기본으로 콘솔·파일이 달려 있다. 남은 자리를 시험용 장치로 채우고, 한 개 더 시도한다.
    sw::vector<CountingLogOutput*> listAttached;
    for ( uint32 attempt = 0; attempt < 16; ++attempt )
    {
        sw::unique_ptr<CountingLogOutput> output = sw::make_unique<CountingLogOutput>();
        CountingLogOutput*                pRaw   = output.get();
        // 거절되면 `output` 은 그 자리에서 파괴된다 — 그래서 **받아들여진 것만** 들고 있는다.
        // 반환값이 없던 시절에는 이것을 알 방법이 없어, 죽은 포인터를 들고 있게 된다.
        if ( logger.addOutput( std::move( output ) ) )
            listAttached.push_back( pRaw );
    }

    logger.writeLog( sw::LogLevel::Error, "Test", "Cap", "한 줄", __FILE__, __LINE__ );
    logger.shutdown();

    // 거절되지 않고 달린 장치는 **전부** 그 줄을 받아야 한다. 예전에는 8번째 뒤로 0 이었다.
    uint32 attachedCount = 0;
    uint32 silentCount   = 0;
    for ( CountingLogOutput* pOutput : listAttached )
    {
        if ( pOutput->_writeCount > 0 )
            ++attachedCount;
        else
            ++silentCount;
    }

    SW_EXPECT_TRUE_MSG( attachedCount > 0, "달린 장치가 한 줄도 받지 못했다" );
    SW_EXPECT_TRUE_MSG( silentCount == 0,
                        "받아 놓고 한 줄도 주지 않은 출력 장치가 있다 — 상한을 넘겼으면 거절했어야 한다" );
}
