/**
 * @file Logger.h
 * @brief 엔진 전체에서 쓰는 로깅 시스템입니다.
 *
 * 로그 태그(Engine / Editor / Game)는 호출하는 모듈의 컴파일 정의 SW_LOG_TAG 로 정해집니다. 전역 _target 을 덮어쓰지
 * 않으므로 모듈을 로드한 뒤에도 출처가 유지됩니다. 에디터 표시 등은 addLogWrittenListener 로 구독합니다.
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
#include "Core/Log/LogTypes.h"
#include "Core/String/formatString.h"

#if !defined( SW_LOG_TAG )
    /** @brief 호출하는 모듈의 태그입니다. 모듈 헤드에서 재정의합니다. */
    #define SW_LOG_TAG "Engine"
#endif

namespace sw
{
    class FileLogOutput;
    class ILogOutput;

    // ------------------------------------------------------------------------------
    // 2) ILogSink — 매크로가 말을 거는 전역 파사드(출력 장치는 ILogOutput 이다)
    // ------------------------------------------------------------------------------
    /**
     * @class ILogSink
     * @brief 로깅 파사드입니다. 포맷 · 리스너 · 로그 폴더까지 책임지는 "로거 전체" 의 연결 지점입니다.
     * @details **출력 장치 인터페이스가 아닙니다.** 한 줄이 실제로 나가는 곳은 `ILogOutput`(`ConsoleLogOutput` ·
     *          `FileLogOutput`)이고, 그것들을 여러 개 들고 있는 것이 `Logger` 입니다. 이 인터페이스는 **로거 전체를 감싸거나
     *          바꿔 끼우는** 곳입니다. 테스트 프레임워크가 로그를 가로채려고 이것을 구현해 기존 싱크를 감쌉니다(`TestFramework.h`).
     */
    class SW_API ILogSink
    {
    public:
        ILogSink() = default;
        /** @brief 가상 소멸자입니다. */
        virtual ~ILogSink()                        = default;
        ILogSink( const ILogSink& )                = default;
        ILogSink& operator=( const ILogSink& )     = default;
        ILogSink( ILogSink&& ) noexcept            = default;
        ILogSink& operator=( ILogSink&& ) noexcept = default;

        /** @brief 로그 폴더와 출력 장치를 엽니다. */
        virtual void initialize() = 0;
        /** @brief 출력 장치와 리스너를 닫습니다. */
        virtual void shutdown() = 0;
        /** @brief 한 줄을 출력 장치와 리스너에 남깁니다. */
        virtual void writeLog( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line ) = 0;
        /** @brief 한 줄이 쓰일 때 호출할 리스너를 붙입니다. */
        virtual DelegateHandle addLogWrittenListener( const LogWrittenDelegate& listener ) = 0;
        /** @brief 핸들로 리스너를 뗍니다. */
        virtual void removeLogWrittenListener( const DelegateHandle& handle ) = 0;
        /** @brief 로그 파일이 있는 폴더의 경로입니다. */
        virtual const string& getLogFolderPath() = 0;
    };

    // ------------------------------------------------------------------------------
    // 3) Logger — 기본 싱크. 전역 호출은 setGlobalSink 뒤 writeLogGlobal 로 한다
    // ------------------------------------------------------------------------------
    /**
     * @class Logger
     * @brief 기본 로깅 파사드입니다. 포맷 · 타임스탬프 · 리스너 · 비동기 큐를 맡고, **출력은 `ILogOutput` 에 넘깁니다.**
     * @details 예전에는 이 클래스가 콘솔 쓰기와 파일 교체까지 직접 했고, 뮤텍스 **하나**가 둘을 함께 잠갔습니다. 그래서 파일
     *          I/O 가 느리면 콘솔도 멈췄습니다. 지금은 장치마다 자기 락을 가집니다. 기본으로 콘솔 · 파일 출력을 하나씩 달고
     *          시작하며, `addOutput` 으로 더 붙일 수 있습니다(에디터 패널 · 네트워크 등. 그때도 이 클래스를 고칠 필요는 없습니다).
     */
    class SW_API Logger final : public ILogSink
    {
    public:
        /** @brief 기본 출력 장치(콘솔 · 파일)를 하나씩 달고, 전역 싱크가 비어 있으면 자신을 등록합니다. */
        Logger();
        /** @brief 전역 싱크가 자신이면 등록을 해제합니다. 출력 장치를 닫는 일은 shutdown 이 합니다. */
        virtual ~Logger() override;

