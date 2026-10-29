/**
 * @file Logger.h
 * @brief 엔진 전체에서 쓰는 로깅 시스템입니다.
 *
 * 로그 태그(Engine / Editor / Game)는 호출하는 모듈의 컴파일 정의 SW_LOG_TAG 로 정해집니다. 전역 _target 을 덮어쓰지
 * 않으므로 모듈을 로드한 뒤에도 출처가 유지됩니다. 에디터 표시 등은 registerLogWrittenListener 로 구독합니다.
 *
 * **줄의 종류 id.** 테스트가 로그 문장을 부분 문자열로 맞추면 문구를 고칠 때마다 지므로(그래서 `countContaining` 을 없앴다), 테스트가 찾을 줄에는 `SW_LOG_ERROR_ID` · `SW_LOG_WARNING_ID` 로
 * 글자 상수 id 를 붙인다(`LogEntry::_pID`, 테스트는 `ScopedLogCollector::countWithID`). 감싸는 싱크(`ILogSink::writeLogWithID`)는 id 를 그대로 넘겨야 한다.
 */
#pragma once
#include "Core/Container/formatString.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogTypes.h"

#if !defined( SW_LOG_TAG )
    /** @brief 호출하는 모듈의 태그입니다. 모듈 헤드에서 재정의합니다. */
    #define SW_LOG_TAG "Engine"
