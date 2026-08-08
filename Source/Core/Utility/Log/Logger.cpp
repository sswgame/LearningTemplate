/**
 * @file Logger.cpp
 * @brief Logger 구현
 */
#include "pch.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/String/fixed_string.h"
#include "Core/Common/PlatformHeaders.h"

namespace sw
{
	namespace
	{
		constexpr const utf8* kDefaultLogFolder = "Log";
		Logger*				  s_loggerInstance	= nullptr;
	} // namespace

	Logger::Logger()
	{
		SW_ASSERT( s_loggerInstance == nullptr );
		s_loggerInstance = this;
	}

	Logger::~Logger()
	{
		if ( s_loggerInstance == this )
			s_loggerInstance = nullptr;
	}

	void Logger::initialize()
	{
		if ( _bInitialized )
			return;

		SW_ASSERT( s_loggerInstance != nullptr );
		s_loggerInstance->initializeInternal();
	}

	void Logger::shutdown()
	{
		std::lock_guard<std::mutex> lock{ _mutex };
		if ( _pFile != nullptr )
		{
			std::fflush( _pFile );
			std::fclose( _pFile );
			_pFile = nullptr;
		}
		_onLogWritten.removeAll();
		_bInitialized = false;
	}

	void Logger::initializeInternal()
	{
		if ( _bInitialized )
			return;

		std::filesystem::path baseDir = FileUtil::getCurrentPath();
#if defined( SW_PLATFORM_WINDOWS )
		baseDir = std::filesystem::path( FileUtil::getExecutablePath() ).parent_path();
#endif

		_logFolderPath = ( baseDir / kDefaultLogFolder ).string();

		if ( FileUtil::isFileExist( _logFolderPath ) == false )
			FileUtil::createDirectory( _logFolderPath );
		_bInitialized = true;

#if defined( SW_PLATFORM_WINDOWS )
		SetConsoleOutputCP( CP_UTF8 );
		SetConsoleCP( CP_UTF8 );
#endif
	}

	void Logger::writeLog( LogLevel level, const utf8* tag, const utf8* pMessage, const utf8* file, int32 line )
	{
		SW_ASSERT( s_loggerInstance );
		s_loggerInstance->writeLogInternal( level, tag, pMessage, file, line );
	}

	void Logger::writeLogInternal( LogLevel level, const utf8* tag, const utf8* pMessage, const utf8* file, int32 line )
	{
		SW_ASSERT( _bInitialized );

		const auto&		  now  = std::chrono::system_clock::now();
		const std::time_t time = std::chrono::system_clock::to_time_t( now );
		std::tm*		  pLocalTime{};
#if defined( SW_PLATFORM_WINDOWS )
		std::tm localTime{};
		localtime_s( &localTime, &time );
		pLocalTime = &localTime;
#else
		pLocalTime = std::localtime( &time );
#endif
		const int32 year   = pLocalTime->tm_year + 1900;
		const int32 month  = pLocalTime->tm_mon + 1;
		const int32 day	   = pLocalTime->tm_mday;
		const int32 hour   = pLocalTime->tm_hour;
		const int32 minute = pLocalTime->tm_min;
		const int32 second = pLocalTime->tm_sec;

		constexpr int32			   kMaxDateSize = 32;
		fixed_string<kMaxDateSize> date{};
		formatstring( date.data(), date.capacity(), "%#-%#-%# %#:%#:%#", year, month, day, hour, minute, second );

		static constexpr const utf8* kArrHeader[] = { "Error", "Warning", "Info", "Trace" };
		static_assert( SW_COUNT_OF( kArrHeader ) == static_cast<uint32>( LogLevel::Count ), "LogLevel과 같아야 합니다" );

		const uint8 levelIndex = static_cast<uint8>( level );
		if ( levelIndex >= static_cast<uint8>( LogLevel::Count ) )
			return;

		const utf8* effectiveTag  = ( StringUtil::isNullOrEmpty( tag ) ) ? "Engine" : tag;
		const utf8* effectiveFile = ( StringUtil::isNullOrEmpty( file ) ) ? "unknown" : file;
		const utf8* effectiveMsg  = ( pMessage != nullptr ) ? pMessage : "";

		LogEntry entry;
		entry.level		= level;
		entry.tag		= effectiveTag;
		entry.message	= effectiveMsg;
		entry.file		= effectiveFile;
		entry.line		= line;
		entry.timeStamp = date.c_str();

		fixed_string<constant::kMaxBuffer8192> formattedBuffer{};
		// 경로:줄 은 메시지 뒤에 둔다. 선행 file(line): 은 IDE problem matcher가 빌드 경고로 오인한다.
		formatstring( formattedBuffer.data(), formattedBuffer.capacity(),
					  "[%#] [%#] [%#] - %#\n> %#:%#\n",
					  date.data(), effectiveTag, kArrHeader[levelIndex], effectiveMsg, effectiveFile, line );

		LogWrittenMulticast listeners;
		{
			std::lock_guard<std::mutex> lock{ _mutex };
			writeLogConsole( level, formattedBuffer.c_str() );
			writeLogFile( level, year, month, day, hour, formattedBuffer.c_str() );
			listeners = _onLogWritten;
		}

		if ( listeners.isBound() )
			listeners.broadcast( entry );
	}

