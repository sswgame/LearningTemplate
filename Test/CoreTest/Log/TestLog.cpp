#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/File/FileUtil.h"
#include "Core/LogSink/AsyncLogSink.h"
#include "Core/LogSink/ConsoleLogOutput.h"
#include "Core/LogSink/FileLogOutput.h"
#include "Core/LogSink/ILogOutput.h"
#include "Core/Process/ProcessEnvironment.h"

#include "TestFramework/Harness/TestChildProcess.h"
#include "TestFramework/TestFramework.h"

#include <cstdlib>

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
            _handle = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA(
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
            sw::Logger::unregisterGlobalListener( _handle );
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
            _handle = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA(
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
            sw::Logger::unregisterGlobalListener( _handle );
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

    /** @brief 아무 데도 쓰지 않고 받은 줄 수만 세는 테스트용 출력 장치. */
    class CountingLogOutput final : public sw::ILogOutput
    {
    public:
        [[nodiscard]] bool open() override { return true; }
        void               close() override {}
        void               write( const sw::LogRecord& ) override { ++_writeCount; }

        uint32 _writeCount{ 0 };
    };

    /** @brief 받은 줄의 완성된 글(`LogRecord::_formatted` — 파일 · 콘솔이 쓰는 그것)을 모으는 테스트용 출력 장치. */
    class CapturingLogOutput final : public sw::ILogOutput
    {
    public:
        [[nodiscard]] bool open() override { return true; }
        void               close() override {}
        void               write( const sw::LogRecord& record ) override { _listFormatted.push_back( record._formatted ); }

        sw::vector<sw::string> _listFormatted;
    };

    /**
     * @class BlockedFolderLogOutput
     * @brief 열 때 폴더를 만드는 장치입니다. 길을 파일이 막고 있으면 `FileUtil` 이 **열다가** 오류 로그를 남깁니다(`FileLogOutput` 이 로그 폴더를 못 만들 때와 같은 모양).
     */
    class BlockedFolderLogOutput final : public sw::ILogOutput
    {
    public:
        explicit BlockedFolderLogOutput( sw::string blockedFolder )
            : _blockedFolder{ std::move( blockedFolder ) }
            , _listFormatted{}
            , _bFolderReady{ true }
        {
        }

        [[nodiscard]] bool open() override
        {
            _bFolderReady = sw::FileUtil::ensureDirectoryExists( _blockedFolder );
            return _bFolderReady;
        }
        void close() override {}
        void write( const sw::LogRecord& record ) override { _listFormatted.push_back( record._formatted ); }

        sw::string             _blockedFolder;
        sw::vector<sw::string> _listFormatted;
        bool                   _bFolderReady;
    };

    /**
     * @class SelfLoggingLogOutput
     * @brief `open` 이나 첫 `write` 안에서 로그를 남기는 장치입니다 — 장치가 부르는 쪽으로 로그를 되돌리는 모양을 만듭니다.
     */
    class SelfLoggingLogOutput final : public sw::ILogOutput
    {
    public:
        SelfLoggingLogOutput( bool bLogInOpen, bool bLogInWrite )
            : _openCount{ 0 }
            , _writeCount{ 0 }
            , _bLogInOpen{ bLogInOpen }
            , _bLogInWrite{ bLogInWrite }
        {
        }

        [[nodiscard]] bool open() override
        {
            ++_openCount;
            if ( _bLogInOpen )
                SW_LOG_WARNING( "[Probe] a log written while the device is opening" );
            return true;
        }
        void close() override {}
        void write( const sw::LogRecord& ) override
        {
            ++_writeCount;
            if ( _bLogInWrite && _writeCount == 1 )
                SW_LOG_WARNING( "[Probe] a log written from inside write" );
        }

        uint32 _openCount;
        uint32 _writeCount;
        bool   _bLogInOpen;
        bool   _bLogInWrite;
    };

    /** @brief 스코프 동안 전역 싱크를 @p pSink 로 바꿉니다 — `SW_LOG_*` 가 그 싱크로 들어가게 해, 싱크가 자기 로그를 받는 경로를 만든다. 싱크보다 **나중에** 만들어야 한다(소멸이 거꾸로). */
    class GlobalSinkSwap
    {
    public:
        explicit GlobalSinkSwap( sw::ILogSink* pSink )
            : _pPrevious{ sw::Logger::getGlobalSink() }
        {
            sw::Logger::setGlobalSink( pSink );
        }
        ~GlobalSinkSwap() { sw::Logger::setGlobalSink( _pPrevious ); }

        GlobalSinkSwap( const GlobalSinkSwap& )            = delete;
        GlobalSinkSwap& operator=( const GlobalSinkSwap& ) = delete;

    private:
        sw::ILogSink* _pPrevious;
    };

    /** @brief 자식 프로세스가 돌릴 교착 시나리오 이름을 담는 환경 변수. */
    constexpr const utf8* kDeadlockChildEnvironment = "SW_TEST_LOG_DEADLOCK_CHILD";

    /** @brief 시나리오 하나를 자식 프로세스로 돌린다 — 교착이면 시한에 자식이 죽고 `_bTimedOut` 이 선다(같은 프로세스에서 돌면 테스트 전체가 멈춘다). */
    test::ProcessRunResult runDeadlockScenario( const utf8* pScenario )
    {
        const test::ChildEnvironmentVariable arrEnvironment[] = {
            { kDeadlockChildEnvironment, pScenario }
        };
        return test::runThisExecutableAsChild( "LogTest.DeadlockChildRoleRunsAScenario", arrEnvironment, 30 );
    }

    /** @brief 이 날짜 · 시각의 레코드 하나. 연도를 먼 미래로 두어 실제 로그 파일과 이름이 겹치지 않게 한다. */
    sw::LogRecord makeProbeRecord( int32 hour, sw::LogLevel level, const utf8* pText )
    {
        sw::LogRecord record{};
        record._formatted = pText;
        record._year      = 2099;
        record._month     = 1;
        record._day       = 2;
        record._hour      = hour;
        record._level     = level;
        return record;
    }

    /** @brief `FileLogOutput` 이 그 시각에 쓰는 파일의 경로(`LOG_<연>-<월>-<일>-<시>_<세션>.txt`). */
    sw::string makeProbeLogPath( const sw::FileLogOutput& output, int32 hour )
    {
        sw::string fileName{ "LOG_2099-1-2-" };
        fileName += sw::to_string( hour );
        fileName += "_";
        fileName += sw::getCrashSessionID();
        fileName += ".txt";
        return sw::FileUtil::joinPath( output.getLogFolderPath(), fileName );
    }
} // namespace

// ------------------------------------------------------------------------------
// 1) Core_Log — 초기화·폴더·매크로
// ------------------------------------------------------------------------------
/**
 * @brief [LogTest] 로거 초기화
 */
SW_TEST_CASE( LogTest, LoggerInitialized )
{
    const sw::string_view folderPath = sw::Logger::getGlobalSink()->getLogFolderPath();
    SW_EXPECT_FALSE( folderPath.empty() );
}

/**
 * @brief [LogTest] 로그 폴더 존재
 */
SW_TEST_CASE( LogTest, LogFolderExists )
{
    const sw::string_view folderPath = sw::Logger::getGlobalSink()->getLogFolderPath();
    if ( folderPath.empty() == false )
        SW_EXPECT_TRUE( sw::FileUtil::isDirectory( folderPath ) );
}

/**
 * @brief [LogTest] 각 레벨의 로그가 싱크까지 전달됨
 */
SW_TEST_CASE( LogTest, WriteLogDeliversEveryLevel )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
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
 * @details SW_DEBUG 가 아닐 때 매크로가 통째로 사라지면 배포본에 로그가 하나도 없다.
 *          상한이 Warning 밑으로 내려가면 여기서 걸린다.
 */
SW_TEST_CASE( LogTest, ErrorAndWarningSurviveEveryBuild )
{
    static_assert( SW_LOG_COMPILED_VERBOSITY >= SW_LOG_VERBOSITY_WARNING, "Error/Warning 은 어떤 빌드에서도 컴파일되어야 한다" );
    SW_EXPECT_TRUE( SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_ERROR ) );
    SW_EXPECT_TRUE( SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_WARNING ) );
}