#endif

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) ILogSink. 매크로가 호출하는 전역 파사드이며, 출력 장치는 ILogOutput 이다.
    // ------------------------------------------------------------------------------
    /**
     * @class ILogSink
     * @brief 로깅 파사드입니다. 포맷, 리스너, 로그 폴더까지 책임지는 "로거 전체"의 연결 지점입니다.
     * @details **출력 장치 인터페이스가 아닙니다.** 한 줄이 실제로 나가는 곳은 `ILogOutput`(`ConsoleLogOutput` ·
     *          `FileLogOutput`)이고, 그것들을 여러 개 들고 있는 것이 기본 싱크 `AsyncLogSink` 입니다. 이 인터페이스는 **로거 전체를 감싸거나
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
        /**
         * @brief 한 줄을 출력 장치와 리스너에 남깁니다. @p pID 는 줄의 종류 id 입니다(`SW_LOG_ERROR_ID` · `LogEntry::_pID`) — 정적 글자여야 하고 없으면 nullptr 입니다.
         * @details 감싸는 싱크는 @p pID 를 감싼 싱크로 그대로 넘깁니다. 넘기지 않으면 테스트가 id 로 줄을 찾지 못한다.
         */
        virtual void writeLogWithID( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID ) = 0;
        /** @brief id 가 없는 한 줄입니다(`writeLogWithID` 에 nullptr 를 넘긴다). */
        void writeLog( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line )
        {
            writeLogWithID( level, pTag, pCaller, pMessage, pFile, line, nullptr );
        }
        /**
         * @brief 한 줄이 쓰일 때 호출할 리스너를 붙이고 그 구독을 돌려줍니다. 구독이 사라지면 뗍니다(진행 중인 방송이 끝나기를 기다린다).
         * @details 감싸는 싱크는 감싼 싱크의 구독을 그대로 돌려줍니다. 리스너를 받지 않는 싱크는 빈 구독을 돌려줍니다.
         */
        [[nodiscard]] virtual Subscription registerLogWrittenListener( const LogWrittenDelegate& listener ) = 0;
        /** @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 리스너를 모두 해제하고, 해제한 수를 반환합니다(핫 리로드가 모듈을 언로드하기 전에 부릅니다). */
        virtual uint32 removeListenerCodeWithin( const void* pBegin, const void* pEnd ) = 0;
        /** @brief 로그 파일이 있는 폴더의 경로를 반환합니다. */
        virtual string_view getLogFolderPath() = 0;
        /**
         * @brief 이 프로세스가 지금 쓰는 로그 파일의 경로입니다. 아직 한 줄도 쓰지 않았거나 파일을 쓰지 않는 싱크면 빈 글입니다.
         * @details 폴더에서 "가장 새 파일" 을 고르면 같은 폴더를 쓰는 다른 프로세스(병렬 실행)의 로그를 집는다 — 이 프로세스의 것은 싱크만 안다.
         */
        virtual string getLogFilePath() = 0;
        /**
         * @brief 크래시 경로에서 부릅니다: 큐에 남은 줄과 장치 버퍼를 **지금 이 스레드에서** 내보냅니다. 락을 바로 잡지 못하면 포기합니다.
         * @details 감싸는 싱크(테스트 프레임워크)는 감싼 싱크로 넘깁니다. 비동기 큐가 없는 싱크는 아무것도 하지 않아도 됩니다.
         */
        virtual void flushForCrash() {}
        /**
         * @brief 줄마다 JSON 한 줄을 표준 출력에 쓰는 장치(`JSONLinesLogOutput`)를 달고 콘솔 장치를 끕니다(컨테이너 · 로그 수집기용, 운영 설정 `_bJSONLogs`). 두 번 불러도 한 번입니다.
         * @return 켰으면 true. 장치를 달 수 없는 싱크(감싸는 싱크 · 비동기 큐가 없는 싱크)는 기본 구현이 false 를 돌려줍니다 — 부른 쪽이 알려야 합니다.
         * @details 출력 장치 형식(`ILogOutput`)이 이 헤더보다 위 층에 있어 장치를 인자로 받지 않고, 켜는 일만 싱크에 맡깁니다.
         */
        virtual bool enableJSONLinesOutput() { return false; }
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) Logger. 매크로가 부르는 전역 진입점이며 전역 싱크, 상세도, 호출자 이름 테이블을 가진다. 인스턴스는 없다.
    //    기본 싱크(비동기 큐와 출력 장치)는 `AsyncLogSink`(Core/Log/Sink)이고, 생성되면 전역 싱크가 비어 있을 때 자신을 등록한다.
    // ------------------------------------------------------------------------------
    /**
     * @class Logger
     * @brief 로그 매크로가 호출하는 전역 진입점입니다. 한 줄은 전역 싱크(`ILogSink`)에 넘기고, 싱크가 없으면 버립니다.
     * @details 이 진입점은 파일, 크래시, 모듈을 모르므로 Core 의 아래 계층(Container, Memory, String 등)이 로그를 쓸 수 있습니다.
     *          포맷, 타임스탬프, 리스너, 비동기 큐, 출력 장치는 싱크(`AsyncLogSink`)가 맡습니다.
     */
    class SW_API Logger final
    {
    public:
        Logger() = delete;

        /**
         * @brief 소스 파일 경로별 Caller 이름을 등록합니다. **예외를 던지지 않습니다.**
         * @details `SW_LOG_CALLER` 가 정적 초기화 중에 부르므로 여기서 예외가 나면 잡을 곳이 없습니다(std::terminate). 내부는
         *          고정 배열과 뮤텍스뿐이라 던질 것이 없고, 그 사실을 타입(noexcept)으로 못 박아 정적 초기화가 안전하다는 것을
         *          계약으로 만듭니다.
         */
        static void registerCaller( string_view filePath, string_view callerName ) noexcept;
        /** @brief 소스 파일 경로에 등록된 Caller 이름을 반환합니다. */
        static const utf8* getCaller( const utf8* pFile );

        /** @brief 전역 싱크의 `flushForCrash` 입니다. 크래시 핸들러가 리포트를 쓴 뒤 부릅니다. */
        static void flushGlobalForCrash();
        /** @brief 매크로가 쓸 전역 싱크를 바꿉니다. */
        static void setGlobalSink( ILogSink* pSink );
        /** @brief 전역 싱크가 비어 있으면 @p pSink 를 등록합니다(기본 싱크가 만들어질 때 부릅니다). */
        static void registerGlobalSink( ILogSink* pSink );
        /** @brief 전역 싱크가 @p pSink 이면 해제합니다(기본 싱크가 파괴될 때 부릅니다). */
        static void unregisterGlobalSink( ILogSink* pSink );
        /**
         * @brief 전역 싱크로 한 줄을 남깁니다. 싱크가 없으면 줄을 버리지 않고 stderr 에 쓰고 비웁니다(기동이 이르거나 로거가 없는 도구에서도 줄이 남는다).
         */
        static void writeLogGlobal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID = nullptr );
        /**
         * @brief `SW_LOG_ASSERT`(Debug)가 멈추기 전에 식 · 메시지 · 위치를 stderr 에 동기로 쓰고 비웁니다.
         * @details 로거는 비동기라 멈추면 큐의 메시지를 잃을 수 있습니다(`SW_ASSERT` 의 `printAssertFailure` 와 같은 이유). 매크로가 이 함수를 거치지 않고는 멈출 수 없게 한 곳에 둡니다.
         */
        static void printAssertFailureToStderr( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pFunction ) noexcept;
        /**
         * @brief 전역 싱크에 리스너를 붙이고 그 구독을 돌려줍니다. 싱크가 없으면 순서 위반이라 `SW_LOW_LEVEL_ASSERT` 로 보고하고(Debug 는 멈춘다) 빈 구독을 돌려줍니다.
         * @details 구독은 붙인 싱크를 가리킨다 — 그 뒤 전역 싱크가 바뀌어도 붙인 곳에서 떼고, 싱크가 먼저 사라지면 무효가 된다(종료 순서에서 정상).
         *          구독을 풀면 진행 중인 방송이 끝날 때까지 기다린다(최대 2 초). 그래서 리스너 콜백이 얻는 락을 **쥔 채** 풀지 않는다 — 콜백이 그 락을 기다리면
         *          2 초 뒤 stderr 경고로 풀린다. 리스너 콜백 안에서 푸는 것은 기다리지 않는다(자기 방송을 기다리지 않게 막아 두었다).
         */
        [[nodiscard]] static Subscription registerGlobalListener( const LogWrittenDelegate& listener );
        /** @brief 전역 싱크에서 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 리스너를 모두 해제하고, 해제한 수를 반환합니다. 싱크가 없으면 0 입니다. */
        static uint32 releaseGlobalListenerCodeWithin( const void* pBegin, const void* pEnd );
        /** @brief 현재 전역 싱크를 반환합니다. 없으면 nullptr 입니다. */
        static ILogSink* getGlobalSink();

        /**
         * @brief 런타임 상세도를 정합니다. 이 수준보다 덜 심각한 줄은 버려집니다.
         * @details 컴파일 타임 상한(SW_LOG_COMPILED_VERBOSITY)이 "무엇을 남길 수 있나"를 정하고, 이 값이 "지금 무엇을 남길까"
         *          를 정합니다. 언리얼의 카테고리 기본 상세도와 같은 역할입니다. 명령줄 인자나 전역 변수로는 바꿀 수
         *          없고 코드에서 호출합니다.
         */
        static void setRuntimeVerbosity( LogLevel level );
        /** @brief setRuntimeVerbosity 로 정한 값입니다(기본값 Info). */
        static LogLevel getRuntimeVerbosity();
        /** @brief 이 수준이 지금 기록되는지 확인합니다. 포맷 비용을 치르기 전에 물어봅니다. */
        static bool shouldLog( LogLevel level ) { return static_cast<int32>( level ) <= static_cast<int32>( getRuntimeVerbosity() ); }

        /**
         * @brief 이후 모든 줄(모든 스레드)에 시계 태그(`[tick=N] ` 또는 `[frame=N] `)를 붙입니다. 호스트가 틱 · 프레임마다 부릅니다.
         * @details 언리얼 로그 줄의 프레임 번호(`[123]LogNet:`)에 해당합니다. 서버와 클라이언트 로그를 모아 볼 때 한 사건이 몇 번째 틱인지 알 수 있습니다.
         *          고정 틱을 도는 전용 서버는 `LogClock::Tick`, 고정 틱이 없는 리슨 호스트(App)는 `LogClock::Frame` 이다 — 한 이름으로 적으면 두 번호를
         *          같은 간격으로 읽게 된다. 종류와 번호는 한 원자 값이라 다른 스레드가 둘을 섞어 읽지 않습니다.
         *          프로세스 전역 값이라 틱 밖의 스레드(네트워크, 로더) 줄에도 지금 번호가 붙습니다. 정하지 않으면(기본) 줄은 바이트 하나 다르지 않습니다.
         */
        static void setClockNumber( LogClock clock, uint64 number );
        /** @brief 시계 태그를 해제합니다(기본 상태). */
        static void clearClockNumber();
        /** @brief 현재 시계 종류와 번호를 반환합니다. 정하지 않았으면 false 를 반환하고 @p outClock 은 Tick, @p outNumber 는 0 입니다. */
        [[nodiscard]] static bool getClockNumber( LogClock& outClock, uint64& outNumber );
    };
} // namespace sw

