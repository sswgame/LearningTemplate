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
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/ConcurrentQueue.h"
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
        /** @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 리스너를 모두 떼고, 뗀 수를 반환합니다(핫 리로드가 모듈을 내리기 전에 부른다). */
        virtual uint32 releaseListenerCodeWithin( const void* pBegin, const void* pEnd ) = 0;
        /** @brief 로그 파일이 있는 폴더의 경로입니다. */
        virtual const string& getLogFolderPath() = 0;
        /**
         * @brief 크래시 경로에서 부릅니다: 큐에 남은 줄과 장치 버퍼를 **지금 이 스레드에서** 내보냅니다. 락을 바로 잡지 못하면 포기합니다.
         * @details 감싸는 싱크(테스트 프레임워크)는 감싼 싱크로 넘깁니다. 비동기 큐가 없는 싱크는 아무것도 하지 않아도 됩니다.
         */
        virtual void flushForCrash() {}
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) Logger — 기본 싱크. 전역 호출은 setGlobalSink 뒤 writeLogGlobal 로 한다
    // ------------------------------------------------------------------------------
    /**
     * @class Logger
     * @brief 기본 로깅 파사드입니다. 포맷 · 타임스탬프 · 리스너 · 비동기 큐를 맡고, **출력은 `ILogOutput` 에 넘깁니다.**
     * @details 장치마다 자기 락을 가지므로 파일 I/O 가 느려도 콘솔이 멈추지 않습니다. 기본으로 콘솔 · 파일 출력을 하나씩 달고
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
        /** @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 리스너를 모두 뗍니다. */
        uint32 releaseListenerCodeWithin( const void* pBegin, const void* pEnd ) override;
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
         * @warning 상한을 넘기면 받아서 `open` 해 놓고 디스패치에서 말없이 빠뜨리는 대신, 거절하고 **거절했다고 알립니다.**
         *          조용히 무시하든 조용히 파괴하든 호출하는 쪽에게는 똑같이 보이지 않는 실패이기 때문입니다.
         */
        bool addOutput( unique_ptr<ILogOutput> output );

        /** @brief 로그 파일이 있는 폴더의 경로입니다. 파일 출력 장치에 물어 답합니다. */
        const string& getLogFolderPath() override;
        /**
         * @brief 큐에 남은 줄을 이 스레드에서 모두 쓰고 장치 버퍼를 내보냅니다. 로거 락을 바로 잡지 못하면 포기합니다(기다리면 크래시가 멈춤이 된다).
         * @details 크래시 리포트 · "로그 + std::abort" 치명 경로가 부릅니다 — 큐에 넣기만 하고 프로세스가 끝나면 크래시 직전의 경고 · 정보
         *          줄과 파일의 stdio 버퍼가 사라집니다.
         */
        void flushForCrash() override;
        /** @brief 전역 싱크의 `flushForCrash` 입니다. 크래시 핸들러가 리포트를 쓴 뒤 부릅니다. */
        static void flushGlobalForCrash();
        /** @brief 매크로가 쓸 전역 싱크를 바꿉니다. */
        static void setGlobalSink( ILogSink* pSink );
        /** @brief 전역 싱크로 한 줄을 남깁니다. 싱크가 없으면 무시합니다. */
        static void writeLogGlobal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line );
        /** @brief 전역 싱크에 리스너를 붙입니다. */
        static DelegateHandle addGlobalListener( const LogWrittenDelegate& listener );
        /** @brief 전역 싱크에서 리스너를 뗍니다. */
        static void removeGlobalListener( const DelegateHandle& handle );
        /** @brief 전역 싱크에서 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 리스너를 모두 떼고, 뗀 수를 반환합니다. 싱크가 없으면 0 입니다. */
        static uint32 releaseGlobalListenerCodeWithin( const void* pBegin, const void* pEnd );
        /** @brief 현재 전역 싱크입니다. 없으면 nullptr 입니다. */
        static ILogSink* getGlobalSink();

        /**
         * @brief 런타임 상세도를 정합니다. 이 수준보다 덜 심각한 줄은 버려집니다.
         * @details 컴파일 타임 상한(SW_LOG_COMPILED_VERBOSITY)이 "무엇을 남길 수 있나" 를 정하고, 이 값이 "지금 무엇을 남길까"
         *          를 정합니다. 언리얼의 카테고리 기본 상세도와 같은 역할입니다. 명령줄 인자 · 전역 변수로는 바꿀 수
         *          없고 코드에서 부릅니다.
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
         * @brief 리스너를 뗀 뒤, 떼기 **전에** 시작한 방송이 모두 끝날 때까지 기다립니다.
         * @details 방송은 리스너 목록을 복사해 락 밖에서 부릅니다. 기다리지 않으면 떼기가 돌아온 뒤에도 다른 스레드의 방송이 뗀 리스너를
         *          부를 수 있습니다(에디터 콘솔 패널이 파괴되거나 모듈이 내려간 뒤에 그 코드로 들어감). 이 스레드가 방송 도중(리스너 안)이면
         *          자기 자신을 기다리게 되므로 기다리지 않습니다. 끊임없는 로그로 세대가 비지 않으면 2 초 뒤 경고하고 돌아옵니다.
         * @param retiredSlot 떼기 직전 세대 칸(_mutex 안에서 읽은 값)
         */
        void waitForRetiredBroadcasts( uint32 retiredSlot );
        /** @brief 세대를 넘기고 넘기기 전 칸을 돌려줍니다. _mutex 를 쥐고 부릅니다. */
        uint32 retireBroadcastSlot();

        /**
         * @brief 달 수 있는 출력 장치의 최대 개수입니다.
         * @details `dispatchToOutputs` 는 잠금 안에서 포인터만 고정 배열로 복사해 오고 쓰기는 **락 밖에서** 합니다(느린 파일 I/O
         *          가 콘솔을 막지 않도록). 그 배열의 크기가 곧 이 상한이고, `addOutput` 도 이 값으로 거절합니다.
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
        atomic<uint32>                   _arrBroadcastInFlight[2]; ///< 리스너를 부르는 중인 방송 수(세대 두 칸). 떼기가 이전 세대가 빌 때까지 기다린다
        uint32                           _broadcastEpoch;          ///< 방송 세대. _mutex 로 보호한다
        atomic<bool>                     _bIsRunning;
        atomic<bool>                     _bInitialized;                             ///< 쓰는 스레드는 락 없이 읽는다(초기화 · 종료와 경쟁하지 않게 원자값)
        utf8                             _arrCachedDateStr[constant::kMaxBuffer32]; ///< 캐시한 날짜 문자열(YYYY-M-D H:M: 형식)
    };
} // namespace sw

// ------------------------------------------------------------------------------
// 4) SW_LOG_* — 빌드 구성으로 통째로 끄지 않고 **상세도 상한**으로 자른다
//
//    SW_DEBUG 가 아니면 매크로 전체를 빈 껍데기로 만들면 배포본에서 문제가 생겼을 때 남는 것이 하나도
//    없어서 덤프만으로 원인을 찾아야 한다. 그래서 언리얼처럼 **카테고리 상세도**로 자른다. 상한을 넘는 호출만 컴파일에서 사라지고, 그 아래는 Shipping 에도 남는다. 관례대로 Warning
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
    /// @brief 배포본은 Warning 까지만 컴파일합니다. Info · Trace 는 호출 자체가 사라집니다. 전용 서버 배포본은 Info 까지다(`cmake/Engine/BuildLayout.cmake` 가 정의).
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
     * @note 시험이 단언 가로채기(`test::ScopedAssertCapture`)를 걸어 두었으면 멈추지 않고 세기만 합니다.
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
                if ( ::sw::internal::tryCaptureAssert() == false )                               \
                    SW_DEBUG_BREAK();                                                            \
            }                                                                                    \
        } while ( false )
#else
    /**
     * @brief Debug 가 아니면 **멈추지 않고 Error 로그만** 남깁니다.
     * @details no-op 으로 두지 않습니다 — 배포본에서 계약이 깨진 순간을 놓치게 됩니다.
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