/**
 * @brief [LogTest] id 가 붙은 줄(`SW_LOG_*_ID`)은 문구와 상관없이 id 로 찾아지고, id 없는 줄은 어떤 id 로도 잡히지 않으며, 방어 구간의 감싼 싱크도 id 를 넘긴다
 */
SW_TEST_CASE( LogTest, LinesAreFoundByIDNotWording )
{
    test::ScopedLogCollector logs;
    SW_TEST_DEFENSIVE_SCOPE( "the wrapped sink must keep the id" ); // 감싼 싱크가 id 를 넘기지 않으면 아래 세기가 0 이 된다
    SW_LOG_WARNING_ID( "Test.LogIDProbe", "first wording %#", 1 );
    SW_LOG_ERROR_ID( "Test.LogIDProbe", "completely different wording" );
    SW_LOG_WARNING_ID( "Test.LogIDOther", "third" );
    SW_LOG_WARNING( "Test.LogIDProbe is only a word in a line without an id" );
    SW_EXPECT_EQUAL( 2u, logs.countWithID( "Test.LogIDProbe" ) );
    SW_EXPECT_EQUAL( 1u, logs.countWithID( "Test.LogIDOther" ) );
    SW_EXPECT_EQUAL( 0u, logs.countWithID( "Test.LogIDMissing" ) );
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
    SW_TEST_REQUIRES_FEATURE( DebugLog );
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
    SW_EXPECT_TRUE( sw::StringUtil::startsWith( traceMessage, "[TestLog] SW_LOG_TRACE 3.14" ) );
#endif
}

