#include "pch.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/LogTypes.h"
#include "Core/Log/Logger.h"
#include "Core/Log/Sink/AsyncLogSink.h"
#include "Core/Log/Sink/ConsoleLogOutput.h"
#include "Core/Log/Sink/FileLogOutput.h"
#include "Core/Log/Sink/ILogOutput.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/ProcessSession.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

#include <cstring>

// 로그 — 전역 싱크 · 폴더, 레벨마다 리스너까지 닿기, 런타임 상세도, 컴파일 상한, id 로 찾기, 서식 치환, 잘못된 UTF-8, 호출자,
// 여러 스레드 · 큰 묶음 · 리스너 붙였다 떼기, 출력 장치 상한, 잘못된 바이트만 바꾸기, 같은 이름 파일의 호출자, 시간별 파일 장치,
// 자식 역할(`SW_TEST_CHILD_ROLE`)로 보는 교착 · 콘솔 · stderr · 싱크 없는 리스너.
// 옛 `ReleaseListenerCodeWithinDropsOnlyThatRange` 는 옮기지 않았다 — `removeListenerCodeWithin` 이 지금 싱크에 없다.
// 케이스가 일부러 내는 Warning · Error 는 테스트 id(`Test.Log*`)를 붙이고 `SW_EXPECT_LOG` 로 적는다(실행기가 그 밖의 Warning · Error 를 실패로 센다).

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
            _subscription = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA(
                sw::LogWrittenDelegate,
                [this]( const sw::LogEntry& entry )
            {
                if ( sw::StringUtil::startsWith( entry._message, kCapturePrefix ) )
                    _listEntry.push_back( entry );
            } ) );
        }

        LogCapture( const LogCapture& )            = delete;
        LogCapture& operator=( const LogCapture& ) = delete;

        /** @brief 리스너가 실제로 등록되었는지 반환합니다. */
        bool isAttached() const
        {
            return _subscription.isActive();
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

        /** @brief 수집된 줄 가운데 id 가 @p pID 인 줄 수입니다(id 없는 줄은 세지 않는다). */
        uint32 countWithID( const utf8* pID ) const
        {
            uint32 count = 0;
            for ( const sw::LogEntry& entry : _listEntry )
            {
                if ( entry._pID != nullptr && std::strcmp( entry._pID, pID ) == 0 )
                    ++count;
            }
            return count;
        }

    private:
        sw::vector<sw::LogEntry> _listEntry;
        sw::Subscription         _subscription; ///< 마지막 멤버라 먼저 풀린다(목록보다 먼저)
    };

    /**
     * @class ThreadSafeLogCapture
     * @brief 멀티스레드 동시 로깅 테스트용 스레드 안전 로그 캡처 싱크
     */
    class ThreadSafeLogCapture
    {
    public:
        ThreadSafeLogCapture()
        {
            _subscription = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA(
                sw::LogWrittenDelegate,
                [this]( const sw::LogEntry& entry )
            {
                if ( sw::StringUtil::startsWith( entry._message, kCapturePrefix ) )
                {
                    sw::ScopedLock<sw::mutex> lock{ _mutex };
                    _listEntry.push_back( entry );
                }
            } ) );
        }

        ThreadSafeLogCapture( const ThreadSafeLogCapture& )            = delete;
        ThreadSafeLogCapture& operator=( const ThreadSafeLogCapture& ) = delete;

        bool isAttached() const
        {
            return _subscription.isActive();
        }

        [[maybe_unused]] sw::vector<sw::LogEntry> getEntries() const
        {
            sw::ScopedLock<sw::mutex> lock{ _mutex };
            return _listEntry;
        }

        uint32 getCount() const
        {
            sw::ScopedLock<sw::mutex> lock{ _mutex };
            return static_cast<uint32>( _listEntry.size() );
        }

    private:
        mutable sw::mutex        _mutex;
        sw::vector<sw::LogEntry> _listEntry;
        sw::Subscription         _subscription; ///< 마지막 멤버라 먼저 풀린다(목록 · 잠금보다 먼저)
    };

    /** @brief 아무 데도 쓰지 않고 받은 줄 수만 세는 테스트용 출력 장치. */
    class CountingLogOutput final : public sw::ILogOutput
    {
    public:
        uint32             _writeCount{ 0 };
        [[nodiscard]] bool open() override { return true; }
        void               close() override {}
        void               write( const sw::LogRecord& ) override { ++_writeCount; }
    };

    /** @brief 받은 줄의 완성된 글(`LogRecord::_formatted` — 파일 · 콘솔이 쓰는 그것)을 모으는 테스트용 출력 장치. */
    class CapturingLogOutput final : public sw::ILogOutput
    {
    public:
        sw::vector<sw::string> _listFormatted;
        [[nodiscard]] bool     open() override { return true; }
        void                   close() override {}
        void                   write( const sw::LogRecord& record ) override { _listFormatted.push_back( record._formatted ); }
    };

} // namespace

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
    LogCapture capture;
    SW_REQUIRE_TRUE( capture.isAttached() );

    SW_EXPECT_LOG( "Test.LogLevelProbe" ); // Warning · Error 줄은 이 테스트가 전달을 보려는 대상이다

    // 기본 런타임 상세도는 Info 라 Trace 가 버려진다 — 이 테스트는 **네 레벨 모두**를 보는 것이
    // 목적이므로 잠깐 올렸다가 되돌린다(컴파일 상한과 런타임 상세도는 다른 축이다).
    const sw::LogLevel previousVerbosity = sw::Logger::getRuntimeVerbosity();
    sw::Logger::setRuntimeVerbosity( sw::LogLevel::Trace );
    const struct VerbosityRestore
    {
        sw::LogLevel _previous;
        ~VerbosityRestore() { sw::Logger::setRuntimeVerbosity( _previous ); }
    } verbosityRestore{ previousVerbosity };

    SW_LOG_INFO_ID( "TestLog.InfoLevelLog", "[TestLog] Info level log" );
    SW_LOG_WARNING_ID( "Test.LogLevelProbe", "[TestLog] Warning level log" );
    SW_LOG_ERROR_ID( "Test.LogLevelProbe", "[TestLog] Error level log" );
    SW_LOG_TRACE_ID( "TestLog.TraceLevelLog", "[TestLog] Trace level log" ); // Trace 는 Debug 만 컴파일한다 — 아래 기대 수가 그것을 따른다

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
    constexpr uint32 expectedCount = SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_TRACE ) ? 4u : 3u;

    SW_REQUIRE_EQUAL( expectedCount, capture.getCount() );

    for ( uint32 logIndex = 0; logIndex < expectedCount; ++logIndex )
    {
        const sw::LogEntry& entry = capture.getEntries()[logIndex];
        SW_EXPECT_EQUAL( static_cast<uint32>( expectedLevels[logIndex] ), static_cast<uint32>( entry._level ) );
        SW_EXPECT_EQUAL( sw::string( expectedMessages[logIndex] ), entry._message );
        SW_EXPECT_EQUAL( sw::string( SW_LOG_TAG ), entry._tag );
        SW_EXPECT_FALSE( entry._file.empty() );
        SW_EXPECT_TRUE( entry._line > 0 );
    }
}