// ------------------------------------------------------------------------------
// 4) SW_LOG_*. 빌드 구성으로 통째로 끄지 않고 **상세도 상한**으로 자른다.
//
//    SW_DEBUG 가 아닐 때 매크로 전체를 빈 껍데기로 만들면 배포본에서 문제가 생겼을 때 남는 것이 하나도
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

namespace sw
{
    /**
     * @brief 컴파일에서 빠진 로그 수준의 자리가 인자 이름을 "쓴 것"으로 세게 하는 선언입니다.
     * @details 로그에만 쓰는 지역 변수 · 매개변수는 그 수준이 빠진 빌드(배포본의 Info · Trace)에서 매크로가 통째로 사라지면 미사용 경고가 됩니다.
     *          `sizeof` 안은 평가되지 않으므로 부작용 · 비용은 없고, 이름만 쓴 것으로 세어집니다. 정의가 없습니다(평가되면 링크 오류).
     */
    struct LogDiscardUtil
    {
        template <typename... TArgs>
        static int32 consume( const TArgs&... args );
    };
} // namespace sw

/// @brief 컴파일에서 빠진 SW_LOG_* 의 본문입니다. 인자를 평가하지 않지만 이름은 쓴 것으로 셉니다(`LogDiscardUtil`).
#define SW_LOG_DISCARDED( ... ) ( (void)sizeof( ::sw::LogDiscardUtil::consume( __VA_ARGS__ ) ) )