/**
 * @brief [LogTest] 비-UTF8(ANSI/CP949) 문자열 전달 시에도 안전하게 처리됨
 */
SW_TEST_CASE( LogTest, NonUtf8FallbackSafety )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    LogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    // 유효하지 않은 UTF-8 바이트 시퀀스 (0xFF, 0xFE 등)
    const utf8 invalidUtf8Bytes[] = { '[', 'T', 'e', 's', 't', 'L', 'o', 'g', ']', ' ', 'B', 'a', 'd', ':', static_cast<utf8>( 0xFF ), static_cast<utf8>( 0xFE ), '\0' };
    SW_LOG_INFO( "%#", invalidUtf8Bytes );

    SW_ASSERT_EQUAL( 1u, capture.getCount() );
    SW_EXPECT_TRUE( sw::StringUtil::startsWith( capture.getEntries()[0]._message, "[TestLog] Bad:" ) );
#endif
}

/**
 * @brief [LogTest] Caller 지정 및 수신 검증
 */
SW_TEST_CASE( LogTest, LogCallerHandling )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
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
#endif
}

/**
 * @brief [LogTest] 8개 스레드 동시 대량 로깅 스트레스 테스트 (데이터 레이스/크래시 검증)
 */
SW_TEST_CASE( LogTest, ConcurrentMultiThreadedLogging )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
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
#endif
}

/**
 * @brief [LogTest] 큐 용량(4096) 초과 시 동기 폴백 안전성 검증
 */
SW_TEST_CASE( LogTest, QueueOverflowFallbackStress )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    ThreadSafeLogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    constexpr uint32 kHeavyLogCount = 6000;
    for ( uint32 logIndex = 0; logIndex < kHeavyLogCount; ++logIndex )
    {
        SW_LOG_INFO( "[TestLog] Heavy burst log %#", logIndex );
    }

    SW_EXPECT_EQUAL( kHeavyLogCount, capture.getCount() );
#endif
}

/**
 * @brief [LogTest] 로거 shutdown 시 큐 잔여 로그 플러시(Drain) 일관성 검증
 */
SW_TEST_CASE( LogTest, ShutdownQueueDrainConsistency )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    ThreadSafeLogCapture capture;
    SW_ASSERT_TRUE( capture.isAttached() );

    constexpr uint32 kDrainCount = 500;
    for ( uint32 logIndex = 0; logIndex < kDrainCount; ++logIndex )
    {
        SW_LOG_INFO( "[TestLog] Drain check %#", logIndex );
    }

    SW_EXPECT_EQUAL( kDrainCount, capture.getCount() );
#endif
}

/**
 * @brief [LogTest] 로깅 진행 중 동시 리스너 등록/해제 동시성 검증
 */
SW_TEST_CASE( LogTest, ConcurrentListenerAttachDetach )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
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
        sw::DelegateHandle handle = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA(
            sw::LogWrittenDelegate,
            []( const sw::LogEntry& ) {} ) );
        SW_EXPECT_TRUE( handle.isValid() );
        sw::Logger::unregisterGlobalListener( handle );
    }

    if ( emitter.joinable() )
        emitter.join();
