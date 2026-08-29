/**
 * @file Logger.h
 * @brief 엔진 전체에서 사용되는 로깅 시스템
 *
 * 로그 태그(Engine / Editor / Game)는 호출 모듈의 컴파일 정의 SW_LOG_TAG로 결정됩니다.
 * 전역 _target을 덮어쓰지 않으므로 모듈 로드 후에도 출처가 유지됩니다.
 * 에디터 표시 등은 addLogWrittenListener 로 구독합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/formatString.h"

#if !defined( SW_LOG_TAG )
	/** @brief 호출 모듈 태그. 모듈 헤드에서 재정의합니다. */
	#define SW_LOG_TAG "Engine"
#endif

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) LogLevel / LogEntry / LogRecord — 심각도 + 한 줄 기록 + 비동기 큐 레코드
	// ------------------------------------------------------------------------------
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

	/** @brief 싱크·리스너에 넘기는 한 줄입니다. */
	struct LogEntry
	{
		string	 _tag;
		string	 _caller;
		string	 _message;
		string	 _file;
		string	 _timeStamp;
		int32	 _line{ 0 };
		LogLevel _level = LogLevel::Info;
	};

	/** @brief 비동기 락-프리 큐 전송용 로그 레코드 */
	struct LogRecord
	{
		string	 _formatted;
		int32	 _year{ 0 };
		int32	 _month{ 0 };
		int32	 _day{ 0 };
		int32	 _hour{ 0 };
		LogLevel _level = LogLevel::Info;
	};

	SW_DECLARE_MULTI_CAST_DELEGATE( void, LogWrittenMulticast, const LogEntry& );
	using LogWrittenDelegate = Delegate<void( const LogEntry& )>;

	// ------------------------------------------------------------------------------
	// 2) ILogSink — 콘솔/파일 구현을 갈아 끼울 수 있는 출력면
	// ------------------------------------------------------------------------------
	/**
	 * @class ILogSink
	 * @brief 로그 출력을 담당하는 추상 인터페이스
	 */
	class SW_API ILogSink
	{
	public:
		/** @brief 파일 핸들과 리스너를 닫습니다. */
		virtual ~ILogSink() = default;
		/** @brief 로그 폴더와 출력 대상을 엽니다. */
		virtual void initialize() = 0;
		/** @brief 파일과 리스너를 닫습니다. */
		virtual void shutdown() = 0;
		/** @brief 한 줄을 콘솔·파일·리스너에 남깁니다. */
		virtual void writeLog( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line ) = 0;
		/** @brief 한 줄이 쓰일 때 호출할 리스너를 붙입니다. */
		virtual DelegateHandle addLogWrittenListener( const LogWrittenDelegate& listener ) = 0;
		/** @brief 핸들로 리스너를 뗍니다. */
		virtual void removeLogWrittenListener( const DelegateHandle& handle ) = 0;
		/** @brief 로그 파일이 있는 폴더 경로입니다. */
		virtual const string& getLogFolderPath() = 0;
	};

	// ------------------------------------------------------------------------------
	// 3) Logger — 기본 싱크. 전역 호출은 setGlobalSink 후 writeLogGlobal
	// ------------------------------------------------------------------------------
	/**
	 * @class Logger
	 * @brief 전역 로깅 시스템 및 기본 파일/콘솔 ILogSink 구현체 (Lock-Free Async Ring Buffer 기반)
	 */
	class SW_API Logger final : public ILogSink
	{
	public:
		/** @brief 파일 포인터와 플래그를 비운 상태로 둡니다. */
		Logger();
		/** @brief 열려 있는 로그 파일을 닫습니다. */
		virtual ~Logger() override;

		Logger( const Logger& )			   = delete;
		Logger& operator=( const Logger& ) = delete;

		/** @brief 로그 폴더를 만들고 비동기 백그라운드 I/O 작업 스레드를 시작합니다. */
		void initialize() override;
		/** @brief 큐를 모두 비우고 작업 스레드를 안전하게 종료합니다. */
		void shutdown() override;

		/** @brief 한 줄을 콘솔·파일·리스너에 남깁니다. */
		void writeLog( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line ) override;
		/** @brief 한 줄이 쓰일 때 호출할 리스너를 붙입니다. */
		DelegateHandle addLogWrittenListener( const LogWrittenDelegate& listener ) override;
		/** @brief 핸들로 리스너를 뗍니다. */
		void removeLogWrittenListener( const DelegateHandle& handle ) override;

		/** @brief 매크로가 쓸 전역 싱크를 바꿉니다. */
		static void setGlobalSink( ILogSink* pSink );
		/** @brief 전역 싱크로 한 줄을 남깁니다. 싱크가 없으면 무시합니다. */
		static void writeLogGlobal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line );
		/** @brief 전역 싱크에 리스너를 붙입니다. */
		static DelegateHandle addGlobalListener( const LogWrittenDelegate& listener );
		/** @brief 전역 싱크에서 리스너를 뗍니다. */
		static void removeGlobalListener( const DelegateHandle& handle );

		/** @brief 로그 파일이 있는 폴더 경로입니다. */
		const string& getLogFolderPath() override;
		/** @brief 현재 전역 싱크입니다. 없으면 nullptr. */
		static ILogSink* getGlobalSink();

	private:
		/** @brief 폴더·파일명을 준비하고 첫 파일을 엽니다. */
		void initializeInternal();
		/** @brief 백그라운드 I/O 작업자 루프입니다. */
		void workerLoop();
		/** @brief 큐에 남은 로그를 모두 비우고 기록합니다. */
		void flushQueue();
		/** @brief 타임스탬프를 붙여 큐에 넣거나 즉시 씁니다. */
		void writeLogInternal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line );
		/** @brief 레벨 색으로 콘솔에 한 줄을 씁니다. */
		void writeLogConsole( LogLevel level, const utf8* pMessage );
		/** @brief 시각이 바뀌면 파일을 갈아 끼운 뒤 한 줄을 씁니다. */
		void writeLogFile( LogLevel level, int32 year, int32 month, int32 day, int32 hour, const utf8* pFormattedBuffer );

		string							 _logFolderPath;
		string							 _currentLogFileName;
		LogWrittenMulticast				 _onLogWritten;
		ConcurrentQueue<LogRecord, 4096> _queue;
		std::thread						 _workerThread;
		std::condition_variable_any		 _cv;
		mutex							 _mutex;	 ///< 파일 쓰기 및 리스너 호출 동기화용 메인 뮤텍스
		mutex							 _cvMutex;	 ///< 조건 변수 대기용 뮤텍스
		mutex							 _timeMutex; ///< 타임스탬프 계산 및 문자열 캐시 동기화용 뮤텍스
		std::FILE*						 _pFile;
		void*							 _pCachedConsoleHandle; ///< GetStdHandle(STD_OUTPUT_HANDLE) 캐시
		std::time_t						 _cachedTimeSec;		///< 초 단위 캐시된 시스템 시간
		int32							 _cachedYear;
		int32							 _cachedMonth;
		int32							 _cachedDay;
		int32							 _cachedHour;
		int32							 _lastLogHour;			   ///< 시간별 로그 파일 롤오버 감지용
		uint16							 _defaultConsoleAttribute; ///< 초기 콘솔 텍스트 색상 속성
		atomic<bool>					 _bIsRunning;
		bool							 _bInitialized;
		bool							 _bHasConsole;								///< 표준 출력 콘솔 유효성 여부
		utf8							 _arrCachedDateStr[constant::kMaxBuffer32]; ///< 캐시된 YYYY-M-D H:M: 포맷 날짜 문자열
	};

	[[maybe_unused]] static inline constexpr const ::utf8* swGetLogCaller( ... ) { return nullptr; }
} // namespace sw