/**
 * @brief 포맷 문자열(과 인자)을 포맷해 Logger::writeLogGlobal 로 넘기는 핵심 매크로입니다.
 * @details **런타임 상세도를 먼저 확인합니다.** 버려질 줄은 8KB 버퍼 포맷 비용도 치르지 않습니다.
 * @note 메시지 문자열을 __VA_ARGS__ 의 첫 인자로 받으므로, 가변 인자 생략(C++20) 확장을 쓰지 않습니다.
 */
#define SW_LOG_WRITE( level, pID, ... )                                                                     \
    do                                                                                                      \
    {                                                                                                       \
        if ( ::sw::Logger::shouldLog( level ) )                                                             \
        {                                                                                                   \
            ::utf8 arrBuffer[::sw::constant::kMaxBuffer8192];                                               \
            ::sw::formatstring( arrBuffer, ::sw::constant::kMaxBuffer8192, __VA_ARGS__ );                   \
            ::sw::Logger::writeLogGlobal( level, SW_LOG_TAG, nullptr, arrBuffer, __FILE__, __LINE__, pID ); \
        }                                                                                                   \
    } while ( false )

/// @brief id 가 없는 줄입니다.
#define SW_LOG_INTERNAL( level, ... ) SW_LOG_WRITE( level, nullptr, __VA_ARGS__ )

/**
 * @brief 이어 갈 수 없는 결함을 한 줄로 남기고 **모든 구성에서** 멈춥니다(언리얼 `UE_LOG(Fatal)` · 유니티 `Debug.LogError` + `Application.Quit` 자리).
 * @details 서식은 `SW_LOG_*` 와 같은 `formatstring`(`%#`)입니다. 줄은 stderr 에 먼저 동기로 쓰이고 로그로도 가며, 비동기 싱크를 비운 뒤
 *          `std::abort` 합니다(`sw::internal::reportFatal`, `[[noreturn]]`). 로그 수준 필터 · 컴파일 제외를 타지 않습니다. 로그보다 아래 티어는 `SW_LOW_LEVEL_FATAL`.
 */
#define SW_FATAL( ... )                                                                           \
    do                                                                                            \
    {                                                                                             \
        ::utf8 arrFatalMessage[::sw::constant::kMaxBuffer8192];                                   \
        ::sw::formatstring( arrFatalMessage, ::sw::constant::kMaxBuffer8192, __VA_ARGS__ );       \
        ::sw::internal::reportFatal( __FILE__, static_cast<int32>( __LINE__ ), arrFatalMessage ); \
    } while ( false )

#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_ERROR )
    /** @brief Error 수준으로 포맷해 남깁니다. 모든 빌드에 남습니다. */
    #define SW_LOG_ERROR( ... ) SW_LOG_INTERNAL( sw::LogLevel::Error, __VA_ARGS__ )
    /**
     * @brief id 가 붙은 Error 입니다. 테스트가 줄의 문장이 아니라 id 로 찾는다(`test::ScopedLogCollector::countWithID`) — 문구를 고쳐도 테스트가 지지 않는다.
     * @param id `"영역.사건"` 꼴의 **글자 상수**(예: `"Cook.RemovedOutput"`)입니다. 변수는 받지 않습니다(`"" id` 로 글자 상수를 강제한다). 같은 사건은 같은 id 를 씁니다.
     */
    #define SW_LOG_ERROR_ID( id, ... ) SW_LOG_WRITE( sw::LogLevel::Error, "" id, __VA_ARGS__ )