        Logger( const Logger& )            = delete;
        Logger& operator=( const Logger& ) = delete;

        /** @brief 출력 장치를 열고(로그 폴더 생성 포함) 비동기 I/O 작업 스레드를 시작합니다. */
        void initialize() override;
        /** @brief 큐를 모두 비우고 작업 스레드를 안전하게 끝냅니다. */
        void shutdown() override;

        /** @brief 한 줄을 포맷해 리스너와 출력 장치에 넘깁니다. */
        void writeLog( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line ) override;
        /** @brief 한 줄이 쓰일 때 호출할 리스너를 붙입니다. */
        DelegateHandle addLogWrittenListener( const LogWrittenDelegate& listener ) override;
        /** @brief 핸들로 리스너를 뗍니다. */
        void removeLogWrittenListener( const DelegateHandle& handle ) override;
        /**
         * @brief 소스 파일 경로별 Caller 이름을 등록합니다. **예외를 던지지 않습니다.**
         * @details `SW_LOG_CALLER` 가 정적 초기화 중에 부르므로 여기서 예외가 나면 잡을 곳이 없습니다(std::terminate). 내부는
         *          고정 배열과 뮤텍스뿐이라 던질 것이 없고, 그 사실을 타입(noexcept)으로 못 박아 정적 초기화가 안전하다는 것을
         *          계약으로 만듭니다.
         */
        static void registerCaller( string_view filePath, string_view callerName ) noexcept;
        /** @brief 소스 파일 경로에 등록된 Caller 이름을 반환합니다. */
        static const utf8* getCaller( const utf8* pFile );

        /**
         * @brief 출력 장치를 하나 더 답니다. 이미 초기화된 뒤라면 바로 `open` 합니다.
         * @param output 소유권을 가져갑니다. nullptr 이면 무시합니다.
         * @return 실제로 달았으면 true. **상한(`_s_kMaxOutput`)을 넘으면 false** 이고, 그때 `output` 은 그대로 파괴됩니다.
         * @warning 예전에는 반환값이 없었고, 상한을 넘겨도 받아서 `open` 까지 해 놓은 뒤 디스패치에서 **말없이 빠뜨렸습니다**
         *          (고정 배열이 8개에서 잘렸습니다). 붙인 쪽에서는 보이지 않는 실패입니다. 그래서 거절하되 **거절했다고 알립니다.**
         *          조용히 무시하든 조용히 파괴하든 호출하는 쪽에게는 똑같기 때문입니다.
         */
        bool addOutput( unique_ptr<ILogOutput> output );

        /** @brief 로그 파일이 있는 폴더의 경로입니다. 파일 출력 장치에 물어 답합니다. */
        const string& getLogFolderPath() override;
        /** @brief 매크로가 쓸 전역 싱크를 바꿉니다. */
        static void setGlobalSink( ILogSink* pSink );
        /** @brief 전역 싱크로 한 줄을 남깁니다. 싱크가 없으면 무시합니다. */
        static void writeLogGlobal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line );
        /** @brief 전역 싱크에 리스너를 붙입니다. */
        static DelegateHandle addGlobalListener( const LogWrittenDelegate& listener );
        /** @brief 전역 싱크에서 리스너를 뗍니다. */
        static void removeGlobalListener( const DelegateHandle& handle );
        /** @brief 현재 전역 싱크입니다. 없으면 nullptr 입니다. */
        static ILogSink* getGlobalSink();

        /**
         * @brief 런타임 상세도를 정합니다. 이 수준보다 덜 심각한 줄은 버려집니다.
         * @details 컴파일 타임 상한(SW_LOG_COMPILED_VERBOSITY)이 "무엇을 남길 수 있나" 를 정하고, 이 값이 "지금 무엇을 남길까"
         *          를 정합니다. 언리얼의 카테고리 기본 상세도와 같은 역할입니다. 배포본에서 사용자에게 `-logVerbosity=trace` 로
         *          실행해 달라고 해서 재현 로그를 받는 것이 이 값의 용도입니다.
         */
        static void setRuntimeVerbosity( LogLevel level );
        /** @brief setRuntimeVerbosity 로 정한 값입니다(기본값 Info). */
        static LogLevel getRuntimeVerbosity();
        /** @brief 이 수준이 지금 기록되는지 확인합니다. 포맷 비용을 치르기 전에 물어봅니다. */
        static bool shouldLog( LogLevel level ) { return static_cast<int32>( level ) <= static_cast<int32>( getRuntimeVerbosity() ); }