#endif
}

/**
 * @brief [LogTest] 출력 장치 상한을 넘기면 **거절하고 경고한다** (조용히 삼키지 않는다)
 * @details 디스패치는 잠금 안에서 포인터만 고정 배열로 떠 와 락 밖에서 쓴다 — 느린 파일 I/O 가
 *          콘솔을 막지 않게 하는 분리다. 그 배열은 상한에서 잘리므로 `addOutput` 이 상한을 넘는 장치를
 *          **받아서 `open` 까지 해 놓으면 한 줄도 주지 못한다.** 붙인 자리에서는 보이지 않는 실패라
 *          상한에서 거절한다 — 그래서 "달린 장치는 반드시 받는다" 가 참이 된다.
 */
SW_TEST_CASE( LogTest, OutputsBeyondTheCapAreRejectedNotSilentlyIgnored )
{
    sw::AsyncLogSink logger;
    logger.initialize();

    // 기본으로 콘솔·파일이 달려 있다. 남은 자리를 테스트용 장치로 채우고, 한 개 더 시도한다.
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

    // 거절되지 않고 달린 장치는 **전부** 그 줄을 받아야 한다.
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

/**
 * @brief [LogTest] 자식 역할: 교착 시나리오 하나를 돌린다(환경 변수가 이름을 준다). 그냥 실행하면 건너뛴다.
 * @details 로거의 락을 쥔 채 로그를 부르면 그 로그가 같은 락으로 되돌아와 같은 스레드가 영구 정지한다. 그래서 이 케이스는 부모가 시한을 걸고 띄운다.
 *          시나리오마다 싱크를 **전역 싱크로** 만들어 `SW_LOG_*` 가 그 싱크 자신으로 들어오게 한다(전역 싱크가 따로 있으면 되돌아오는 길이 없다).
 */
SW_TEST_CASE( LogTest, DeadlockChildRoleRunsAScenario )
{
    sw::string  pScenarioText;
    const utf8* pScenario = sw::ProcessEnvironment::tryRead( kDeadlockChildEnvironment, pScenarioText ) ? pScenarioText.c_str() : nullptr;
    if ( pScenario == nullptr )
        SW_TEST_SKIP( "child only — the Deadlock tests launch it" );
    const sw::string_view scenario{ pScenario };

    sw::AsyncLogSink logger;
    GlobalSinkSwap   globalSinkSwap{ &logger };

    if ( scenario == "initialize" )
    {
        // 길을 파일이 막은 폴더를 여는 장치 — 열다가 `FileUtil` 이 오류 로그를 남긴다. 장치를 락 안에서 열면 그 로그가 `initialize` 를 멈춘다.
        const sw::string root    = test::makeTempDirectory( "SwLogDeadlockInitialize" );
        const sw::string blocker = sw::FileUtil::joinPath( root, "blocker" );
        SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( blocker, "a file, not a folder" ) );

        sw::unique_ptr<BlockedFolderLogOutput> output   = sw::make_unique<BlockedFolderLogOutput>( sw::FileUtil::joinPath( blocker, "Logs" ) );
        BlockedFolderLogOutput*                pBlocked = output.get();
        SW_ASSERT_TRUE( logger.addOutput( std::move( output ) ) ); // 아직 초기화 전이라 여기서는 열지 않는다

        logger.initialize();

        SW_EXPECT_FALSE( pBlocked->_bFolderReady );
        uint32 createFailedCount = 0;
        uint32 openFailedCount   = 0;
        for ( const sw::string& line : pBlocked->_listFormatted )
        {
            if ( sw::StringUtil::contains( line, "Could not create directory" ) )
                ++createFailedCount;
            if ( sw::StringUtil::contains( line, "could not be opened" ) )
                ++openFailedCount;
        }
        SW_EXPECT_EQUAL( 1u, createFailedCount ); // 열다가 남긴 오류가 장치까지 왔다
        SW_EXPECT_EQUAL( 1u, openFailedCount );   // 열지 못한 장치가 있다고 싱크가 알렸다
        logger.shutdown();
    }
    else if ( scenario == "addOutput" )
    {
        logger.initialize();
        uint32                   limitWarningCount = 0;
        const sw::DelegateHandle handle            = logger.registerLogWrittenListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [&limitWarningCount]( const sw::LogEntry& entry )
                   {
            if ( sw::StringUtil::contains( entry._message, "At most" ) )
                ++limitWarningCount;
        } ) );

        // 이미 초기화된 뒤라 `addOutput` 이 장치를 바로 연다 — 열다가 남긴 로그도 락 밖이어야 한다.
        sw::unique_ptr<SelfLoggingLogOutput> logging  = sw::make_unique<SelfLoggingLogOutput>( true, false );
        SelfLoggingLogOutput*                pLogging = logging.get();
        SW_ASSERT_TRUE( logger.addOutput( std::move( logging ) ) );
        SW_EXPECT_EQUAL( 1u, pLogging->_openCount );

        // 상한을 넘기면 거절 경고가 **리스너가 있는 채로** 남는다 — 락 안에서 남기면 방송이 같은 락을 잡는다.
        bool bRejected = false;
        for ( uint32 attempt = 0; attempt < 16 && bRejected == false; ++attempt )
        {
            bRejected = logger.addOutput( sw::make_unique<CountingLogOutput>() ) == false;
        }
        SW_EXPECT_TRUE( bRejected );
        SW_EXPECT_EQUAL( 1u, limitWarningCount );

        logger.unregisterLogWrittenListener( handle );
        logger.shutdown();
    }
    else if ( scenario == "writeLogsBack" )
    {
        // 장치가 `write` 안에서 로그를 남기면(자기 락을 쥔 채라고 보면) 그 로그는 장치로 되돌아오지 않고 stderr 로 간다 — 버려지지도, 멈추지도 않는다.
        logger.initialize();
        sw::unique_ptr<SelfLoggingLogOutput> logging  = sw::make_unique<SelfLoggingLogOutput>( false, true );
        SelfLoggingLogOutput*                pLogging = logging.get();
        SW_ASSERT_TRUE( logger.addOutput( std::move( logging ) ) );

        logger.writeLog( sw::LogLevel::Error, "Test", "Reentry", "한 줄", __FILE__, __LINE__ );
        logger.shutdown();

        SW_EXPECT_EQUAL( 1u, pLogging->_writeCount );
    }
    else
    {
        SW_EXPECT_TRUE_MSG( false, "unknown deadlock scenario" );
    }
}