#else
    #define SW_LOG_ERROR( ... )        SW_LOG_DISCARDED( __VA_ARGS__ )
    #define SW_LOG_ERROR_ID( id, ... ) SW_LOG_DISCARDED( __VA_ARGS__ )
#endif

#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_WARNING )
    /** @brief Warning 수준으로 포맷해 남깁니다. 배포본에도 남습니다. */
    #define SW_LOG_WARNING( ... ) SW_LOG_INTERNAL( sw::LogLevel::Warning, __VA_ARGS__ )
    /** @brief id 가 붙은 Warning 입니다(`SW_LOG_ERROR_ID` 와 같다). */
    #define SW_LOG_WARNING_ID( id, ... ) SW_LOG_WRITE( sw::LogLevel::Warning, "" id, __VA_ARGS__ )
#else
    #define SW_LOG_WARNING( ... )        SW_LOG_DISCARDED( __VA_ARGS__ )
    #define SW_LOG_WARNING_ID( id, ... ) SW_LOG_DISCARDED( __VA_ARGS__ )
#endif

#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
    /** @brief Info 수준으로 포맷해 남깁니다. 배포본에서는 호출이 사라집니다. */
    #define SW_LOG_INFO( ... ) SW_LOG_INTERNAL( sw::LogLevel::Info, __VA_ARGS__ )
#else
    #define SW_LOG_INFO( ... ) SW_LOG_DISCARDED( __VA_ARGS__ )
#endif

#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_TRACE )
    /** @brief Trace 수준으로 포맷해 남깁니다. Debug 에만 컴파일됩니다. */
    #define SW_LOG_TRACE( ... ) SW_LOG_INTERNAL( sw::LogLevel::Trace, __VA_ARGS__ )
#else
    #define SW_LOG_TRACE( ... ) SW_LOG_DISCARDED( __VA_ARGS__ )
#endif

#if defined( SW_DEBUG )

    /**
     * @brief 조건이 거짓이면 메시지 · 식 · 파일 · 함수 · 줄을 Error 로 남기고 디버거에서 멈춥니다.
     * @note 테스트가 단언 가로채기(`test::ScopedAssertCapture`)를 걸어 두었으면 멈추지 않고 세기만 합니다.
     * @note Debug 에서만 멈춥니다. 그 밖의 빌드는 아래에서 **로그만** 남깁니다. 배포본에서 단언이 통째로 사라지면 무엇이
     *       어긋났는지 알 길이 없기 때문입니다.
     */
    #define SW_LOG_ASSERT( expr, ... )                                                                                                          \
        do                                                                                                                                      \
        {                                                                                                                                       \
            if ( !( expr ) )                                                                                                                    \
            {                                                                                                                                   \
                utf8 _assertMsg[sw::constant::kMaxBuffer8192];                                                                                  \
                sw::formatstring( _assertMsg, sw::constant::kMaxBuffer8192, __VA_ARGS__ );                                                      \
                ::sw::Logger::printAssertFailureToStderr( #expr, _assertMsg, __FILE__, static_cast<int32>( __LINE__ ), SW_FUNCTION_SIGNATURE ); \
                SW_LOG_INTERNAL( sw::LogLevel::Error,                                                                                           \
                                 "ASSERT failed\n"                                                                                              \
                                 "Expression : %#\n"                                                                                            \
                                 "Message    : %#\n"                                                                                            \
                                 "FileName   : %#\n"                                                                                            \
                                 "Function   : %#\n"                                                                                            \
                                 "Line       : %#",                                                                                             \
                                 #expr, _assertMsg, __FILE__, SW_FUNCTION_SIGNATURE, __LINE__ );                                                \
                if ( ::sw::internal::tryCaptureAssert() == false &&                                                                             \
                     ::sw::internal::shouldBreakOnAssert( #expr, _assertMsg, __FILE__, __LINE__ ) )                                             \
                    SW_DEBUG_BREAK();                                                                                                           \
            }                                                                                                                                   \
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
