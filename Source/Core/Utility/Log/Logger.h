#pragma once
/**
 * @file Logger.h
 * @brief 엔진 전체에서 사용되는 로깅 시스템
 *
 * 로그 태그(Engine / Editor / Game)는 호출 모듈의 컴파일 정의 SW_LOG_TAG로 결정됩니다.
 * 전역 _target을 덮어쓰지 않으므로 모듈 로드 후에도 출처가 유지됩니다.
 */
#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonDefines.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Utility/String/formatString.h"
#include <cstdio>

#if !defined( SW_LOG_TAG )
	#define SW_LOG_TAG "Engine"
#endif

namespace sw
{

	/**
	 * @class Logger
	 * @brief 파일 출력 및 콘솔 색상 출력을 지원하는 전역 로깅 시스템
	 */
	class Logger final
	{
	public:
		Logger();
		~Logger();
		/**
		 * @enum LogLevel
		 * @brief 로그의 중요도/심각도 레벨 식별자
		 */
		enum class LogLevel
		{
			Error,	 /**< 심각한 오류, 프로그램 흐름에 영향 */
			Warning, /**< 경고, 잠재적인 문제 내포 */
			Info,	 /**< 일반적인 정보 (초기화, 종료 등) */
			Trace,	 /**< 상세한 디버그 추적 정보 */
			Count	 /**< 로그 레벨의 총 개수 */
		};

		/**
		 * @brief App이 소유한 Logger 인스턴스를 기동합니다.
		 */
		bool startup()
		{
			if ( _bInitialized )
				return true;

			initializeInternal();
			return true;
		}

		/**
		 * @brief 종료 함수. 사용 중인 로그 파일 스트림을 닫고 리소스를 해제합니다.
		 */
		void shutdown()
		{
			std::lock_guard<std::mutex> lock( _mutex );
			if ( _pFile != nullptr )
			{
				std::fflush( _pFile );
				std::fclose( _pFile );
				_pFile = nullptr;
			}
			_bInitialized = false;
		}

		/**
		 * @brief 테스트/툴 등에서 Logger 인스턴스가 이미 생성된 뒤 경로만 준비할 때 사용합니다.
		 */
		static void initialize();

		/**
		 * @brief 로그 파일들이 저장되는 루트 폴더의 절대 경로를 반환합니다.
		 */
		static const std::string& getLogFolderPath();

		/**
		 * @brief 지정된 레벨·태그로 로그 메시지를 기록합니다.
		 * @param tag 호출 모듈 태그 (보통 SW_LOG_TAG)
		 * @param file 호출 위치 파일 (__FILE__)
		 * @param line 호출 위치 줄 (__LINE__)
		 */
		static void writeLog( LogLevel level, const utf8* tag, const utf8* pMessage, const utf8* file, int32 line );

	public:
		void initializeInternal();
		void writeLogInternal( LogLevel level, const utf8* tag, const utf8* pMessage, const utf8* file, int32 line );
		void writeLogConsole( LogLevel level, const utf8* pMessage );
		void writeLogFile( LogLevel level, int32 year, int32 month, int32 day, int32 hour, const utf8* formattedBuffer );

		std::string _logFolderPath = {};
		std::mutex	_mutex		   = {};
		bool		_bInitialized  = false;

		std::FILE*	_pFile				= nullptr;
		std::string _currentLogFileName = {};
	};
}

// ============================================================================
// [매크로 인터페이스 정의 영역]
// ============================================================================

#if defined( SW_DEBUG )
	/**
	 * @brief 포맷 문자열 파싱 후 Logger::writeLog 로 전달하는 코어 매크로 구현체
	 */
	#define SW_LOG_INTERNAL( level, message, ... )                                                          \
		do                                                                                                  \
		{                                                                                                   \
			utf8 _buffer[sw::constant::kMaxBuffer8192];                                                     \
			sw::formatstring( _buffer, sw::constant::kMaxBuffer8192, message __VA_OPT__(, ) __VA_ARGS__ ); \
			sw::Logger::writeLog( level, SW_LOG_TAG, _buffer, __FILE__, __LINE__ );                         \
		} while ( false )

	/** @brief SW_LOG_ERROR 매크로 정의입니다. */
	#define SW_LOG_ERROR( message, ... )   SW_LOG_INTERNAL( sw::Logger::LogLevel::Error, message __VA_OPT__(, ) __VA_ARGS__ )
	/** @brief SW_LOG_WARNING 매크로 정의입니다. */
	#define SW_LOG_WARNING( message, ... ) SW_LOG_INTERNAL( sw::Logger::LogLevel::Warning, message __VA_OPT__(, ) __VA_ARGS__ )
	/** @brief SW_LOG_INFO 매크로 정의입니다. */
	#define SW_LOG_INFO( message, ... )	   SW_LOG_INTERNAL( sw::Logger::LogLevel::Info, message __VA_OPT__(, ) __VA_ARGS__ )
	/** @brief SW_LOG_TRACE 매크로 정의입니다. */
	#define SW_LOG_TRACE( message, ... )   SW_LOG_INTERNAL( sw::Logger::LogLevel::Trace, message __VA_OPT__(, ) __VA_ARGS__ )
	/**
	 * @brief 조건 실패 시 메시지·식·파일·함수·라인을 Error로 남기고 디버그 브레이크
	 * @note Release에서는 no-op. 무조건 실패는 SW_LOG_ASSERT( false, ... )로 호출.
	 */
	#define SW_LOG_ASSERT( expr, message, ... )                                                                     \
		do                                                                                                          \
		{                                                                                                           \
			if ( !( expr ) )                                                                                        \
			{                                                                                                       \
				utf8 _assertMsg[sw::constant::kMaxBuffer8192];                                                      \
				sw::formatstring( _assertMsg, sw::constant::kMaxBuffer8192, message __VA_OPT__(, ) __VA_ARGS__ ); \
				SW_LOG_INTERNAL( sw::Logger::LogLevel::Error,                                                       \
								 "ASSERT failed\n"                                                                  \
								 "Expression : %#\n"                                                                \
								 "Message    : %#\n"                                                                \
								 "FileName   : %#\n"                                                                \
								 "Function   : %#\n"                                                                \
								 "Line       : %#",                                                                 \
								 #expr, _assertMsg, __FILE__, SW_FUNCTION_SIGNATURE, __LINE__ );                    \
				SW_DEBUG_BREAK();                                                                                   \
			}                                                                                                       \
		} while ( false )
#else
	/** @brief SW_LOG_ERROR 매크로 정의입니다. */
	#define SW_LOG_ERROR( message, ... )
	/** @brief SW_LOG_WARNING 매크로 정의입니다. */
	#define SW_LOG_WARNING( message, ... )
	/** @brief SW_LOG_INFO 매크로 정의입니다. */
	#define SW_LOG_INFO( message, ... )
	/** @brief SW_LOG_TRACE 매크로 정의입니다. */
	#define SW_LOG_TRACE( message, ... )
	/** @brief SW_LOG_ASSERT 매크로 정의입니다. */
	#define SW_LOG_ASSERT( expr, message, ... )
#endif