[[maybe_unused]] static inline constexpr const ::utf8* swGetLogCaller( ... ) { return nullptr; }

// ------------------------------------------------------------------------------
// 4) SW_LOG_* — Debug 에서만 포맷·기록. Release 는 no-op
// ------------------------------------------------------------------------------

/**
 * @brief 현재 파일 또는 네임스페이스 스코프의 로그 Caller(클래스/시스템명)를 지정합니다.
 */
#define SW_LOG_CALLER( name ) \
	[[maybe_unused]] static inline constexpr const ::utf8* swGetLogCaller( ::int32 = 0 ) { return name; }

#if defined( SW_DEBUG )
	/**
	 * @brief 포맷 문자열(+인자)을 파싱한 뒤 Logger::writeLog 로 전달하는 코어 매크로
	 * @note 메시지 문자열을 __VA_ARGS__ 첫 인자로 받아, 가변 인자 생략(C++20) 확장을 쓰지 않습니다.
	 */
	#define SW_LOG_INTERNAL( level, ... )                                                                          \
		do                                                                                                         \
		{                                                                                                          \
			::utf8 arrBuffer[::sw::constant::kMaxBuffer8192];                                                      \
			::sw::formatstring( arrBuffer, ::sw::constant::kMaxBuffer8192, __VA_ARGS__ );                          \
			::sw::Logger::writeLogGlobal( level, SW_LOG_TAG, swGetLogCaller( 0 ), arrBuffer, __FILE__, __LINE__ ); \
		} while ( false )

	/** @brief Error 레벨로 포맷해 남깁니다. */
	#define SW_LOG_ERROR( ... ) SW_LOG_INTERNAL( sw::LogLevel::Error, __VA_ARGS__ )
	/** @brief Warning 레벨로 포맷해 남깁니다. */
	#define SW_LOG_WARNING( ... ) SW_LOG_INTERNAL( sw::LogLevel::Warning, __VA_ARGS__ )
	/** @brief Info 레벨로 포맷해 남깁니다. */
	#define SW_LOG_INFO( ... ) SW_LOG_INTERNAL( sw::LogLevel::Info, __VA_ARGS__ )
	/** @brief Trace 레벨로 포맷해 남깁니다. */
	#define SW_LOG_TRACE( ... ) SW_LOG_INTERNAL( sw::LogLevel::Trace, __VA_ARGS__ )

	/**
	 * @brief 조건 실패 시 메시지·식·파일·함수·라인을 Error로 남기고 디버그 브레이크
	 * @note Release에서는 no-op. 무조건 실패는 SW_LOG_ASSERT( false, ... )로 호출.
	 */
	#define SW_LOG_ASSERT( expr, ... )                                                           \
		do                                                                                       \
		{                                                                                        \
			if ( !( expr ) )                                                                     \
			{                                                                                    \
				utf8 _assertMsg[sw::constant::kMaxBuffer8192];                                   \
				sw::formatstring( _assertMsg, sw::constant::kMaxBuffer8192, __VA_ARGS__ );       \
				SW_LOG_INTERNAL( sw::LogLevel::Error,                                            \
								 "ASSERT failed\n"                                               \
								 "Expression : %#\n"                                             \
								 "Message    : %#\n"                                             \
								 "FileName   : %#\n"                                             \
								 "Function   : %#\n"                                             \
								 "Line       : %#",                                              \
								 #expr, _assertMsg, __FILE__, SW_FUNCTION_SIGNATURE, __LINE__ ); \
				SW_DEBUG_BREAK();                                                                \
			}                                                                                    \
		} while ( false )
#else
	#define SW_LOG_ERROR( ... )
	#define SW_LOG_WARNING( ... )
	#define SW_LOG_INFO( ... )
	#define SW_LOG_TRACE( ... )
	#define SW_LOG_ASSERT( expr, ... )
#endif
