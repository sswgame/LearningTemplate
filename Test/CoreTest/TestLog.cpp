#include "pch.h"

#include "TestFramework/TestFramework.h"

SW_LOG_CALLER( "TestLog" );

namespace
{
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
				if ( entry._message.rfind( kCapturePrefix, 0 ) == 0 )
					_entries.push_back( entry );
			} ) );
		}

		/** @brief 전역 싱크에서 리스너를 뗍니다. */
		~LogCapture()
		{
			sw::Logger::removeGlobalListener( _handle );
		}

		LogCapture( const LogCapture& )			   = delete;
		LogCapture& operator=( const LogCapture& ) = delete;

		/** @brief 리스너가 실제로 등록되었는지 반환합니다. */
		bool isAttached() const
		{
			return _handle.isValid();
		}

		/** @brief 수집된 로그 목록입니다. */
		const sw::vector<sw::LogEntry>& getEntries() const
		{
			return _entries;
		}

		/** @brief 수집된 로그 개수입니다. */
		uint32 getCount() const
		{
			return static_cast<uint32>( _entries.size() );
		}

	private:
		sw::vector<sw::LogEntry> _entries;
		sw::DelegateHandle		 _handle;
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
			_handle = sw::Logger::addGlobalListener( SW_DELEGATE_LAMBDA(
				sw::LogWrittenDelegate,
				[this]( const sw::LogEntry& entry )
			{
				if ( entry._message.rfind( kCapturePrefix, 0 ) == 0 )
				{
					std::scoped_lock<sw::mutex> lock{ _mutex };
					_entries.push_back( entry );
				}
			} ) );
		}

		~ThreadSafeLogCapture()
		{
			sw::Logger::removeGlobalListener( _handle );
		}

		ThreadSafeLogCapture( const ThreadSafeLogCapture& )			   = delete;
		ThreadSafeLogCapture& operator=( const ThreadSafeLogCapture& ) = delete;

		bool isAttached() const
		{
			return _handle.isValid();
		}

		[[maybe_unused]] sw::vector<sw::LogEntry> getEntries() const
		{
			std::scoped_lock<sw::mutex> lock{ _mutex };
			return _entries;
		}

		uint32 getCount() const
		{
			std::scoped_lock<sw::mutex> lock{ _mutex };
			return static_cast<uint32>( _entries.size() );
		}

	private:
		mutable sw::mutex		 _mutex;
		sw::vector<sw::LogEntry> _entries;
		sw::DelegateHandle		 _handle;
	};
} // namespace

// ------------------------------------------------------------------------------
// 1) Core_Log — 초기화·폴더·매크로
// ------------------------------------------------------------------------------
/**
 * @brief [Core_Log] 로거 초기화
 */
SW_TEST_CASE( Core_Log, LoggerInitialized )
{
	const sw::string& folderPath = sw::Logger::getGlobalSink()->getLogFolderPath();
	SW_EXPECT_FALSE( folderPath.empty() );
}

/**
 * @brief [Core_Log] 로그 폴더 존재
 */
SW_TEST_CASE( Core_Log, LogFolderExists )
{
	const sw::string& folderPath = sw::Logger::getGlobalSink()->getLogFolderPath();
	if ( folderPath.empty() == false )
	{
		SW_EXPECT_TRUE( sw::FileUtil::directoryExists( folderPath ) );
	}
}

/**
 * @brief [Core_Log] 각 레벨의 로그가 싱크까지 전달됨
 */
SW_TEST_CASE( Core_Log, WriteLogDeliversEveryLevel )
{
#if defined( SW_DEBUG )
	LogCapture capture;
	SW_ASSERT_TRUE( capture.isAttached() );

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
 * @brief [Core_Log] 로그 레벨 개수
 */
SW_TEST_CASE( Core_Log, LogLevelCount )
{
	constexpr uint32 expected = 4u;
	SW_EXPECT_EQUAL( expected, static_cast<uint32>( sw::LogLevel::Count ) );
}

/**
 * @brief [Core_Log] 로그 매크로가 %# 인자를 치환해 기록함
 */
SW_TEST_CASE( Core_Log, LogMacrosFormatArguments )
{
#if defined( SW_DEBUG )
	LogCapture capture;
	SW_ASSERT_TRUE( capture.isAttached() );

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
 * @brief [Core_Log] 비-UTF8(ANSI/CP949) 문자열 전달 시에도 안전하게 처리됨
 */
SW_TEST_CASE( Core_Log, NonUtf8FallbackSafety )
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
 * @brief [Core_Log] Caller 지정 및 수신 검증
 */
SW_TEST_CASE( Core_Log, LogCallerHandling )
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
 * @brief [Core_Log] 8개 스레드 동시 대량 로깅 스트레스 테스트 (데이터 레이스/크래시 검증)
 */
SW_TEST_CASE( Core_Log, ConcurrentMultiThreadedLogging )
{
#if defined( SW_DEBUG )
	ThreadSafeLogCapture capture;
	SW_ASSERT_TRUE( capture.isAttached() );

	constexpr uint32 kThreadCount		= 8;
	constexpr uint32 kLogsPerThread		= 100;
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
 * @brief [Core_Log] 큐 용량(4096) 초과 시 동기 폴백 안전성 검증
 */
SW_TEST_CASE( Core_Log, QueueOverflowFallbackStress )
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
 * @brief [Core_Log] 로거 shutdown 시 큐 잔여 로그 플러시(Drain) 일관성 검증
 */
SW_TEST_CASE( Core_Log, ShutdownQueueDrainConsistency )
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
 * @brief [Core_Log] 로깅 진행 중 동시 리스너 등록/해제 동시성 검증
 */
SW_TEST_CASE( Core_Log, ConcurrentListenerAttachDetach )
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
