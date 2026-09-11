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
#include "Core/Log/ILogOutput.h"
#include "Core/Log/LogTypes.h"
#include "Core/String/formatString.h"

#if !defined( SW_LOG_TAG )
    /** @brief 호출 모듈 태그. 모듈 헤드에서 재정의합니다. */
    #define SW_LOG_TAG "Engine"
#endif

namespace sw
{
    class FileLogOutput;

    // ------------------------------------------------------------------------------
    // 2) ILogSink — 매크로가 말을 거는 전역 파사드 (장치는 ILogOutput 이다)
    // ------------------------------------------------------------------------------
    /**
     * @class ILogSink
     * @brief 로깅 파사드 — 포맷·리스너·로그 폴더까지 책임지는 "로거 전체" 의 이음매
     * @details **출력 장치 인터페이스가 아닙니다.** 한 줄이 실제로 나가는 곳은 `ILogOutput`
     *          (`ConsoleLogOutput` · `FileLogOutput`)이고, 그쪽을 여러 개 물고 있는 것이 `Logger` 다.
     *          이 인터페이스는 **로거 전체를 감싸거나 갈아 끼우는** 자리다 — 테스트 프레임워크가
     *          로그를 가로채려고 이걸 구현해 기존 싱크를 감싼다(`TestFramework.h`).
     */
    class SW_API ILogSink
    {
    public:
        ILogSink() = default;
        /** @brief 파일 핸들과 리스너를 닫습니다. */
        virtual ~ILogSink()                        = default;
        ILogSink( const ILogSink& )                = default;
        ILogSink& operator=( const ILogSink& )     = default;
        ILogSink( ILogSink&& ) noexcept            = default;
        ILogSink& operator=( ILogSink&& ) noexcept = default;

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
     * @brief 기본 로깅 파사드 — 포맷·타임스탬프·리스너·비동기 큐를 맡고, **출력은 `ILogOutput` 에 넘깁니다.**
     * @details 예전에는 이 클래스가 콘솔 쓰기와 파일 롤오버까지 직접 했고, 뮤텍스 **하나**가 둘을 함께
     *          잠갔습니다 — 파일 I/O 가 느리면 콘솔도 멈췄습니다. 지금은 장치마다 제 락을 갖습니다.
     *          기본으로 콘솔·파일 출력을 하나씩 달고 시작하며, `addOutput` 으로 더 붙일 수 있습니다
     *          (에디터 패널·네트워크 등 — 그때 이 클래스를 고칠 일은 없습니다).
     */
    class SW_API Logger final : public ILogSink
    {
    public:
        /** @brief 파일 포인터와 플래그를 비운 상태로 둡니다. */
        Logger();
        /** @brief 열려 있는 로그 파일을 닫습니다. */
        virtual ~Logger() override;

        Logger( const Logger& )            = delete;
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
        /**
         * @brief 소스 파일 경로별 Caller 이름을 등록합니다. **던지지 않습니다.**
         * @details `SW_LOG_CALLER` 가 정적 초기화에서 부르므로 여기서 예외가 나오면 잡을 곳이
         *          없다(std::terminate). 내부는 고정 배열과 뮤텍스뿐이라 던질 것이 없고, 그
         *          사실을 타입으로 못박아 정적 초기화가 안전하다는 것을 계약으로 만든다.
         */
        static void registerCaller( string_view filePath, string_view callerName ) noexcept;
        /** @brief 소스 파일 경로에 매핑된 Caller 이름을 반환합니다. */
        static const utf8* getCaller( const utf8* pFile );

        /**
         * @brief 출력 장치를 하나 더 답니다. 이미 초기화된 뒤라면 즉시 `open` 합니다.
         * @param output 소유권을 가져갑니다. 널이면 무시합니다.
         */
        void addOutput( unique_ptr<ILogOutput> output );

        /** @brief 로그 파일이 있는 폴더 경로입니다 — 파일 출력에 물어 답합니다. */
        const string& getLogFolderPath() override;
        /** @brief 매크로가 쓸 전역 싱크를 바꿉니다. */
        static void setGlobalSink( ILogSink* pSink );
        /** @brief 전역 싱크로 한 줄을 남깁니다. 싱크가 없으면 무시합니다. */
        static void writeLogGlobal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line );
        /** @brief 전역 싱크에 리스너를 붙입니다. */
        static DelegateHandle addGlobalListener( const LogWrittenDelegate& listener );
        /** @brief 전역 싱크에서 리스너를 뗍니다. */
        static void removeGlobalListener( const DelegateHandle& handle );
        /** @brief 현재 전역 싱크입니다. 없으면 nullptr. */
        static ILogSink* getGlobalSink();