/**
 * @brief [LogTest] 런타임 상세도가 그보다 덜 심각한 줄을 버리는지
 * @details 배포본에서 고객에게 상세도를 올려 재현을 받는 것이 이 값의 용도다. 컴파일 상한과는 다른
 *          축이다 — 상한은 "무엇을 남길 수 있나", 이 값은 "지금 무엇을 남길까" 를 정한다.
 */
SW_TEST_CASE( LogTest, RuntimeVerbosityDropsLessSevere )
{
    LogCapture capture;
    SW_REQUIRE_TRUE( capture.isAttached() );
    SW_EXPECT_LOG( "Test.LogVerbosityProbe" ); // 남는 Error 줄은 이 테스트가 보려는 대상이다

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

    SW_LOG_ERROR_ID( "Test.LogVerbosityProbe", "[TestLog] kept" );
    SW_LOG_WARNING_ID( "TestLog.Dropped", "[TestLog] dropped" );
    SW_LOG_INFO_ID( "TestLog.Dropped", "[TestLog] dropped" );
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
 * @brief [LogTest] id 가 붙은 줄(`SW_LOG_*_ID`)은 문구와 상관없이 리스너의 `LogEntry::_pID` 로 찾아지고, id 없는 줄은 어떤 id 로도 잡히지 않는다
 */
SW_TEST_CASE( LogTest, LinesAreFoundByIDNotWording )
{
    LogCapture capture;
    SW_REQUIRE_TRUE( capture.isAttached() );
    SW_EXPECT_LOG( "Test.LogIDProbe" );
    SW_EXPECT_LOG( "Test.LogIDOther" );
    SW_LOG_WARNING_ID( "Test.LogIDProbe", "[TestLog] first wording %#", 1 );
    SW_LOG_ERROR_ID( "Test.LogIDProbe", "[TestLog] completely different wording" );
    SW_LOG_WARNING_ID( "Test.LogIDOther", "[TestLog] third" );
    SW_LOG_INFO_ID( "TestLog.TestLogIDProbeOnlyWord", "[TestLog] Test.LogIDProbe is only a word in a line without an id" ); // id 없는 줄 — 어떤 id 로도 잡히지 않는다
    SW_EXPECT_EQUAL( 2u, capture.countWithID( "Test.LogIDProbe" ) );
    SW_EXPECT_EQUAL( 1u, capture.countWithID( "Test.LogIDOther" ) );
    SW_EXPECT_EQUAL( 0u, capture.countWithID( "Test.LogIDMissing" ) );
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
    LogCapture capture;
    SW_REQUIRE_TRUE( capture.isAttached() );

    SW_EXPECT_LOG( "Test.LogFormatProbe" ); // Warning · Error 줄은 이 테스트가 치환을 보려는 대상이다

    // 기본 런타임 상세도는 Info 라 Trace 가 버려진다 — 이 테스트는 **네 레벨 모두**를 보는 것이
    // 목적이므로 잠깐 올렸다가 되돌린다(컴파일 상한과 런타임 상세도는 다른 축이다).
    const sw::LogLevel previousVerbosity = sw::Logger::getRuntimeVerbosity();
    sw::Logger::setRuntimeVerbosity( sw::LogLevel::Trace );
    const struct VerbosityRestore
    {
        sw::LogLevel _previous;
        ~VerbosityRestore() { sw::Logger::setRuntimeVerbosity( _previous ); }
    } verbosityRestore{ previousVerbosity };

    SW_LOG_INFO_ID( "TestLog.SWLOGINFO", "[TestLog] SW_LOG_INFO %#", 123 );
    SW_LOG_WARNING_ID( "Test.LogFormatProbe", "[TestLog] SW_LOG_WARNING %#", "warning" );
    SW_LOG_ERROR_ID( "Test.LogFormatProbe", "[TestLog] SW_LOG_ERROR %#", 404 );
    SW_LOG_TRACE_ID( "TestLog.SWLOGTRACE", "[TestLog] SW_LOG_TRACE %#", 3.14f ); // Trace 는 Debug 만 컴파일한다

    SW_REQUIRE_EQUAL( SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_TRACE ) ? 4u : 3u, capture.getCount() );
    SW_EXPECT_EQUAL( sw::string( "[TestLog] SW_LOG_INFO 123" ), capture.getEntries()[0]._message );
    SW_EXPECT_EQUAL( sw::string( "[TestLog] SW_LOG_WARNING warning" ), capture.getEntries()[1]._message );
    SW_EXPECT_EQUAL( sw::string( "[TestLog] SW_LOG_ERROR 404" ), capture.getEntries()[2]._message );

    // 실수 자릿수는 기본 정밀도에 달려 있으므로 앞부분만 확인합니다.
    if ( capture.getCount() == 4u )
        SW_EXPECT_TRUE( sw::StringUtil::startsWith( capture.getEntries()[3]._message, "[TestLog] SW_LOG_TRACE 3.14" ) );
}