/**
 * @brief [LogTest] 로그 폴더를 만들 수 없어 장치가 열다가 오류를 남겨도 `initialize` 는 멈추지 않고 오류를 알리고 끝난다
 * @details 전에는 `initialize` 가 `_mutex` 를 쥔 채 장치를 열었고, 그 로그가 같은 `_mutex` 를 다시 잡아 기동이 영구 정지했다.
 */
SW_TEST_CASE( LogTest, InitializeDoesNotDeadlockWhenAnOutputLogsWhileOpening )
{
    const test::ProcessRunResult child = runDeadlockScenario( "initialize" );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_FALSE_MSG( child._bTimedOut, "initialize hung — the sink opened an output while holding its lock" );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail().c_str() );
}

/**
 * @brief [LogTest] `addOutput` 이 장치를 열다가 남긴 로그와 상한 거절 경고는 리스너가 있어도 교착 없이 방송된다
 */
SW_TEST_CASE( LogTest, AddOutputDoesNotDeadlockWhenItLogs )
{
    const test::ProcessRunResult child = runDeadlockScenario( "addOutput" );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_FALSE_MSG( child._bTimedOut, "addOutput hung — it logged while holding the sink lock" );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail().c_str() );
}

/**
 * @brief [LogTest] 장치가 `write` 안에서 남긴 로그는 되돌아와 멈추지 않고 stderr 로 나간다(버려지지 않는다)
 */
SW_TEST_CASE( LogTest, LogFromInsideAnOutputWriteGoesToStderr )
{
    const test::ProcessRunResult child = runDeadlockScenario( "writeLogsBack" );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_FALSE_MSG( child._bTimedOut, "the sink hung on a log written from inside an output" );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail().c_str() );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "a log written from inside write" ), child.getOutputTail().c_str() );
}

/**
 * @brief [LogTest] removeListenerCodeWithin 은 호출 스텁이 그 범위 안인 리스너만 뗀다
 * @details 핫 리로드가 모듈 이미지를 내리기 전에 부르는 길이다(`ModuleImageUtil::releaseModuleCode`) — 에디터 콘솔 패널이 다는 리스너가 여기 든다.
 *          범위를 한 리스너의 스텁 하나로 좁혀, 다른 리스너는 남아 계속 받는지 본다. 두 람다의 몸통을 일부러 다르게 둔다(같으면 링커가
 *          하나로 접어 두 스텁의 주소가 같아질 수 있다).
 */