        /**
         * @brief 런타임 상세도를 정합니다 — 이 레벨보다 덜 심각한 줄은 버려집니다.
         * @details 컴파일 타임 상한(SW_LOG_COMPILED_VERBOSITY)이 "무엇을 남길 수 있나" 를 정하고,
         *          이 값이 "지금 무엇을 남길까" 를 정한다. 언리얼의 카테고리 기본 상세도와 같은 자리다.
         *          배포본에서 고객에게 `-logVerbosity=trace` 를 시켜 재현을 받는 것이 이 값의 용도다.
         */
        static void setRuntimeVerbosity( LogLevel level );
        /** @brief setRuntimeVerbosity 로 정한 값 (기본 Info). */
        static LogLevel getRuntimeVerbosity();
        /** @brief 이 레벨이 지금 기록되는가 — 포맷 비용을 치르기 전에 물어봅니다. */
        static bool shouldLog( LogLevel level ) { return static_cast<int32>( level ) <= static_cast<int32>( getRuntimeVerbosity() ); }

    private:
        /** @brief 백그라운드 I/O 작업자 루프입니다. */
        void workerLoop();
        /** @brief 큐에 남은 로그를 모두 비우고 기록합니다. */
        void flushQueue();
        /** @brief 타임스탬프를 붙여 큐에 넣거나 즉시 씁니다. */
        void writeLogInternal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line );
        /** @brief 달려 있는 모든 출력 장치에 한 줄을 넘깁니다. 장치마다 제 락을 갖습니다. */
        void dispatchToOutputs( const LogRecord& record );

        LogWrittenMulticast              _onLogWritten;
        vector<unique_ptr<ILogOutput>>   _listOutput;
        FileLogOutput*                   _pFileOutput; ///< _listOutput 이 소유. getLogFolderPath 용 비소유 포인터
        ConcurrentQueue<LogRecord, 4096> _queue;
        std::thread                      _workerThread;
        std::condition_variable_any      _cv;
        mutex                            _mutex;         ///< 리스너 목록 + 출력 목록 동기화용
        mutex                            _cvMutex;       ///< 조건 변수 대기용 뮤텍스
        mutex                            _timeMutex;     ///< 타임스탬프 계산 및 문자열 캐시 동기화용 뮤텍스
        std::time_t                      _cachedTimeSec; ///< 초 단위 캐시된 시스템 시간
        int32                            _cachedYear;
        int32                            _cachedMonth;
        int32                            _cachedDay;
        int32                            _cachedHour;
        atomic<bool>                     _bIsRunning;
        bool                             _bInitialized;
        utf8                             _arrCachedDateStr[constant::kMaxBuffer32]; ///< 캐시된 YYYY-M-D H:M: 포맷 날짜 문자열
    };
} // namespace sw

// ------------------------------------------------------------------------------
// 4) SW_LOG_* — **상세도 상한**으로 자릅니다 (빌드 구성으로 통째로 끄지 않습니다)
//
//    예전에는 SW_DEBUG 가 아니면 매크로 전체가 빈 껍데기였다. 그러면 배포본에서 문제가 났을 때
//    남는 것이 하나도 없어 덤프만으로 원인을 찾아야 한다 — 실제 서비스에서는 성립하지 않는다.
//    언리얼처럼 **카테고리 상세도**로 자른다: 상한을 넘는 호출만 컴파일에서 사라지고, 그 아래는
//    Shipping 에도 남는다. 관례대로 Warning 이상은 어떤 빌드에서도 살린다.
//
//    SW_LOG_COMPILED_VERBOSITY 는 LogLevel 의 순서(Error 0 → Trace 3)와 같은 숫자다.
// ------------------------------------------------------------------------------

#if !defined( SW_LOG_COMPILED_VERBOSITY )
    #if defined( SW_SHIPPING )
  /// @brief 배포본은 Warning 까지만 컴파일한다 — Info/Trace 는 호출 자체가 사라진다.
        #define SW_LOG_COMPILED_VERBOSITY 1
    #elif defined( SW_DEBUG )
        #define SW_LOG_COMPILED_VERBOSITY 3
    #else
  /// @brief 개발(Release) 빌드는 Info 까지. Trace 는 비용이 커서 뺀다.
        #define SW_LOG_COMPILED_VERBOSITY 2
    #endif
#endif

/// @brief 이 레벨이 이 빌드에 컴파일되어 있는가 (숫자는 LogLevel 순서와 같다).
#define SW_LOG_LEVEL_COMPILED( levelIndex ) ( ( levelIndex ) <= SW_LOG_COMPILED_VERBOSITY )