/**
 * @brief [LogTest] 비-UTF8(ANSI/CP949) 문자열 전달 시에도 안전하게 처리됨
 */
SW_TEST_CASE( LogTest, NonUTF8FallbackSafety )
{
    LogCapture capture;
    SW_REQUIRE_TRUE( capture.isAttached() );

    // 유효하지 않은 UTF-8 바이트 시퀀스 (0xFF, 0xFE 등)
    const utf8 invalidUTF8Bytes[] = { '[', 'T', 'e', 's', 't', 'L', 'o', 'g', ']', ' ', 'B', 'a', 'd', ':', static_cast<utf8>( 0xFF ), static_cast<utf8>( 0xFE ), '\0' };
    SW_LOG_INFO_ID( "TestLog.InvalidUtf8Probe", "%#", invalidUTF8Bytes );

    SW_REQUIRE_EQUAL( 1u, capture.getCount() );
    SW_EXPECT_TRUE( sw::StringUtil::startsWith( capture.getEntries()[0]._message, "[TestLog] Bad:" ) );
}

/**
 * @brief [LogTest] 줄의 범주는 id 의 영역이다(`LogEntry::getCategory`) — 파일마다 등록하는 호출자 이름은 없다
 */
SW_TEST_CASE( LogTest, LogCategoryComesFromID )
{
    LogCapture capture;
    SW_REQUIRE_TRUE( capture.isAttached() );

    SW_EXPECT_LOG( "Test.LogCallerProbe" ); // Warning 줄은 이 테스트가 범주를 보려는 대상이다
    SW_LOG_INFO_ID( "TestLog.CustomCallerMessage", "[TestLog] Custom caller message" );
    SW_LOG_WARNING_ID( "Test.LogCallerProbe", "[TestLog] Warning from caller" );

    SW_REQUIRE_EQUAL( 2u, capture.getCount() );
    SW_EXPECT_TRUE( capture.getEntries()[0].getCategory() == "TestLog" );
    SW_EXPECT_EQUAL( sw::string( "[TestLog] Custom caller message" ), capture.getEntries()[0]._message );
    SW_EXPECT_TRUE( capture.getEntries()[1].getCategory() == "Test" );
    SW_EXPECT_EQUAL( sw::string( "[TestLog] Warning from caller" ), capture.getEntries()[1]._message );
}

/**
 * @brief [LogTest] 8개 스레드 동시 대량 로깅 스트레스 테스트 (데이터 레이스/크래시 검증)
 */
SW_TEST_CASE( LogTest, ConcurrentMultiThreadedLogging )
{
    ThreadSafeLogCapture capture;
    SW_REQUIRE_TRUE( capture.isAttached() );

    constexpr uint32 kThreadCount       = 8;
    constexpr uint32 kLogsPerThread     = 100;
    constexpr uint32 kExpectedTotalLogs = kThreadCount * kLogsPerThread;

    sw::thread arrWorker[kThreadCount];
    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        arrWorker[threadIndex] = sw::thread( [threadIndex]
        {
            for ( uint32 logIndex = 0; logIndex < kLogsPerThread; ++logIndex )
            {
                SW_LOG_INFO_ID( "TestLog.ThreadLog", "[TestLog] Thread %# Log %#", threadIndex, logIndex );
            }
        } );
    }

    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        if ( arrWorker[threadIndex].joinable() )
            arrWorker[threadIndex].join();
    }

    SW_EXPECT_EQUAL( kExpectedTotalLogs, capture.getCount() );
}