    private:
        /** @brief 백그라운드 I/O 작업 스레드의 루프입니다. */
        void workerLoop();
        /** @brief 큐에 남은 로그를 모두 비우고 기록합니다. */
        void flushQueue();
        /** @brief 타임스탬프를 붙여 큐에 넣거나 바로 씁니다. */
        void writeLogInternal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line );
        /** @brief 달려 있는 모든 출력 장치에 한 줄을 넘깁니다. 장치마다 자기 락을 가집니다. */
        void dispatchToOutputs( const LogRecord& record );

        /**
         * @brief 달 수 있는 출력 장치의 최대 개수입니다.
         * @details `dispatchToOutputs` 는 잠금 안에서 포인터만 고정 배열로 복사해 오고 쓰기는 **락 밖에서** 합니다(느린 파일 I/O
         *          가 콘솔을 막지 않도록). 그 배열의 크기가 곧 이 상한입니다. 예전에는 배열 리터럴 `8` 만 있고 `addOutput` 은
         *          그 사실을 몰랐습니다.
         */
        static constexpr uint32 _s_kMaxOutput = 8;

        LogWrittenMulticast              _onLogWritten;
        vector<unique_ptr<ILogOutput>>   _listOutput;
        FileLogOutput*                   _pFileOutput; ///< _listOutput 이 소유한다. getLogFolderPath 용 비소유 포인터
        ConcurrentQueue<LogRecord, 4096> _queue;
        std::thread                      _workerThread;
        std::condition_variable_any      _cv;
        mutex                            _mutex;         ///< 리스너 목록과 출력 목록을 보호한다
        mutex                            _cvMutex;       ///< 조건 변수 대기용 뮤텍스
        mutex                            _timeMutex;     ///< 타임스탬프 계산과 문자열 캐시를 보호한다
        std::time_t                      _cachedTimeSec; ///< 초 단위로 캐시한 시스템 시각
        int32                            _cachedYear;
        int32                            _cachedMonth;
        int32                            _cachedDay;
        int32                            _cachedHour;
        atomic<bool>                     _bIsRunning;
        bool                             _bInitialized;
        utf8                             _arrCachedDateStr[constant::kMaxBuffer32]; ///< 캐시한 날짜 문자열(YYYY-M-D H:M: 형식)
    };
} // namespace sw

// ------------------------------------------------------------------------------
// 4) SW_LOG_* — 빌드 구성으로 통째로 끄지 않고 **상세도 상한**으로 자른다
//
//    예전에는 SW_DEBUG 가 아니면 매크로 전체가 빈 껍데기였다. 그러면 배포본에서 문제가 생겼을 때 남는 것이 하나도
//    없어서 덤프만으로 원인을 찾아야 한다. 실제 서비스에서는 통하지 않는 방식이다. 그래서 언리얼처럼 **카테고리
//    상세도**로 자른다. 상한을 넘는 호출만 컴파일에서 사라지고, 그 아래는 Shipping 에도 남는다. 관례대로 Warning
//    이상은 어떤 빌드에서도 남긴다.
//
//    SW_LOG_COMPILED_VERBOSITY 는 LogLevel 의 순서와 같은 숫자다. 아래 SW_LOG_VERBOSITY_* 가 그 이름이고,
//    static_assert 가 열거자와 어긋나지 않게 지킨다. 전처리기 조건에는 열거자를 쓸 수 없어서 매크로가 필요하다.
// ------------------------------------------------------------------------------

/// @brief 수준 번호의 이름입니다. `SW_LOG_LEVEL_COMPILED( 2 )` 처럼 숫자로 적으면 읽는 사람이 2 가 무엇인지 알 수 없습니다.
#define SW_LOG_VERBOSITY_ERROR   0
#define SW_LOG_VERBOSITY_WARNING 1
#define SW_LOG_VERBOSITY_INFO    2
#define SW_LOG_VERBOSITY_TRACE   3
static_assert( static_cast<int32>( sw::LogLevel::Error ) == SW_LOG_VERBOSITY_ERROR, "SW_LOG_VERBOSITY_ERROR 가 LogLevel 과 어긋났다" );
static_assert( static_cast<int32>( sw::LogLevel::Warning ) == SW_LOG_VERBOSITY_WARNING, "SW_LOG_VERBOSITY_WARNING 이 LogLevel 과 어긋났다" );
static_assert( static_cast<int32>( sw::LogLevel::Info ) == SW_LOG_VERBOSITY_INFO, "SW_LOG_VERBOSITY_INFO 가 LogLevel 과 어긋났다" );
static_assert( static_cast<int32>( sw::LogLevel::Trace ) == SW_LOG_VERBOSITY_TRACE, "SW_LOG_VERBOSITY_TRACE 가 LogLevel 과 어긋났다" );