	void Logger::writeLogConsole( LogLevel level, const utf8* pMessage )
	{
#if defined( SW_PLATFORM_WINDOWS )
		const HANDLE consoleHandle = GetStdHandle( STD_OUTPUT_HANDLE );
		DWORD		 dwMode		   = 0;
		if ( consoleHandle != nullptr && consoleHandle != INVALID_HANDLE_VALUE && GetConsoleMode( consoleHandle, &dwMode ) )
		{
			CONSOLE_SCREEN_BUFFER_INFO consoleInfo{};
			GetConsoleScreenBufferInfo( consoleHandle, &consoleInfo );
			const WORD defaultAttribute = consoleInfo.wAttributes;

			static constexpr WORD arrLevelColor[] =
				{
					FOREGROUND_RED | FOREGROUND_INTENSITY,
					FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
					FOREGROUND_GREEN | FOREGROUND_INTENSITY,
					FOREGROUND_INTENSITY,
				};

			SetConsoleTextAttribute( consoleHandle, arrLevelColor[static_cast<int32>( level )] );
			std::fputs( pMessage, stdout );
			std::fflush( stdout );
			SetConsoleTextAttribute( consoleHandle, defaultAttribute );
		}
		else
		{
			std::fputs( pMessage, stdout );
			std::fflush( stdout );
			OutputDebugStringA( pMessage );
		}
#else
		std::ignore = level;
		std::fputs( pMessage, stdout );
		std::fflush( stdout );
#endif
	}

	void Logger::writeLogFile( LogLevel level, const int32 year, const int32 month, const int32 day, const int32 hour, const utf8* formattedBuffer )
	{
		fixed_string<constant::kMaxBuffer128> expectedFileName{};
		formatstring( expectedFileName.data(), expectedFileName.capacity(), "LOG_%#-%#-%#-%#.txt", year, month, day, hour );

		if ( _currentLogFileName != expectedFileName.data() )
		{
			if ( _pFile != nullptr )
			{
				std::fclose( _pFile );
				_pFile = nullptr;
			}

			_currentLogFileName = expectedFileName.data();
			std::filesystem::path logPath{ _logFolderPath };
			logPath /= _currentLogFileName;

#if defined( SW_PLATFORM_WINDOWS )
			fopen_s( &_pFile, logPath.string().c_str(), "a" );
#else
			_pFile = fopen( logPath.string().c_str(), "a" );
#endif
		}

		if ( _pFile == nullptr )
			return;

		std::fprintf( _pFile, "%s", StringUtil::localeToUtf8( formattedBuffer ).c_str() );
		if ( level == LogLevel::Error )
			std::fflush( _pFile );
	}

	const std::string& Logger::getLogFolderPath()
	{
		if ( s_loggerInstance )
			return s_loggerInstance->_logFolderPath;

		static std::string emptyPath;
		return emptyPath;
	}

	DelegateHandle Logger::addLogWrittenListener( const LogWrittenDelegate& listener )
	{
		if ( s_loggerInstance == nullptr )
			return {};

		std::lock_guard<std::mutex> lock{ s_loggerInstance->_mutex };
		return s_loggerInstance->_onLogWritten.add( listener );
	}

	void Logger::removeLogWrittenListener( const DelegateHandle& handle )
	{
		if ( s_loggerInstance == nullptr )
			return;

		std::lock_guard<std::mutex> lock{ s_loggerInstance->_mutex };
		s_loggerInstance->_onLogWritten.remove( handle );
	}

} // namespace sw