SW_TEST_CASE( LogTest, ReleaseListenerCodeWithinDropsOnlyThatRange )
{
    sw::AsyncLogSink logger;
    logger.initialize();

    int32                        releasedValue{ 0 };
    int32                        keptValue{ 0 };
    const sw::LogWrittenDelegate released = SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [&releasedValue]( const sw::LogEntry& )
    { releasedValue += 1; } );
    const sw::LogWrittenDelegate kept     = SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [&keptValue]( const sw::LogEntry& )
        { keptValue += 10; } );
    logger.registerLogWrittenListener( released );
    logger.registerLogWrittenListener( kept );

    const uint8* pReleasedCode = static_cast<const uint8*>( released.getCodeAddress() );
    SW_EXPECT_EQUAL( 1u, logger.removeListenerCodeWithin( pReleasedCode, pReleasedCode + 1 ) );
    SW_EXPECT_EQUAL( 0u, logger.removeListenerCodeWithin( pReleasedCode, pReleasedCode + 1 ) );

    logger.writeLog( sw::LogLevel::Error, "Test", "Release", "한 줄", __FILE__, __LINE__ );
    logger.shutdown();

    SW_EXPECT_EQUAL( 0, releasedValue );
    SW_EXPECT_EQUAL( 10, keptValue );
}

/**
 * @brief [LogTest] 잘못된 UTF-8 바이트가 섞인 줄은 그 바이트만 백슬래시 + `xNN` 이 되고, 나머지(한글 포함)는 그대로 파일 · 콘솔에 간다
 * @details 한 바이트만 틀려도 줄 전체를 로캘 변환하면, 아무도 `setlocale` 을 부르지 않아 C 로캘이라 Windows 에서는 멀쩡한 한글까지
 *          바이트마다 다른 글자로 깨지고 glibc 에서는 변환이 실패해 줄이 통째로 빈다.
 */
SW_TEST_CASE( LogTest, InvalidUtf8ByteIsEscapedNotTheWholeLine )
{
    sw::AsyncLogSink logger;
    logger.initialize();
    sw::unique_ptr<CapturingLogOutput> output   = sw::make_unique<CapturingLogOutput>();
    CapturingLogOutput*                pCapture = output.get();
    SW_ASSERT_TRUE( logger.addOutput( std::move( output ) ) );

    logger.writeLog( sw::LogLevel::Error, "Test", "Utf8", "\xED\x95\x9C\xEA\xB8\x80 \xFF end", __FILE__, __LINE__ ); // "한글 <FF> end"
    logger.shutdown();

    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pCapture->_listFormatted.size() ) );
    const sw::string& line = pCapture->_listFormatted[0];
    SW_EXPECT_TRUE_MSG( line.find( "\xED\x95\x9C\xEA\xB8\x80 \\xFF end" ) != sw::string::npos, line.c_str() );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( line.c_str() ) );
}

/**
 * @brief [LogTest] 이름이 같은 두 파일(다른 폴더)의 로그 호출자 이름이 섞이지 않는다 — 구분자가 섞여도 같은 파일은 같다
 * @details 호출자 표가 파일 이름만 키로 쓰면 `CallerBase/SameNamedFile.cpp` 와 `CallerKit/SameNamedFile.cpp` 가 같은 키라, 나중에 등록된
 *          이름이 두 파일의 로그에 모두 붙는다.
 */
SW_TEST_CASE( LogTest, CallerNamesOfSameNamedFilesStayApart )
{
    sw::Logger::registerCaller( "C:/proj/Source/CallerBase/SameNamedFile.cpp", "BaseCaller" );
    sw::Logger::registerCaller( "C:\\proj\\Source\\CallerKit\\SameNamedFile.cpp", "KitCaller" );
    SW_EXPECT_STREQ( "BaseCaller", sw::Logger::getCaller( "C:/proj/Source/CallerBase/SameNamedFile.cpp" ) );
    SW_EXPECT_STREQ( "KitCaller", sw::Logger::getCaller( "C:\\proj\\Source\\CallerKit\\SameNamedFile.cpp" ) );
    // 앞부분 · 구분자가 달라도(헤더는 TU 마다 `__FILE__` 의 꼴이 다를 수 있다) 같은 파일이다.
    SW_EXPECT_STREQ( "KitCaller", sw::Logger::getCaller( "../elsewhere/CallerKit/SameNamedFile.cpp" ) );
}