/**
 * @brief [LogTest] 큐 용량(4096) 초과 시 동기 폴백 안전성 검증
 */
SW_TEST_CASE( LogTest, QueueOverflowFallbackStress )
{
    ThreadSafeLogCapture capture;
    SW_REQUIRE_TRUE( capture.isAttached() );

    constexpr uint32 kHeavyLogCount = 6000;
    for ( uint32 logIndex = 0; logIndex < kHeavyLogCount; ++logIndex )
    {
        SW_LOG_INFO_ID( "TestLog.HeavyBurstLog", "[TestLog] Heavy burst log %#", logIndex );
    }

    SW_EXPECT_EQUAL( kHeavyLogCount, capture.getCount() );
}

/**
 * @brief [LogTest] 로거 shutdown 시 큐 잔여 로그 플러시(Drain) 일관성 검증
 */
SW_TEST_CASE( LogTest, ShutdownQueueDrainConsistency )
{
    ThreadSafeLogCapture capture;
    SW_REQUIRE_TRUE( capture.isAttached() );

    constexpr uint32 kPollCount = 500;
    for ( uint32 logIndex = 0; logIndex < kPollCount; ++logIndex )
    {
        SW_LOG_INFO_ID( "TestLog.DrainCheck", "[TestLog] Drain check %#", logIndex );
    }

    SW_EXPECT_EQUAL( kPollCount, capture.getCount() );
}

/**
 * @brief [LogTest] 로깅 진행 중 동시 리스너 등록/해제 동시성 검증
 */
SW_TEST_CASE( LogTest, ConcurrentListenerAttachDetach )
{
    sw::thread emitter( []
    {
        for ( uint32 index = 0; index < 500; ++index )
        {
            SW_LOG_TRACE_ID( "TestLog.BackgroundEmitterMessage", "[TestLog] Background emitter message %#", index );
        }
    } );

    // 리스너 등록/해제 100회 반복
    for ( uint32 iteration = 0; iteration < 100; ++iteration )
    {
        sw::Subscription subscription = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA(
            sw::LogWrittenDelegate,
            []( const sw::LogEntry& ) {} ) );
        SW_EXPECT_TRUE( subscription.isActive() );
        subscription.reset();
    }

    if ( emitter.joinable() )
        emitter.join();
    SW_EXPECT_FALSE( emitter.joinable() );
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
    sw::AsyncLogSink logger( sw::test::makeScratchPath( "sink.log" ) ); // 기본 파일 장치도 케이스 폴더에 쓴다
    logger.initialize();

    // 기본으로 콘솔·파일이 달려 있다. 남은 자리를 테스트용 장치로 채우고, 한 개 더 시도한다.
    sw::vector<CountingLogOutput*> listAttached;
    {
        SW_EXPECT_LOG( "Log.OutputRejected" ); // 상한을 넘은 장치마다 나는 거절 Warning 이 이 테스트가 보려는 것이다
        for ( uint32 attempt = 0; attempt < 16; ++attempt )
        {
            sw::unique_ptr<CountingLogOutput> output = sw::make_unique<CountingLogOutput>();
            CountingLogOutput*                pRaw   = output.get();
            // 거절되면 `output` 은 그 자리에서 파괴된다 — 그래서 **받아들여진 것만** 들고 있는다.
            // 반환값이 없던 시절에는 이것을 알 방법이 없어, 죽은 포인터를 들고 있게 된다.
            if ( logger.addOutput( std::move( output ) ) )
                listAttached.push_back( pRaw );
        }
    }

    logger.writeLog( sw::LogLevel::Info, "Test", "한 줄", __FILE__, __LINE__ ); // 지역 싱크의 콘솔 장치로도 나가므로 오류 줄로 남기지 않는다(레벨은 이 확인과 무관하다)
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

    SW_EXPECT_TRUE_MSG( attachedCount > 0, "no attached output received the line" );
    SW_EXPECT_TRUE_MSG( silentCount == 0, "an output was accepted but never received a line - one beyond the cap should have been rejected" );
}

/**
 * @brief [LogTest] 잘못된 UTF-8 바이트가 섞인 줄은 그 바이트만 백슬래시 + `xNN` 이 되고, 나머지(한글 포함)는 그대로 파일 · 콘솔에 간다
 * @details 한 바이트만 틀려도 줄 전체를 로캘 변환하면, 아무도 `setlocale` 을 부르지 않아 C 로캘이라 Windows 에서는 멀쩡한 한글까지
 *          바이트마다 다른 글자로 깨지고 glibc 에서는 변환이 실패해 줄이 통째로 빈다.
 */