/**
 * @brief 현재 파일 또는 네임스페이스 스코프의 로그 Caller(클래스/시스템명)를 지정합니다.
 */
#define SW_LOG_CALLER( name )                                                                                       \
    namespace                                                                                                       \
    {                                                                                                               \
        [[maybe_unused]] static const bool SW_CONCAT( _s_logCallerRegistered_, __COUNTER__ ) = []() noexcept { \
			::sw::Logger::registerCaller( __FILE__, name );                                    \
			return true; }(); \
    }

/**
 * @brief 포맷 문자열(+인자)을 파싱한 뒤 Logger::writeLog 로 전달하는 코어 매크로
 * @details **런타임 상세도를 먼저 물어본다** — 버려질 줄은 8KB 버퍼 포맷 비용도 치르지 않는다.
 * @note 메시지 문자열을 __VA_ARGS__ 첫 인자로 받아, 가변 인자 생략(C++20) 확장을 쓰지 않습니다.
 */
#define SW_LOG_INTERNAL( level, ... )                                                                  \
    do                                                                                                 \
    {                                                                                                  \
        if ( ::sw::Logger::shouldLog( level ) )                                                        \
        {                                                                                              \
            ::utf8 arrBuffer[::sw::constant::kMaxBuffer8192];                                          \
            ::sw::formatstring( arrBuffer, ::sw::constant::kMaxBuffer8192, __VA_ARGS__ );              \
            ::sw::Logger::writeLogGlobal( level, SW_LOG_TAG, nullptr, arrBuffer, __FILE__, __LINE__ ); \
        }                                                                                              \
    } while ( false )

#if SW_LOG_LEVEL_COMPILED( 0 )
    /** @brief Error 레벨로 포맷해 남깁니다. 어떤 빌드에도 남습니다. */
    #define SW_LOG_ERROR( ... ) SW_LOG_INTERNAL( sw::LogLevel::Error, __VA_ARGS__ )
#else
    #define SW_LOG_ERROR( ... )
#endif

#if SW_LOG_LEVEL_COMPILED( 1 )
    /** @brief Warning 레벨로 포맷해 남깁니다. 배포본에도 남습니다. */
    #define SW_LOG_WARNING( ... ) SW_LOG_INTERNAL( sw::LogLevel::Warning, __VA_ARGS__ )
#else
    #define SW_LOG_WARNING( ... )
#endif

#if SW_LOG_LEVEL_COMPILED( 2 )
    /** @brief Info 레벨로 포맷해 남깁니다. 배포본에서는 호출이 사라집니다. */
    #define SW_LOG_INFO( ... ) SW_LOG_INTERNAL( sw::LogLevel::Info, __VA_ARGS__ )
#else
    #define SW_LOG_INFO( ... )
#endif

#if SW_LOG_LEVEL_COMPILED( 3 )
    /** @brief Trace 레벨로 포맷해 남깁니다. Debug 에만 컴파일됩니다. */
    #define SW_LOG_TRACE( ... ) SW_LOG_INTERNAL( sw::LogLevel::Trace, __VA_ARGS__ )
#else
    #define SW_LOG_TRACE( ... )
#endif

#if defined( SW_DEBUG )

    /**
     * @brief 조건 실패 시 메시지·식·파일·함수·라인을 Error로 남기고 디버그 브레이크
     * @note Debug 에서만 브레이크한다. 그 밖의 빌드는 아래에서 **로그만** 남긴다 —
     *       배포본에서 단언이 통째로 사라지면 무엇이 어긋났는지 알 길이 없다.
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
    /**
     * @brief Debug 가 아니면 **브레이크 없이 Error 로만** 남깁니다.
     * @details 예전에는 통째로 no-op 이었다. 배포본에서 계약이 깨진 순간을 놓치는 가장 큰 구멍이었다.
     */
    #define SW_LOG_ASSERT( expr, ... )                                                     \
        do                                                                                 \
        {                                                                                  \
            if ( !( expr ) )                                                               \
            {                                                                              \
                utf8 _assertMsg[sw::constant::kMaxBuffer8192];                             \
                sw::formatstring( _assertMsg, sw::constant::kMaxBuffer8192, __VA_ARGS__ ); \
                SW_LOG_ERROR( "ASSERT failed\n"                                            \
                              "Expression : %#\n"                                          \
                              "Message    : %#\n"                                          \
                              "FileName   : %#\n"                                          \
                              "Line       : %#",                                           \
                              #expr, _assertMsg, __FILE__, __LINE__ );                     \
            }                                                                              \
        } while ( false )
#endif