// ------------------------------------------------------------------------------
// 2) 출력 장치 — 실제 파일 · 표준 출력
// ------------------------------------------------------------------------------
/**
 * @brief [LogTest] FileLogOutput 은 레코드를 그 시각 · 세션의 파일에 쓰고, 시각이 바뀌면 새 파일을 열며, Error 는 닫기 전에 비워 읽힌다
 * @details 열기 전의 쓰기는 버린다(파일을 만들지 않는다). 연도를 2099 로 두어 실제 로그 파일과 이름이 겹치지 않게 하고, 끝에 지운다.
 */
SW_TEST_CASE( LogTest, FileLogOutputWritesTheFileOfItsHourAndSession )
{
    sw::FileLogOutput output;
    output.write( makeProbeRecord( 3, sw::LogLevel::Info, "dropped before open\n" ) );
    SW_ASSERT_TRUE( output.open() );

    const sw::string             firstPath  = makeProbeLogPath( output, 3 );
    const sw::string             secondPath = makeProbeLogPath( output, 4 );
    const sw::vector<sw::string> listProbePath{ firstPath, secondPath };
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [listProbePath]()
    {
        for ( const sw::string& probePath : listProbePath )
        {
            (void)sw::FileUtil::removeFile( probePath ); // 없으면 할 일이 없다
        }
    } ) );
    SW_EXPECT_FALSE_MSG( sw::FileUtil::exists( firstPath ), "열기 전에 쓴 줄이 파일을 만들었습니다" );

    output.write( makeProbeRecord( 3, sw::LogLevel::Info, "probe line in hour 3\n" ) );
    output.write( makeProbeRecord( 4, sw::LogLevel::Error, "probe error in hour 4\n" ) );

    // Error 는 줄마다 비운다 — 쓰는 중인 파일을 닫기 전에 다른 쪽이 열어 읽을 수 있어야 한다(크래시 직전의 Error 가 버퍼에 남지 않게,
    // 실행 중인 로그를 편집기 · tail 이 열 수 있게).
    sw::string secondText;
    SW_EXPECT_TRUE_MSG( sw::FileUtil::readTextFile( secondPath, secondText ), "쓰는 중인 로그 파일을 열지 못했습니다(배타적으로 열었습니까?)" );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( secondText, "probe error in hour 4" ), secondText.c_str() );

    output.close();
    sw::string firstText;
    SW_EXPECT_TRUE( sw::FileUtil::readTextFile( firstPath, firstText ) );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( firstText, "probe line in hour 3" ), firstText.c_str() );
    SW_EXPECT_FALSE_MSG( sw::StringUtil::contains( firstText, "probe error in hour 4" ), "다른 시각의 줄이 섞였습니다" );
}

/**
 * @brief [LogTest] 자식 역할: ConsoleLogOutput 으로 표식 한 줄을 쓴다. 그냥 실행하면 건너뛴다.
 */
SW_TEST_CASE( LogTest, ConsoleChildRoleWritesAMarker )
{
    if ( sw::ProcessEnvironment::isSet( "SW_TEST_CONSOLE_LOG_CHILD" ) == false )
        SW_TEST_SKIP( "child only — ConsoleLogOutputReachesStandardOutput launches it" );

    sw::ConsoleLogOutput output;
    SW_ASSERT_TRUE( output.open() );
    output.write( makeProbeRecord( 1, sw::LogLevel::Warning, "SW_CONSOLE_LOG_PROBE warning line\n" ) );
    output.close();
}

/**
 * @brief [LogTest] ConsoleLogOutput 이 쓴 줄은 표준 출력으로 나간다 — 자식 프로세스의 출력을 받아 본다
 */
SW_TEST_CASE( LogTest, ConsoleLogOutputReachesStandardOutput )
{
    const test::ChildEnvironmentVariable arrEnvironment[] = {
        { "SW_TEST_CONSOLE_LOG_CHILD", "1" }
    };
    const test::ProcessRunResult child = test::runThisExecutableAsChild( "LogTest.ConsoleChildRoleWritesAMarker", arrEnvironment, 60 );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_FALSE( child._bTimedOut );
    SW_EXPECT_EQUAL( 0, child._exitCode );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "SW_CONSOLE_LOG_PROBE warning line" ), child.getOutputTail().c_str() );
}