SW_TEST_CASE( LogTest, InvalidUTF8ByteIsEscapedNotTheWholeLine )
{
    sw::AsyncLogSink logger( sw::test::makeScratchPath( "sink.log" ) ); // 기본 파일 장치도 케이스 폴더에 쓴다
    logger.initialize();
    sw::unique_ptr<CapturingLogOutput> output   = sw::make_unique<CapturingLogOutput>();
    CapturingLogOutput*                pCapture = output.get();
    SW_REQUIRE_TRUE( logger.addOutput( std::move( output ) ) );

    logger.writeLog( sw::LogLevel::Info, "Test", "\xED\x95\x9C\xEA\xB8\x80 \xFF end", __FILE__, __LINE__ ); // "한글 <FF> end" - 지역 싱크의 콘솔 장치로도 나가므로 오류 줄로 남기지 않는다
    logger.shutdown();

    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( pCapture->_listFormatted.size() ) );
    const sw::string& line = pCapture->_listFormatted[0];
    SW_EXPECT_TRUE_MSG( line.find( "\xED\x95\x9C\xEA\xB8\x80 \\xFF end" ) != sw::string::npos, line.c_str() );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( line.c_str() ) );
}

// ------------------------------------------------------------------------------
// 출력 장치 — 실제 파일
// ------------------------------------------------------------------------------
namespace
{
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

    /**
     * @brief `FileLogOutput` 의 시간별 파일을 보는 픽스처 — 케이스가 만든 2099 년 파일을 지운다.
     * @details shortcut: 시간별 파일은 `Saved/Logs` 에만 열린다(장치가 폴더를 받지 않는다) — 케이스 폴더 밖에 잠깐 쓰고 지운다. 다른 장치도 폴더를 받게 되면 `makeScratchPath` 로 옮긴다.
     */
    struct FileLogOutputTest
    {

        sw::vector<sw::string> _listProbePath;
        FileLogOutputTest()
            : _listProbePath{}
        {
        }
        ~FileLogOutputTest()
        {
            for ( const sw::string& path : _listProbePath )
                (void)sw::FileUtil::tryRemoveFile( path ); // 열기 전에 끝난 케이스는 만들지 않았다
        }
        FileLogOutputTest( const FileLogOutputTest& )            = delete;
        FileLogOutputTest& operator=( const FileLogOutputTest& ) = delete;

        /** @brief 그 시각에 쓰는 파일의 경로(`LOG_<연>-<월>-<일>-<시>_<세션>.txt`)이고, 소멸자가 지울 목록에 넣는다. */
        sw::string makeProbeLogPath( const sw::FileLogOutput& output, int32 hour )
        {
            sw::string path = sw::FileUtil::joinPath( output.getLogFolderPath(), "LOG_2099-1-2-" + sw::to_string( hour ) + "_" + sw::ProcessSession::getID() + ".txt" );
            _listProbePath.push_back( path );
            return path;
        }
    };
} // namespace

/**
 * @brief [LogTest] FileLogOutput 은 레코드를 그 시각 · 세션의 파일에 쓰고, 시각이 바뀌면 새 파일을 열며, Error 는 닫기 전에 비워 읽힌다
 * @details 열기 전의 쓰기는 버린다(파일을 만들지 않는다).
 */
SW_TEST_CASE_F( FileLogOutputTest, WritesTheFileOfItsHourAndSession )
{
    sw::FileLogOutput output;
    output.write( makeProbeRecord( 3, sw::LogLevel::Info, "dropped before open\n" ) );
    SW_REQUIRE_TRUE( output.open() );

    const sw::string firstPath  = makeProbeLogPath( output, 3 );
    const sw::string secondPath = makeProbeLogPath( output, 4 );
    SW_EXPECT_FALSE_MSG( sw::FileUtil::exists( firstPath ), "a line written before open created the file" );

    output.write( makeProbeRecord( 3, sw::LogLevel::Info, "probe line in hour 3\n" ) );
    output.write( makeProbeRecord( 4, sw::LogLevel::Error, "probe error in hour 4\n" ) );

    // Error 는 줄마다 비운다 — 쓰는 중인 파일을 닫기 전에 다른 쪽이 열어 읽을 수 있어야 한다(크래시 직전의 Error 가 버퍼에 남지 않게,
    // 실행 중인 로그를 편집기 · tail 이 열 수 있게).
    sw::string secondText;
    SW_EXPECT_TRUE_MSG( sw::FileUtil::readTextFile( secondPath, secondText ), "could not open the log file being written (opened exclusively?)" );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( secondText, "probe error in hour 4" ), secondText );

    output.close();
    sw::string firstText;
    SW_EXPECT_TRUE( sw::FileUtil::readTextFile( firstPath, firstText ) );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( firstText, "probe line in hour 3" ), firstText );
    SW_EXPECT_FALSE_MSG( sw::StringUtil::contains( firstText, "probe error in hour 4" ), "a line of another hour leaked into the file" );
}

// ------------------------------------------------------------------------------
// 자식 역할 — 교착(같은 프로세스에서 돌면 실행 전체가 멈춘다) · 표준 출력 · 표준 에러 · 전역 싱크 떼기(프로세스 전역)를 자식 프로세스에서 본다.
// 자식 App 기동이 Debug 에서 몇 초라 부모 케이스의 시한을 넉넉히 둔다.
// ------------------------------------------------------------------------------
namespace
{
    /** @brief 자식 하나의 시한(초) — 기동 몇 초에 교착 판정 몫을 더한다. */
    constexpr float32 kLogChildTimeoutSeconds = 120.0f;

