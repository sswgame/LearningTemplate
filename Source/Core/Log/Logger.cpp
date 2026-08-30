#include "pch.h"

#include "Core/Log/Logger.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

namespace sw
{
	namespace
	{
		/** @brief 프로세스 전역 활성 로그 싱크 포인터 */
		atomic<ILogSink*> s_globalSink{ nullptr };
		/** @brief 파일명 해시별 로그 Caller 이름 캐시 */
		unordered_map<uint64, string> s_mapFileToCaller{};
		mutex						  s_callerMutex{};
	} // namespace

	Logger::Logger()
		: _logFolderPath{}
		, _currentLogFileName{}
		, _onLogWritten{}
		, _queue{}
		, _workerThread{}
		, _cv{}
		, _mutex{}
		, _cvMutex{}
		, _timeMutex{}
		, _pFile{ nullptr }
		, _pCachedConsoleHandle{ nullptr }
		, _cachedTimeSec{ 0 }
		, _cachedYear{ 0 }
		, _cachedMonth{ 0 }
		, _cachedDay{ 0 }
		, _cachedHour{ 0 }
		, _lastLogHour{ -1 }
		, _defaultConsoleAttribute{ 0 }
		, _bIsRunning{ false }
		, _bInitialized{ false }
		, _bHasConsole{ false }
		, _arrCachedDateStr{}
	{
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

		initializeInternal();
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
		if ( _pFile != nullptr )
		{
			std::fflush( _pFile );
			std::fclose( _pFile );
			_pFile = nullptr;
		}
		_onLogWritten.removeAll();
		_lastLogHour  = -1;
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

	void Logger::registerCaller( string_view filePath, string_view callerName )
	{
		string_view fileName;
		FileUtil::getFileNamePart( filePath, fileName );
		const uint64			fileHash = StringUtil::computeHash64( fileName );
		std::scoped_lock<mutex> lock{ s_callerMutex };
		s_mapFileToCaller[fileHash] = string{ callerName };
	}

	const utf8* Logger::getCaller( const utf8* pFile )
	{
		if ( pFile == nullptr || pFile[0] == '\0' )
			return nullptr;
		string_view fileName;
		FileUtil::getFileNamePart( string_view{ pFile }, fileName );
		const uint64			fileHash = StringUtil::computeHash64( fileName );
		std::scoped_lock<mutex> lock{ s_callerMutex };
		auto					it = s_mapFileToCaller.find( fileHash );
		if ( it != s_mapFileToCaller.end() )
			return it->second.c_str();
		return nullptr;
	}

	const string& Logger::getLogFolderPath()
	{
		return _logFolderPath;
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

	void Logger::initializeInternal()
	{
		const string execPath = FileUtil::getExecutablePath();
		const string baseDir  = execPath.empty() ? FileUtil::getCurrentPath() : FileUtil::getDirectoryPart( execPath );

		_logFolderPath = FileUtil::joinPath( FileUtil::joinPath( baseDir, path::kSavedFolder ), path::kLogsFolder );
		FileUtil::ensureDirectoryExists( _logFolderPath );

#if defined( SW_PLATFORM_WINDOWS )
		SetConsoleOutputCP( CP_UTF8 );
		SetConsoleCP( CP_UTF8 );

		const HANDLE consoleHandle = GetStdHandle( STD_OUTPUT_HANDLE );
		DWORD		 dwMode{ 0 };
		if ( consoleHandle != nullptr && consoleHandle != INVALID_HANDLE_VALUE && GetConsoleMode( consoleHandle, &dwMode ) )
		{
			CONSOLE_SCREEN_BUFFER_INFO consoleInfo{};
			GetConsoleScreenBufferInfo( consoleHandle, &consoleInfo );
			_pCachedConsoleHandle	 = consoleHandle;
			_defaultConsoleAttribute = static_cast<uint16>( consoleInfo.wAttributes );
			_bHasConsole			 = true;
		}
		else
		{
			_pCachedConsoleHandle = nullptr;
			_bHasConsole		  = false;
		}
#endif
	}

	void Logger::workerLoop()
	{
		while ( _bIsRunning.load( std::memory_order_acquire ) || _queue.empty() == false )
		{
			LogRecord record;
			bool	  bProcessedAny = false;
			while ( _queue.dequeue( record ) )
			{
				bProcessedAny = true;
				std::scoped_lock<mutex> lock{ _mutex };
				writeLogConsole( record._level, record._formatted.c_str() );
				writeLogFile( record._level, record._year, record._month, record._day, record._hour, record._formatted.c_str() );
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
			std::scoped_lock<mutex> lock{ _mutex };
			writeLogConsole( record._level, record._formatted.c_str() );
			writeLogFile( record._level, record._year, record._month, record._day, record._hour, record._formatted.c_str() );
		}
	}

	void Logger::writeLogInternal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line )
	{
		// 1단계: 타임스탬프 계산 및 포맷팅 (동일 초 내에서는 캐시된 문자열 재사용)
		int32 year{ 0 }, month{ 0 }, day{ 0 }, hour{ 0 };

		fixed_string<constant::kMaxBuffer32> dateStr{};
		{
			std::scoped_lock<mutex> lock{ _timeMutex };
			const std::time_t		timeSec = std::time( nullptr );
			if ( timeSec == _cachedTimeSec )
			{
				dateStr = _arrCachedDateStr;
				year	= _cachedYear;
				month	= _cachedMonth;
				day		= _cachedDay;
				hour	= _cachedHour;
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
				year			   = localTime.tm_year + 1900;
				month			   = localTime.tm_mon + 1;
				day				   = localTime.tm_mday;
				hour			   = localTime.tm_hour;
				const int32 minute = localTime.tm_min;
				const int32 second = localTime.tm_sec;

				formatstring( _arrCachedDateStr, sizeof( _arrCachedDateStr ), "%#-%#-%# %#:%#:%#", year, month, day, hour, minute, second );
				dateStr		   = _arrCachedDateStr;
				_cachedTimeSec = timeSec;
				_cachedYear	   = year;
				_cachedMonth   = month;
				_cachedDay	   = day;
				_cachedHour	   = hour;
			}
		}

		static constexpr const utf8* kArrHeader[] = { "Error", "Warning", "Info", "Trace" };
		static_assert( SW_COUNT_OF( kArrHeader ) == static_cast<uint32>( LogLevel::Count ), "LogLevel과 같아야 합니다" );

		const size_t levelIndex		 = static_cast<size_t>( level );
		const utf8*	 effectiveTag	 = ( StringUtil::isNullOrEmpty( pTag ) ) ? constant::kDefaultLogTag : pTag;
		const utf8*	 effectiveFile	 = ( StringUtil::isNullOrEmpty( pFile ) ) ? constant::kDefaultLogFile : pFile;
		const utf8*	 effectiveMsg	 = ( pMessage != nullptr ) ? pMessage : "";
		const utf8*	 effectiveCaller = pCaller;
		if ( StringUtil::isNullOrEmpty( effectiveCaller ) && pFile != nullptr )
		{
			effectiveCaller = getCaller( pFile );
		}

		// 2단계: 스택 8KB fixed_string 버퍼에 1회 포맷팅 (동적 힙 메모리 할당 0건)
		fixed_string<constant::kMaxBuffer8192> formattedBuffer{};
		if ( StringUtil::isNullOrEmpty( effectiveCaller ) == false )
		{
			formatstring( formattedBuffer.data(), formattedBuffer.capacity(),
						  "[%#] [%#] [%#] [%#] - %#\n -> %#:%#\n",
						  dateStr.c_str(), effectiveTag, effectiveCaller, kArrHeader[levelIndex], effectiveMsg, effectiveFile, line );
		}
		else
		{
			formatstring( formattedBuffer.data(), formattedBuffer.capacity(),
						  "[%#] [%#] [%#] - %#\n -> %#:%#\n",
						  dateStr.c_str(), effectiveTag, kArrHeader[levelIndex], effectiveMsg, effectiveFile, line );
		}

		// 3단계: 64비트 SWAR 기반 고속 UTF-8 검증 및 Non-UTF8(ANSI/CP949) 한글 안전 자동 변환
		string		fallbackUtf8;
		const utf8* pFormattedBuffer = formattedBuffer.c_str();
		if ( StringUtil::isValidUTF8( pFormattedBuffer ) == false )
		{
			fallbackUtf8	 = StringUtil::localeToUtf8( pFormattedBuffer );
			pFormattedBuffer = fallbackUtf8.c_str();
		}

		// 4단계: 인메모리 리스너(에디터 콘솔 UI/테스트 캡처) 스냅샷 복사 후 락 밖에서 안전하게 전파
		LogWrittenMulticast listenersCopy;
		bool				bHasListeners{ false };
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
			entry._level	 = level;
			entry._tag		 = effectiveTag;
			entry._caller	 = ( effectiveCaller != nullptr ) ? effectiveCaller : "";
			entry._message	 = effectiveMsg;
			entry._file		 = effectiveFile;
			entry._line		 = line;
			entry._timeStamp = dateStr.c_str();
			listenersCopy.broadcast( entry );
		}

		// 5단계: 콘솔 및 파일 비동기 I/O 큐 인큐 (초기화 전이거나 큐 풀일 경우 동기 폴백)
		if ( _bInitialized == false || _bIsRunning.load( std::memory_order_relaxed ) == false )
		{
			std::scoped_lock<mutex> lock{ _mutex };
			writeLogConsole( level, pFormattedBuffer );
			writeLogFile( level, year, month, day, hour, pFormattedBuffer );
			return;
		}

		LogRecord record;
		record._level	  = level;
		record._formatted = pFormattedBuffer;
		record._year	  = year;
		record._month	  = month;
		record._day		  = day;
		record._hour	  = hour;

		if ( _queue.enqueue( std::move( record ) ) == false )
		{
			std::scoped_lock<mutex> lock{ _mutex };
			writeLogConsole( level, pFormattedBuffer );
			writeLogFile( level, year, month, day, hour, pFormattedBuffer );
			return;
		}

		_cv.notify_one();
	}

	void Logger::writeLogConsole( LogLevel level, const utf8* pMessage )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( _bHasConsole && _pCachedConsoleHandle != nullptr )
		{
			HANDLE				  consoleHandle = static_cast<HANDLE>( _pCachedConsoleHandle );
			static constexpr WORD arrLevelColor[] =
				{
					FOREGROUND_RED | FOREGROUND_INTENSITY,
					FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
					FOREGROUND_GREEN | FOREGROUND_INTENSITY,
					FOREGROUND_INTENSITY,
				};

			SetConsoleTextAttribute( consoleHandle, arrLevelColor[static_cast<int32>( level )] );
			std::fputs( pMessage, stdout );
			if ( level == LogLevel::Error )
				std::fflush( stdout );
			SetConsoleTextAttribute( consoleHandle, static_cast<WORD>( _defaultConsoleAttribute ) );
		}
		else
		{
			std::fputs( pMessage, stdout );
			if ( level == LogLevel::Error )
				std::fflush( stdout );
			OutputDebugStringA( pMessage );
		}
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		// Linux/POSIX ANSI Escape Sequences: Bold Red (Error), Bold Yellow (Warning), Bold Green (Info), Gray (Trace)
		static constexpr const utf8* arrAnsiColor[] =
			{
				"\033[1;31m", // Error: Bold Red
				"\033[1;33m", // Warning: Bold Yellow
				"\033[1;32m", // Info: Bold Green
				"\033[0;90m", // Trace: Gray
			};
		static constexpr const utf8* kAnsiReset = "\033[0m";

		const int32 index = static_cast<int32>( level );
		if ( 0 <= index && index < static_cast<int32>( LogLevel::Count ) )
		{
			std::fputs( arrAnsiColor[index], stdout );
			std::fputs( pMessage, stdout );
			std::fputs( kAnsiReset, stdout );
		}
		else
		{
			std::fputs( pMessage, stdout );
		}
		if ( level == LogLevel::Error )
			std::fflush( stdout );
#else
		std::ignore = level;
		std::fputs( pMessage, stdout );
		if ( level == LogLevel::Error )
			std::fflush( stdout );
#endif
	}

	void Logger::writeLogFile( LogLevel level, const int32 year, const int32 month, const int32 day, const int32 hour, const utf8* pFormattedBuffer )
	{
		if ( _bInitialized == false )
			return;

		if ( _pFile == nullptr || hour != _lastLogHour )
		{
			if ( _pFile != nullptr )
			{
				std::fclose( _pFile );
				_pFile = nullptr;
			}

			_lastLogHour = hour;
			fixed_string<constant::kMaxBuffer128> expectedFileName{};
			formatstring( expectedFileName.data(), expectedFileName.capacity(), "LOG_%#-%#-%#-%#.txt", year, month, day, hour );
			_currentLogFileName = expectedFileName.c_str();

			const string logPath = FileUtil::joinPath( _logFolderPath, _currentLogFileName );
#if defined( SW_PLATFORM_WINDOWS )
			fopen_s( &_pFile, logPath.c_str(), "a" );
#else
			_pFile = fopen( logPath.c_str(), "a" );
#endif
		}

		if ( _pFile == nullptr )
			return;

		std::fputs( pFormattedBuffer, _pFile );
		if ( level == LogLevel::Error )
			std::fflush( _pFile );
	}

} // namespace sw