/**
 * @brief [LogTest] 자식 역할: 전역 싱크를 뗀 채 로그 줄을 남기거나, 단언을 실패시킨다. 그냥 실행하면 건너뛴다.
 * @details `nosink` 는 싱크가 없을 때 줄이 stderr 로 가는지, `assert` 는 `SW_LOG_ASSERT` 가 stderr 에 동기로 쓰는지 본다(단언은 가로채서 멈추지 않는다).
 */
SW_TEST_CASE( LogTest, StderrChildRoleWritesMarkers )
{
    sw::string  pRoleText;
    const utf8* pRole = sw::ProcessEnvironment::tryRead( "SW_TEST_LOG_STDERR_CHILD", pRoleText ) ? pRoleText.c_str() : nullptr;
    if ( pRole == nullptr )
        SW_TEST_SKIP( "child only — the stderr cases launch it" );

    if ( sw::StringUtil::equals( pRole, "nosink" ) )
    {
        sw::Logger::setGlobalSink( nullptr );
        SW_LOG_ERROR( "SW_NOSINK_PROBE error line" );
    }
    else
    {
        [[maybe_unused]] test::ScopedAssertCapture capture;
        const bool                                 bLogAssertProbeHolds = false;
        SW_LOG_ASSERT( bLogAssertProbeHolds, "SW_ASSERT_PROBE message" );
    }
}

/**
 * @brief [LogTest] 싱크가 없을 때 `SW_LOG_*` 줄은 버려지지 않고 stderr 로 나간다
 */
SW_TEST_CASE( LogTest, LineWithoutASinkReachesStandardError )
{
    const test::ChildEnvironmentVariable arrEnvironment[] = {
        { "SW_TEST_LOG_STDERR_CHILD", "nosink" }
    };
    const test::ProcessRunResult child = test::runThisExecutableAsChild( "LogTest.StderrChildRoleWritesMarkers", arrEnvironment, 60 );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_FALSE( child._bTimedOut );
    SW_EXPECT_EQUAL( 0, child._exitCode );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "SW_NOSINK_PROBE error line" ), child.getOutputTail().c_str() );
}

/**
 * @brief [LogTest] 실패한 `SW_LOG_ASSERT` 는 식 · 메시지 · 자리를 stderr 에 먼저 남긴다(Debug)
 */
SW_TEST_CASE( LogTest, FailedLogAssertWritesStandardErrorBeforeStopping )
{
#if !defined( SW_DEBUG )
    SW_TEST_SKIP( "SW_LOG_ASSERT stops only in Debug builds" );
#else
    const test::ChildEnvironmentVariable arrEnvironment[] = {
        { "SW_TEST_LOG_STDERR_CHILD", "assert" }
    };
    const test::ProcessRunResult child = test::runThisExecutableAsChild( "LogTest.StderrChildRoleWritesMarkers", arrEnvironment, 60 );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_FALSE( child._bTimedOut );
    SW_EXPECT_EQUAL( 0, child._exitCode );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "[SW_LOG_ASSERT] bLogAssertProbeHolds" ), child.getOutputTail().c_str() );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "SW_ASSERT_PROBE message" ), child.getOutputTail().c_str() );
#endif
}

/**
 * @brief [LogTest] 싱크가 없을 때 리스너를 붙이면 순서 위반으로 보고하고 빈 핸들을 돌려준다(조용히 무시하지 않는다)
 */
SW_TEST_CASE( LogTest, RegisteringAListenerWithoutASinkIsReported )
{
    test::ScopedAssertCapture capture;
    sw::DelegateHandle        handle;
    sw::ILogSink*             pOldSink = sw::Logger::getGlobalSink();
    {
        sw::Logger::setGlobalSink( nullptr ); // 억제용 싱크(ScopedLogSuppressor)는 리스너를 받으므로 싱크 자체를 비운다
        handle = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, []( const sw::LogEntry& ) {} ) );
        sw::Logger::setGlobalSink( pOldSink );
    }
    SW_EXPECT_FALSE( handle.isValid() );
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 1u, capture.getCount() );
}