    /**
     * @class BlockedFolderLogOutput
     * @brief 열 때 폴더를 만드는 장치입니다. 길을 파일이 막고 있으면 `FileUtil` 이 **열다가** 오류 로그를 남깁니다(`FileLogOutput` 이 로그 폴더를 못 만들 때와 같은 모양).
     */
    class BlockedFolderLogOutput final : public sw::ILogOutput
    {
    public:
        sw::string             _blockedFolder;
        sw::vector<sw::string> _listFormatted;
        uint8                  _bFolderReady : 1;
        explicit BlockedFolderLogOutput( sw::string blockedFolder )
            : _blockedFolder{ std::move( blockedFolder ) }
            , _bFolderReady{ SW_TRUE }
        {
        }

        [[nodiscard]] bool open() override
        {
            _bFolderReady = sw::FileUtil::ensureDirectoryExists( _blockedFolder );
            return _bFolderReady;
        }
        void close() override {}
        void write( const sw::LogRecord& record ) override { _listFormatted.push_back( record._formatted ); }
    };

    /** @class SelfLoggingLogOutput @brief `open` 이나 첫 `write` 안에서 로그를 남기는 장치입니다 — 장치가 부르는 쪽으로 로그를 되돌리는 모양을 만듭니다. */
    class SelfLoggingLogOutput final : public sw::ILogOutput
    {
    public:
        uint32 _openCount{ 0 };
        uint32 _writeCount{ 0 };
        uint8  _bLogInOpen  : 1;
        uint8  _bLogInWrite : 1;
        SelfLoggingLogOutput( bool bLogInOpen, bool bLogInWrite )
            : _bLogInOpen{ bLogInOpen }
            , _bLogInWrite{ bLogInWrite }
        {
        }

        [[nodiscard]] bool open() override
        {
            ++_openCount;
            if ( _bLogInOpen )
                SW_LOG_WARNING_ID( "Test.LogProbe", "[Probe] a log written while the device is opening" );
            return true;
        }
        void close() override {}
        void write( const sw::LogRecord& ) override
        {
            ++_writeCount;
            if ( _bLogInWrite && _writeCount == 1 )
                SW_LOG_WARNING_ID( "Test.LogProbe", "[Probe] a log written from inside write" );
        }
    };

    /** @brief 스코프 동안 전역 싱크를 @p pSink 로 바꿉니다 — `SW_LOG_*` 가 그 싱크로 들어가 싱크가 자기 로그를 받는 길을 만든다. 싱크보다 **나중에** 만든다(소멸이 거꾸로). */
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

    /** @brief 교착 시나리오 하나를 자식으로 돌린다 — 교착이면 시한에 자식이 죽고 `runChildRole` 이 실패를 적는다. */
    sw::test::ChildRunResult runDeadlockScenario( const utf8* pScenario )
    {
        return sw::test::runChildRole( "LogTest.DeadlockChildRunsAScenario", pScenario, kLogChildTimeoutSeconds );
    }
} // namespace

/**
 * @brief [LogTest] 자식 역할: 교착 시나리오 하나(인자가 이름)를 돌린다.
 * @details 로거의 락을 쥔 채 로그를 부르면 그 로그가 같은 락으로 되돌아와 같은 스레드가 영구 정지한다 — 그래서 부모가 시한을 걸고 띄운다.
 *          시나리오마다 싱크를 **전역 싱크로** 만들어 `SW_LOG_*` 가 그 싱크 자신으로 들어오게 한다(전역 싱크가 따로 있으면 되돌아오는 길이 없다).
 *          그동안 실행기의 로그 감시는 바꾼 싱크를 보지 않는다 — 시나리오가 일부러 내는 경고는 지역 싱크에서 센다.
 */