#if !defined( SW_LOG_COMPILED_VERBOSITY )
    #if defined( SW_SHIPPING )
    /// @brief 배포본은 Warning 까지만 컴파일합니다. Info · Trace 는 호출 자체가 사라집니다.
        #define SW_LOG_COMPILED_VERBOSITY SW_LOG_VERBOSITY_WARNING
    #elif defined( SW_DEBUG )
        #define SW_LOG_COMPILED_VERBOSITY SW_LOG_VERBOSITY_TRACE
    #else
    /// @brief 개발(Release) 빌드는 Info 까지입니다. Trace 는 비용이 커서 뺍니다.
        #define SW_LOG_COMPILED_VERBOSITY SW_LOG_VERBOSITY_INFO
    #endif
#endif

/// @brief 이 수준이 이 빌드에 컴파일되는지 확인합니다. 인자는 SW_LOG_VERBOSITY_* 로 적습니다.
#define SW_LOG_LEVEL_COMPILED( verbosity ) ( ( verbosity ) <= SW_LOG_COMPILED_VERBOSITY )

/**
 * @brief 현재 파일이나 네임스페이스 스코프의 로그 Caller(클래스 · 시스템 이름)를 지정합니다.
 */
#define SW_LOG_CALLER( name )                                                                                       \
    namespace                                                                                                       \
    {                                                                                                               \
        [[maybe_unused]] static const bool SW_CONCAT( _s_logCallerRegistered_, __COUNTER__ ) = []() noexcept { \
			::sw::Logger::registerCaller( __FILE__, name );                                    \
			return true; }(); \
    }

/**
 * @brief 포맷 문자열(과 인자)을 포맷해 Logger::writeLog 로 넘기는 핵심 매크로입니다.
 * @details **런타임 상세도를 먼저 확인합니다.** 버려질 줄은 8KB 버퍼 포맷 비용도 치르지 않습니다.
 * @note 메시지 문자열을 __VA_ARGS__ 의 첫 인자로 받으므로, 가변 인자 생략(C++20) 확장을 쓰지 않습니다.
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

#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_ERROR )
    /** @brief Error 수준으로 포맷해 남깁니다. 모든 빌드에 남습니다. */
    #define SW_LOG_ERROR( ... ) SW_LOG_INTERNAL( sw::LogLevel::Error, __VA_ARGS__ )
#else
    #define SW_LOG_ERROR( ... )
#endif

#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_WARNING )
    /** @brief Warning 수준으로 포맷해 남깁니다. 배포본에도 남습니다. */
    #define SW_LOG_WARNING( ... ) SW_LOG_INTERNAL( sw::LogLevel::Warning, __VA_ARGS__ )
#else
    #define SW_LOG_WARNING( ... )
#endif

#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
    /** @brief Info 수준으로 포맷해 남깁니다. 배포본에서는 호출이 사라집니다. */
    #define SW_LOG_INFO( ... ) SW_LOG_INTERNAL( sw::LogLevel::Info, __VA_ARGS__ )
#else
    #define SW_LOG_INFO( ... )
#endif

#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_TRACE )
    /** @brief Trace 수준으로 포맷해 남깁니다. Debug 에만 컴파일됩니다. */
    #define SW_LOG_TRACE( ... ) SW_LOG_INTERNAL( sw::LogLevel::Trace, __VA_ARGS__ )
#else
    #define SW_LOG_TRACE( ... )
#endif

#if defined( SW_DEBUG )

    /**
     * @brief 조건이 거짓이면 메시지 · 식 · 파일 · 함수 · 줄을 Error 로 남기고 디버거에서 멈춥니다.
     * @note Debug 에서만 멈춥니다. 그 밖의 빌드는 아래에서 **로그만** 남깁니다. 배포본에서 단언이 통째로 사라지면 무엇이
     *       어긋났는지 알 길이 없기 때문입니다.
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
     * @brief Debug 가 아니면 **멈추지 않고 Error 로그만** 남깁니다.
     * @details 예전에는 통째로 no-op 이었습니다. 배포본에서 계약이 깨진 순간을 놓치는 가장 큰 구멍이었습니다.
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