SW_TEST_CHILD_ROLE( LogTest, DeadlockChildRunsAScenario )
{
    const sw::string scenario = sw::test::getChildRoleArgument();
    sw::AsyncLogSink logger( sw::test::makeScratchPath( "sink.log" ) );
    GlobalSinkSwap   globalSinkSwap{ &logger };

    if ( scenario == "initialize" )
    {
        // 길을 파일이 막은 폴더를 여는 장치 — 열다가 `FileUtil` 이 오류 로그를 남긴다. 장치를 락 안에서 열면 그 로그가 `initialize` 를 멈춘다.
        const sw::string blocker = sw::test::makeScratchPath( "blocker" );
        SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( blocker, "a file, not a folder" ) );

        sw::unique_ptr<BlockedFolderLogOutput> output   = sw::make_unique<BlockedFolderLogOutput>( sw::FileUtil::joinPath( blocker, "Logs" ) );
        BlockedFolderLogOutput*                pBlocked = output.get();
        SW_REQUIRE_TRUE( logger.addOutput( std::move( output ) ) ); // 아직 초기화 전이라 여기서는 열지 않는다

        logger.initialize();

        SW_EXPECT_FALSE( pBlocked->_bFolderReady );
        uint32 createFailedCount = 0;
        uint32 openFailedCount   = 0;
        for ( const sw::string& line : pBlocked->_listFormatted )
        {
            createFailedCount += sw::StringUtil::contains( line, "File.CreateDirectoryFailed" ) ? 1u : 0u;
            openFailedCount += sw::StringUtil::contains( line, "could not be opened" ) ? 1u : 0u;
        }
        SW_EXPECT_EQUAL( 1u, createFailedCount ); // 열다가 남긴 오류가 장치까지 왔다
        SW_EXPECT_EQUAL( 1u, openFailedCount );   // 열지 못한 장치가 있다고 싱크가 알렸다
        logger.shutdown();
    }
    else if ( scenario == "addOutput" )
    {
        logger.initialize();
        uint32           limitWarningCount = 0;
        sw::Subscription subscription      = logger.registerLogWrittenListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [&limitWarningCount]( const sw::LogEntry& entry )
             {
            if ( entry._pID != nullptr && std::strcmp( entry._pID, "Log.OutputRejected" ) == 0 )
                ++limitWarningCount;
        } ) );

        // 이미 초기화된 뒤라 `addOutput` 이 장치를 바로 연다 — 열다가 남긴 로그도 락 밖이어야 한다.
        sw::unique_ptr<SelfLoggingLogOutput> logging  = sw::make_unique<SelfLoggingLogOutput>( true, false );
        SelfLoggingLogOutput*                pLogging = logging.get();
        SW_REQUIRE_TRUE( logger.addOutput( std::move( logging ) ) );
        SW_EXPECT_EQUAL( 1u, pLogging->_openCount );

        // 상한을 넘기면 거절 경고가 **리스너가 있는 채로** 남는다 — 락 안에서 남기면 방송이 같은 락을 잡는다.
        bool bRejected = false;
        for ( uint32 attempt = 0; attempt < 16 && bRejected == false; ++attempt )
            bRejected = logger.addOutput( sw::make_unique<CountingLogOutput>() ) == false;
        SW_EXPECT_TRUE( bRejected );
        SW_EXPECT_EQUAL( 1u, limitWarningCount );

        subscription.reset();
        logger.shutdown();
    }
    else if ( scenario == "writeLogsBack" )
    {
        // 장치가 `write` 안에서 로그를 남기면(자기 락을 쥔 채라고 보면) 그 로그는 장치로 되돌아오지 않고 stderr 로 간다 — 버려지지도, 멈추지도 않는다.
        logger.initialize();
        sw::unique_ptr<SelfLoggingLogOutput> logging  = sw::make_unique<SelfLoggingLogOutput>( false, true );
        SelfLoggingLogOutput*                pLogging = logging.get();
        SW_REQUIRE_TRUE( logger.addOutput( std::move( logging ) ) );

        logger.writeLog( sw::LogLevel::Info, "Test", "한 줄", __FILE__, __LINE__ );
        logger.shutdown();

        SW_EXPECT_EQUAL( 1u, pLogging->_writeCount );
    }
    else
    {
        SW_EXPECT_TRUE_MSG( false, "unknown deadlock scenario " + scenario );
    }
}

/**
 * @brief [LogTest] 로그 폴더를 만들 수 없어 장치가 열다가 오류를 남겨도 `initialize` 는 멈추지 않고 오류를 알리고 끝난다
 * @details 전에는 `initialize` 가 `_mutex` 를 쥔 채 장치를 열었고, 그 로그가 같은 `_mutex` 를 다시 잡아 기동이 영구 정지했다.
 */
SW_TEST_CASE( LogTest, InitializeDoesNotDeadlockWhenAnOutputLogsWhileOpening, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const sw::test::ChildRunResult child = runDeadlockScenario( "initialize" );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail() );
}

/**
 * @brief [LogTest] `addOutput` 이 장치를 열다가 남긴 로그와 상한 거절 경고는 리스너가 있어도 교착 없이 방송된다
 */
SW_TEST_CASE( LogTest, AddOutputDoesNotDeadlockWhenItLogs, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const sw::test::ChildRunResult child = runDeadlockScenario( "addOutput" );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail() );
}

/**
 * @brief [LogTest] 장치가 `write` 안에서 남긴 로그는 되돌아와 멈추지 않고 stderr 로 나간다(버려지지 않는다)
 */
SW_TEST_CASE( LogTest, LogFromInsideAnOutputWriteGoesToStderr, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const sw::test::ChildRunResult child = runDeadlockScenario( "writeLogsBack" );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail() );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "a log written from inside write" ), child.getOutputTail() );
}

/**
 * @brief [LogTest] 자식 역할: ConsoleLogOutput 으로 표식 한 줄을 쓴다.
 */
SW_TEST_CHILD_ROLE( LogTest, ConsoleChildWritesAMarker )
{
    sw::ConsoleLogOutput output;
    SW_REQUIRE_TRUE( output.open() );
    output.write( makeProbeRecord( 1, sw::LogLevel::Warning, "SW_CONSOLE_LOG_PROBE warning line\n" ) );
    output.close();
}

/**
 * @brief [LogTest] ConsoleLogOutput 이 쓴 줄은 표준 출력으로 나간다 — 자식 프로세스의 출력을 받아 본다
 */
SW_TEST_CASE( LogTest, ConsoleLogOutputReachesStandardOutput, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const sw::test::ChildRunResult child = sw::test::runChildRole( "LogTest.ConsoleChildWritesAMarker", {}, kLogChildTimeoutSeconds );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail() ); // 기다리지 못했으면 값이 없어 진다
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "SW_CONSOLE_LOG_PROBE warning line" ), child.getOutputTail() );
}

/**
 * @brief [LogTest] 자식 역할: 전역 싱크를 뗀 채 로그 줄을 남기거나(`nosink`), `SW_LOG_ASSERT` 를 실패시킨다(`assert`, 가로채서 멈추지 않는다).
 * @details 싱크를 떼는 동안은 실행기의 로그 감시도 줄을 보지 못한다 — 판정은 부모가 자식의 출력으로 한다. 끝나면 싱크를 되돌린다(자식의 나머지 종료 길).
 */
SW_TEST_CHILD_ROLE( LogTest, StderrChildWritesMarkers )
{
    const sw::string role = sw::test::getChildRoleArgument();
    if ( role == "nosink" )
    {
        sw::ILogSink* pSink = sw::Logger::getGlobalSink();
        sw::Logger::setGlobalSink( nullptr );
        SW_EXPECT_NULL( sw::Logger::getGlobalSink() );
        SW_LOG_ERROR_ID( "Test.LogNoSinkProbe", "SW_NOSINK_PROBE error line" );
        sw::Logger::setGlobalSink( pSink );
    }
    else if ( role == "assert" )
    {
        [[maybe_unused]] sw::test::ScopedAssertCapture capture;
        SW_EXPECT_LOG( SW_ASSERT_LOG_ID ); // 실패한 SW_LOG_ASSERT 는 stderr 에 쓰고 Error 로그도 남긴다
        const bool bLogAssertProbeHolds = false;
        SW_LOG_ASSERT( bLogAssertProbeHolds, "SW_ASSERT_PROBE message" );
    }
    else
    {
        SW_EXPECT_TRUE_MSG( false, "unknown stderr role " + role );
    }
}

/**
 * @brief [LogTest] 싱크가 없을 때 `SW_LOG_*` 줄은 버려지지 않고 stderr 로 나간다
 */
SW_TEST_CASE( LogTest, LineWithoutASinkReachesStandardError, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const sw::test::ChildRunResult child = sw::test::runChildRole( "LogTest.StderrChildWritesMarkers", "nosink", kLogChildTimeoutSeconds );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail() ); // 기다리지 못했으면 값이 없어 진다
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "SW_NOSINK_PROBE error line" ), child.getOutputTail() );
}

#if defined( SW_DEBUG )
/**
 * @brief [LogTest] 실패한 `SW_LOG_ASSERT` 는 식 · 메시지 · 자리를 stderr 에 먼저 남긴다(멈추는 단언은 Debug 에만 있다)
 */
SW_TEST_CASE( LogTest, FailedLogAssertWritesStandardErrorBeforeStopping, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const sw::test::ChildRunResult child = sw::test::runChildRole( "LogTest.StderrChildWritesMarkers", "assert", kLogChildTimeoutSeconds );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail() ); // 기다리지 못했으면 값이 없어 진다
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "[SW_LOG_ASSERT] bLogAssertProbeHolds" ), child.getOutputTail() );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "SW_ASSERT_PROBE message" ), child.getOutputTail() );
}
#endif

/**
 * @brief [LogTest] 자식 역할: 싱크가 없을 때 리스너를 붙이면 순서 위반으로 보고하고 빈 구독을 돌려준다(조용히 무시하지 않는다).
 * @details 전역 싱크를 떼는 일이라 자식에서 한다 — 같은 프로세스면 그동안 다른 스레드의 로그가 실행기의 감시를 비켜 간다.
 */
SW_TEST_CHILD_ROLE( LogTest, ListenerWithoutASinkChild )
{
    sw::test::ScopedAssertCapture capture;
    sw::ILogSink*                 pSink = sw::Logger::getGlobalSink();
    sw::Logger::setGlobalSink( nullptr ); // 싱크 자체를 비운다 — 단언의 Error 로그는 stderr 로 간다(실행기가 보지 못한다)
    sw::Subscription subscription = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, []( const sw::LogEntry& ) {} ) );
    sw::Logger::setGlobalSink( pSink );
    SW_EXPECT_FALSE( subscription.isActive() );
    if constexpr ( sw::test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 1u, capture.getCount() );
}

/**
 * @brief [LogTest] 싱크가 없을 때 리스너를 붙이면 순서 위반으로 보고된다 — 자식이 판정하고 stderr 에 단언 줄이 남는다
 */
SW_TEST_CASE( LogTest, RegisteringAListenerWithoutASinkIsReported, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const sw::test::ChildRunResult child = sw::test::runChildRole( "LogTest.ListenerWithoutASinkChild", {}, kLogChildTimeoutSeconds );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail() ); // 기다리지 못했으면 값이 없어 진다
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "needs a global log sink" ), child.getOutputTail() );
}
