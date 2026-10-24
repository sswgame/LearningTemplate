/**
 * @file Macros.h
 * @brief Core 공통 매크로(어서션, 디버그 브레이크, SW_API, 인라인 힌트 등)를 정의합니다. 타깃 매크로 검사는 TargetMacroCheck.h 에 있습니다.
 */
#pragma once
#include "Core/Common/TargetMacroCheck.h"
#include "Core/Common/Types.h"

#include <cstdio>      // SW_ASSERT 가 멈추기 전에 남기는 한 줄(Debug)
#include <type_traits> // SW_REQUIRES 와 arrayCountHelper 의 std::enable_if_t

// ------------------------------------------------------------------------------
// 1) 전처리기 유틸리티 (Pre-processor Utilities)
// ------------------------------------------------------------------------------

#define SW_CONCAT_IMPL( x, y ) x##y
#define SW_CONCAT( x, y )      SW_CONCAT_IMPL( x, y )

/** @brief 구조체/클래스 내 멤버 오프셋을 바이트 단위로 반환합니다. */
#define SW_OFFSET_OF( type, member ) offsetof( type, member )

// ------------------------------------------------------------------------------
// 2) 디버그 브레이크. 어서션이 실패하면 디버거에서 멈춘다.
//    MSVC 확장(`__debugbreak`, `__FUNCSIG__`, `__forceinline`, `__declspec`)을 쓸 수 있는지는 SW_PLATFORM_WINDOWS 로 판단한다.
//    Windows 는 MS ABI 툴체인(cl, clang-cl)만 빌드하고(TargetMacroCheck.h), clang-cl 은 SW_COMPILER_CLANG 이므로 SW_COMPILER_MSVC 로는 판단할 수 없다.
// ------------------------------------------------------------------------------
#if defined( SW_PLATFORM_WINDOWS )
    /** @brief MSVC 확장 디버거 브레이크입니다(cl · clang-cl). */
    #define SW_DEBUG_BREAK() __debugbreak()
#else
    /** @brief Clang/GCC 트랩입니다. */
    #define SW_DEBUG_BREAK() __builtin_trap()
#endif

// ------------------------------------------------------------------------------
// 3) 함수 시그니처 — 컴파일러별 pretty name
// ------------------------------------------------------------------------------
#if defined( SW_PLATFORM_WINDOWS )
    /** @brief MSVC 확장 함수 시그니처 문자열입니다(cl · clang-cl). */
    #define SW_FUNCTION_SIGNATURE __FUNCSIG__
#else
    /** @brief Clang/GCC 의 함수 시그니처 문자열입니다(__PRETTY_FUNCTION__). */
    #define SW_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

// ------------------------------------------------------------------------------
// 4) 디버그와 릴리즈. 따로 정하지 않았으면 _DEBUG 로 구분한다.
// ------------------------------------------------------------------------------
#if !defined( SW_DEBUG ) && !defined( SW_RELEASE )

    #if defined( _DEBUG ) || defined( DEBUG )
        /** @brief 디버그 빌드입니다. */
        #define SW_DEBUG 1
    #else
        /** @brief 릴리즈 빌드입니다. */
        #define SW_RELEASE 1
    #endif
#endif

// ------------------------------------------------------------------------------
// 5) SW_ASSERT. 논리 불변식을 검사한다(메시지 없음, 자주 도는 경로용).
//    메시지가 필요하면 Logger.h 의 SW_LOG_ASSERT 를 쓴다. 그쪽은 배포본에서도 로그를 남긴다.
// ------------------------------------------------------------------------------
/**
 * 사용 가이드:
 *   SW_ASSERT(expr)        — 논리 불변식 검사(메시지 없음, 자주 도는 경로)
 *   SW_LOG_ASSERT(expr, …) — 실패 원인을 추적해야 하는 곳(로그 + 브레이크, 드물게 도는 경로)
 *
 * 둘은 배포본에서 다르게 동작합니다.
 *   - `SW_ASSERT`     : Debug 가 아니면 통째로 사라집니다. 식도 평가하지 않으므로, 부수 효과가 있는 식을 넣으면
 *                       배포본에서는 그 효과가 없어집니다.
 *   - `SW_LOG_ASSERT` : Debug 에서만 멈추고, 그 밖의 빌드에서는 Error 로그를 남깁니다. 배포본에서도 계약이 깨진 순간을
 *                       놓치지 않습니다(Logger.h 의 비-Debug 분기 참고).
 *
 * 배포본에서도 반드시 막아야 하는 조건에는 둘 다 맞지 않습니다. 직접 if 로 검사하고 빠져나가십시오.
 * 로그보다 아래 티어(메모리 · 수학 · 컨테이너 등)는 `SW_LOG_ASSERT` · `SW_LOG_*` 를 쓸 수 없으므로 아래 10) 의 `SW_LOW_LEVEL_ASSERT` · `SW_LOW_LEVEL_ERROR` · `SW_LOW_LEVEL_WARNING` 을 씁니다.
 */
#if defined( SW_DEBUG )
namespace sw::internal
{
    /**
     * @brief `SW_ASSERT` 가 멈추기 **전에** 무엇이 어디서 어긋났는지 stderr 에 남깁니다.
     * @details 디버거 없이 실행하면(CI, 테스트 자식 프로세스, 다른 사람의 PC) 크래시 리포트의 "EXCEPTION_BREAKPOINT"(리눅스는 SIGILL)와
     *          스택만 남는다. 이 함수가 없으면 어느 식이 어긋났는지 알 수 없다.
     *          그래서 식, 파일, 줄, 함수를 남기고 버퍼를 비운 뒤 멈춘다. 로거는 비동기라 멈추면 메시지를 잃을 수 있기 때문이다.
     */
    inline void printAssertFailure( const utf8* pExpression, const utf8* pFile, int32 line, const utf8* pFunction ) noexcept
    {
        std::fprintf( stderr, "\n[SW_ASSERT] %s\n  at %s:%d\n  in %s\n", pExpression, pFile, line, pFunction );
        std::fflush( stderr );
    }
} // namespace sw::internal

    /**
     * @brief 식이 거짓이면 그 식과 위치를 stderr 에 남기고 디버거에서 멈춥니다. Debug 가 아니면 식째 사라집니다.
     * @note 테스트가 단언 가로채기(`test::ScopedAssertCapture`)를 걸어 두었으면 멈추지 않고 개수만 센다(`tryCaptureAssert`).
     *       대화형 에디터 실행이면 App 이 설치한 대화상자가 멈출지 묻는다(`shouldBreakOnAssert`). 선택지는 이번만 무시, 디버거 중단, 이 위치 항상 무시다.
     */
    #define SW_ASSERT( expr )                                                                                                 \
        do                                                                                                                    \
        {                                                                                                                     \
            if ( !( expr ) )                                                                                                  \
            {                                                                                                                 \
                ::sw::internal::printAssertFailure( #expr, __FILE__, static_cast<int32>( __LINE__ ), SW_FUNCTION_SIGNATURE ); \
                if ( ::sw::internal::tryCaptureAssert() == false &&                                                           \
                     ::sw::internal::shouldBreakOnAssert( #expr, nullptr, __FILE__, static_cast<int32>( __LINE__ ) ) )        \
                    SW_DEBUG_BREAK();                                                                                         \
            }                                                                                                                 \
        } while ( false )
#else
    /** @brief Debug 가 아니면 어서션을 없앱니다(식도 평가하지 않습니다). */
    #define SW_ASSERT( expr )
#endif

// ------------------------------------------------------------------------------
// 6) 플랫폼, 아키텍처, 컴파일러. `SW_PLATFORM_*`, `SW_X64` / `SW_ARM64`, `SW_COMPILER_*` 는 CMake 가 정의하고
//    `TargetMacroCheck.h`(이 파일 맨 위에서 포함)가 실제 컴파일러와 대조한다. 여기서 다시 판정하지 않는다.
// ------------------------------------------------------------------------------

// ------------------------------------------------------------------------------
// 7) DLL export / import — Engine.dll 은 SW_API, 게임/에디터 모듈은 SW_MODULE_API
// ------------------------------------------------------------------------------
#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_EXPORTS )
        /** @brief Engine.dll 을 빌드할 때 export 합니다. Core 의 OBJECT 라이브러리도 이 매크로로 내보냅니다. */
        #define SW_API __declspec( dllexport )
        /**
         * @brief Engine.dll 이 명시적으로 인스턴스화해 내보내는 클래스 템플릿의 `extern template` 선언에 붙입니다. 내보내는 쪽에서는 비웁니다 —
         *        인스턴스화 선언에 dllexport 를 붙이면 clang 이 경고합니다(-Wdllexport-explicit-instantiation-decl). 정의 쪽은 `template class SW_API …;` 입니다.
         */
        #define SW_API_EXTERN_TEMPLATE
    #elif defined( SW_IMPORTS )
        /** @brief Engine.dll 을 사용할 때 import 합니다. */
        #define SW_API                 __declspec( dllimport )
        #define SW_API_EXTERN_TEMPLATE __declspec( dllimport )
    #else
        /** @brief 정적 링크할 때는 아무것도 붙이지 않습니다. */
        #define SW_API
        #define SW_API_EXTERN_TEMPLATE
    #endif

    #if defined( SW_MODULE_EXPORTS )
        /** @brief 게임/에디터 모듈 DLL 을 빌드할 때 C-ABI 진입점을 export 합니다. */
        #define SW_MODULE_API __declspec( dllexport )
    #else
        /** @brief 진입점은 GetProcAddress 로만 찾으므로 dllimport 를 쓰지 않습니다. */
        #define SW_MODULE_API
    #endif
#else
    /** @brief ELF · Mach-O 에서는 기본 visibility 로 내보냅니다. */
    #define SW_API                 __attribute__( ( visibility( "default" ) ) )
    /** @brief 명시적 인스턴스화 선언도 기본 visibility 로 둡니다(숨긴 visibility 로 선언한 참조는 다른 이미지에서 풀리지 않는다). */
    #define SW_API_EXTERN_TEMPLATE __attribute__( ( visibility( "default" ) ) )
    /** @brief 모듈 심볼도 기본 visibility 로 내보냅니다. */
    #define SW_MODULE_API          __attribute__( ( visibility( "default" ) ) )
#endif

// ------------------------------------------------------------------------------
// 8) 단언 가로채기. 테스트가 단언 경로를 지나가게 할 때만 쓴다.
//    Debug 의 단언은 멈추므로(`SW_DEBUG_BREAK`) 단언이 걸리는 입력을 테스트하면 프로세스가 죽는다. 가로채기를 건 동안은 멈추지 않고
//    개수만 센다. 유니티의 `LogAssert.Expect`, 언리얼 자동화의 기대 오류와 같은 역할이다. 정의는 `Core/Common/Macros.cpp` 에 있다.
// ------------------------------------------------------------------------------
#if defined( SW_DEBUG )
// 멈추는 단언(`SW_ASSERT`, 멈추는 `SW_LOG_ASSERT`)은 Debug 에만 있으므로 가로채기도 Debug 에만 둔다. 배포본에는 테스트가 단언을 끄는 함수가 없다.
namespace sw::internal
{
    /** @brief 가로채기가 걸려 있으면 단언 하나를 세고 true 를 반환합니다. 부르는 쪽(`SW_ASSERT`, `SW_LOG_ASSERT`)은 멈추지 않습니다. */
    [[nodiscard]] SW_API bool tryCaptureAssert() noexcept;
    /** @brief 가로채기를 겁니다. 겹쳐 걸 수 있으며, 건 횟수만큼 풀어야 멈춤이 돌아옵니다. */
    SW_API void beginAssertCapture() noexcept;
    /** @brief 가로채기 하나를 해제합니다. */
    SW_API void endAssertCapture() noexcept;
    /** @brief 프로세스가 지금까지 가로챈 단언 수입니다. 구간의 수는 시작과 끝의 차로 측정합니다. */
    [[nodiscard]] SW_API uint32 getCapturedAssertCount() noexcept;
} // namespace sw::internal

// ------------------------------------------------------------------------------
// 9) 단언 대화상자. 대화형 에디터 실행에서만 App 이 설치한다(언리얼의 ensure 대화상자). 정의는 `Core/Common/Macros.cpp` 에 있다.
//    설치되지 않았으면(자동 실행, 테스트, 에디터 없는 실행) 단언은 기존처럼 멈춘다.
// ------------------------------------------------------------------------------
namespace sw::internal
{
    /** @brief 대화상자에서 고른 동작입니다. */
    enum class AssertAction : uint8
    {
        Break,        ///< 디버거로 멈춘다(디버거가 없으면 크래시 리포트가 남는다).
        IgnoreOnce,   ///< 이번만 넘긴다.
        IgnoreAlways, ///< 이 위치(파일과 줄)는 이 실행 동안 계속 넘긴다.
    };

    /** @brief 대화상자 함수입니다. @p pMessage 는 `SW_LOG_ASSERT` 의 메시지이며, 없으면 nullptr 입니다. */
    using AssertDialogFunc = AssertAction ( * )( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line );

    /** @brief 대화상자를 설치합니다. nullptr 이면 해제하며, 이후 단언은 항상 멈춥니다. "항상 무시"로 고른 위치도 함께 잊습니다. */
    SW_API void setAssertDialog( AssertDialogFunc pfnDialog ) noexcept;
    /** @brief 대화상자가 설치돼 있으면 true 를 반환합니다. */
    [[nodiscard]] SW_API bool hasAssertDialog() noexcept;
    /**
     * @brief 단언이 멈춰야 하면 true 를 반환합니다. 이 위치가 "항상 무시"이면 false, 대화상자가 없으면 true(기존 동작), 있으면 대화상자에 묻습니다.
     * @details 대화상자는 한 번에 하나만 뜬다. 떠 있는 동안 다른 스레드의 단언은 잠금에서 기다린다.
     */
    [[nodiscard]] SW_API bool shouldBreakOnAssert( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line ) noexcept;
} // namespace sw::internal
#endif

// ------------------------------------------------------------------------------
// 10) 로그보다 아래 티어의 보고. `Core/Log` 는 컨테이너 · 메모리 · 수학보다 위라서 그 아래 티어는 `SW_LOG_*` 를 쓸 수 없다(`CheckCoreLayers`).
//     아래 티어는 `SW_LOW_LEVEL_ERROR` · `SW_LOW_LEVEL_WARNING` · `SW_LOW_LEVEL_ASSERT` 로 보고한다(`SW_LOG_*` 와 같은 호출 구조: 수준은 이름에,
//     호출하는 쪽의 이름은 인자가 아니라 파일에서 자동으로 정해진다). 구조는 Godot 의 `_err_print_error` + `add_error_handler` 와 같다 —
//     `const char*` 만 오가는 동기 통로이고, 로그가 기동 때 등록하는 보고자(`setLowLevelReporter`, `Logger.cpp`)가 로그 줄로 바꾼다.
//     **Error · 단언은 보고자가 있어도 stderr 에 먼저 동기로 쓰고 flush 한 뒤** 보고자에게 넘긴다(비동기 로거는 크래시 직전 메시지를 잃는다).
//     Warning 은 보고자가 받아 가면 stderr 에 쓰지 않는다. 보고자가 없거나 받지 못하면(로거 이전 · 로거 없는 도구 · 싱크 없음) stderr 에 남는다.
//     stderr 로 가는 경로는 `Core/Common/Macros.cpp` 한 곳이다.
//
//     **서식은 `printf` 문법이다(`snprintf`). 엔진 `formatstring` 의 `%#` 가 아니다.** 어긋남은 `SW_PRINTF_FORMAT` 이 컴파일러(`-Wformat`)에게 맡긴다.
// ------------------------------------------------------------------------------
#if defined( SW_COMPILER_CLANG ) || defined( SW_COMPILER_GCC )
    /** @brief `printf` 식 서식 인자를 컴파일러가 검사하게 합니다(`-Wformat`). @p formatIndex 는 서식 문자열, @p firstArgIndex 는 첫 가변 인자의 위치(1부터)입니다. */
    #define SW_PRINTF_FORMAT( formatIndex, firstArgIndex ) __attribute__( ( format( printf, formatIndex, firstArgIndex ) ) )
#else
    #define SW_PRINTF_FORMAT( formatIndex, firstArgIndex )
#endif

namespace sw::internal
{
    /** @brief 아래 티어 보고의 심각도입니다. 로그 수준 `Warning` · `Error` 와 같은 뜻입니다. */
    enum class LowLevelReportLevel : uint8
    {
        Warning,
        Error,
    };

    /** @brief 한 보고의 글 버퍼 크기(널 포함)입니다. 넘치면 끝이 잘리고 `...[truncated]` 가 붙습니다. */
    static constexpr size_t kLowLevelMessageCapacity = 512;

    /**
     * @brief 보고자 함수입니다. @p pFile · @p line 은 보고한 위치(`__FILE__` · `__LINE__`)이고 @p pMessage 는 끝난 글입니다.
     * @return 로그가 받았으면 true(수준 필터로 버려진 것도 받은 것). 받지 못했으면(싱크 없음) false 이고, 그러면 `Warning` 은 stderr 로 갑니다.
     */
    using LowLevelReporterFunc = bool ( * )( LowLevelReportLevel level, const utf8* pFile, int32 line, const utf8* pMessage ) noexcept;

    /** @brief 보고자를 설치하고 전에 등록돼 있던 보고자를 돌려줍니다. nullptr 이면 해제하며, 이후 보고는 stderr 로 갑니다. */
    SW_API LowLevelReporterFunc setLowLevelReporter( LowLevelReporterFunc pfnReporter ) noexcept;
    /** @brief @p pFormat(`printf` 문법)을 @p pBuffer 에 씁니다. @p capacity 를 넘기면 끝을 `...[truncated]` 로 바꿔 잘렸음을 드러냅니다. */
    SW_API void formatLowLevelMessage( utf8* pBuffer, size_t capacity, const utf8* pFormat, ... ) noexcept SW_PRINTF_FORMAT( 3, 4 );
    /** @brief 한 줄을 보고합니다. `Error` 는 stderr 에 먼저 쓰고 보고자에게 넘기며, `Warning` 은 보고자가 받지 못할 때만 stderr 로 갑니다. */
    SW_API void reportLowLevel( LowLevelReportLevel level, const utf8* pFile, int32 line, const utf8* pMessage ) noexcept;
    /** @brief 단언 실패 하나를 `Error` 로 보고합니다(`SW_LOG_ASSERT` 와 같은 모양의 글). */
    SW_API void reportLowLevelAssert( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line ) noexcept;
} // namespace sw::internal

/**
 * @brief 로그보다 아래 티어가 한 줄을 보고합니다(`SW_LOG_ERROR` · `SW_LOG_WARNING` 에 해당). 인자는 `printf` 문법의 서식과 값입니다(512 자까지).
 * @note 서식은 `snprintf` 문법입니다. 엔진 `formatstring` 의 `%#` 를 쓰면 `-Wformat` 이 막습니다(`SW_PRINTF_FORMAT`).
 */
#define SW_LOW_LEVEL_INTERNAL( level, ... )                                                                                 \
    do                                                                                                                      \
    {                                                                                                                       \
        utf8 arrLowLevelMessage[::sw::internal::kLowLevelMessageCapacity];                                                  \
        ::sw::internal::formatLowLevelMessage( arrLowLevelMessage, ::sw::internal::kLowLevelMessageCapacity, __VA_ARGS__ ); \
        ::sw::internal::reportLowLevel( level, __FILE__, static_cast<int32>( __LINE__ ), arrLowLevelMessage );              \
    } while ( false )

/** @brief 로그보다 아래 티어의 `SW_LOG_ERROR` 입니다. stderr 에 먼저 동기로 쓰고 로그로도 남깁니다. 서식은 `printf` 문법입니다. */
#define SW_LOW_LEVEL_ERROR( ... ) SW_LOW_LEVEL_INTERNAL( ::sw::internal::LowLevelReportLevel::Error, __VA_ARGS__ )

/** @brief 로그보다 아래 티어의 `SW_LOG_WARNING` 입니다. 서식은 `printf` 문법입니다. */
#define SW_LOW_LEVEL_WARNING( ... ) SW_LOW_LEVEL_INTERNAL( ::sw::internal::LowLevelReportLevel::Warning, __VA_ARGS__ )

#if defined( SW_DEBUG )
    /**
     * @brief 로그보다 아래 티어의 `SW_LOG_ASSERT` 입니다. 거짓이면 stderr 에 먼저 쓰고 로그로도 남긴 뒤 Debug 에서는 멈춥니다(가로채기 · 대화상자는 `SW_ASSERT` 와 같다).
     *        메시지는 `printf` 문법의 서식입니다(512 자까지).
     */
    #define SW_LOW_LEVEL_ASSERT( expr, ... )                                                                                        \
        do                                                                                                                          \
        {                                                                                                                           \
            if ( !( expr ) )                                                                                                        \
            {                                                                                                                       \
                utf8 arrLowLevelMessage[::sw::internal::kLowLevelMessageCapacity];                                                  \
                ::sw::internal::formatLowLevelMessage( arrLowLevelMessage, ::sw::internal::kLowLevelMessageCapacity, __VA_ARGS__ ); \
                ::sw::internal::reportLowLevelAssert( #expr, arrLowLevelMessage, __FILE__, static_cast<int32>( __LINE__ ) );        \
                if ( ::sw::internal::tryCaptureAssert() == false &&                                                                 \
                     ::sw::internal::shouldBreakOnAssert( #expr, arrLowLevelMessage, __FILE__, static_cast<int32>( __LINE__ ) ) )   \
                    SW_DEBUG_BREAK();                                                                                               \
            }                                                                                                                       \
        } while ( false )
#else
    /** @brief Debug 가 아니면 멈추지 않고 보고만 합니다(`SW_LOG_ASSERT` 와 같다 — 배포본에서도 계약이 깨진 순간을 남긴다). */
    #define SW_LOW_LEVEL_ASSERT( expr, ... )                                                                                        \
        do                                                                                                                          \
        {                                                                                                                           \
            if ( !( expr ) )                                                                                                        \
            {                                                                                                                       \
                utf8 arrLowLevelMessage[::sw::internal::kLowLevelMessageCapacity];                                                  \
                ::sw::internal::formatLowLevelMessage( arrLowLevelMessage, ::sw::internal::kLowLevelMessageCapacity, __VA_ARGS__ ); \
                ::sw::internal::reportLowLevelAssert( #expr, arrLowLevelMessage, __FILE__, static_cast<int32>( __LINE__ ) );        \
            }                                                                                                                       \
        } while ( false )
#endif

/** @brief 코드 블록을 명시적으로 구분할 때 사용합니다 (세미콜론 없이: BLOCK( "..." )). */
#define BLOCK( message )

/**
 * @brief 비트마스크 비트를 만듭니다.
 * @note x 가 31 일 때 부호 있는 오버플로(UB)가 나지 않도록 unsigned 리터럴(1u)을 씁니다.
 */
#define SW_BIT( x ) ( 1u << ( x ) )

// ------------------------------------------------------------------------------
// 8) 비트 필드 불리언 값. 1과 0이 개수가 아니라 상태라는 점을 드러낸다.
//        비트 필드에 true/false 를 바로 대입하면 경고가 날 수 있어 SW_TRUE(1) / SW_FALSE(0) 을 쓴다.
// ------------------------------------------------------------------------------
/** @brief 비트 필드에 참(1)을 대입할 때 사용합니다. */
#define SW_TRUE 1
/** @brief 비트 필드에 거짓(0)을 대입할 때 사용합니다. */
#define SW_FALSE 0

/** @brief 템플릿 제약(SFINAE)을 한 줄로 붙입니다. */
#define SW_REQUIRES( ... ) , std::enable_if_t<( __VA_ARGS__ ), int32> = 0

// ------------------------------------------------------------------------------
// 9) SW_COUNT_OF — 정적 배열의 원소 수
// ------------------------------------------------------------------------------
// 도우미 템플릿은 `sw` 안에 둔다. 모든 TU 가 이 헤더를 거치므로, 전역에 내놓은 이름 하나가 곧 저장소 전체의 이름
// 하나가 된다. 매크로가 네임스페이스까지 붙여 부르므로 쓰는 쪽의 코드는 달라지지 않는다.
namespace sw
{
#if defined( SW_COMPILER_CLANG )
    /** @brief 배열 참조로부터 크기가 "원소 수 + 1" 인 배열 타입을 추론합니다(Clang). */
    template <typename T SW_REQUIRES( __is_array( T ) )>
    auto arrayCountHelper( T& t ) -> utf8 ( & )[sizeof( t ) / sizeof( t[0] ) + 1];
#else
    /** @brief 배열 참조로부터 크기가 "원소 수 + 1" 인 배열 타입을 추론합니다. */
    template <typename T, size_t N>
    utf8 ( &arrayCountHelper( const T ( & )[N] ) )[N + 1];
#endif
} // namespace sw

/** @brief 정적 배열의 원소 개수를 컴파일 타임에 구합니다. */
#define SW_COUNT_OF( array ) ( sizeof( sw::arrayCountHelper( array ) ) - 1 )

// ------------------------------------------------------------------------------
// 10) 인라인, 노인라인, restrict. 핫패스 최적화 힌트.
// ------------------------------------------------------------------------------
#if defined( SW_PLATFORM_WINDOWS )
    /** @brief 강제 인라인 힌트입니다(MSVC 확장, cl · clang-cl). */
    #define SW_INLINE __forceinline
    /** @brief 인라인 금지 힌트입니다. */
    #define SW_NOINLINE __declspec( noinline )
    /** @brief 이 포인터가 다른 포인터와 겹치지 않는다(no alias)고 컴파일러에 알려 줍니다. */
    #define SW_RESTRICT __restrict
#else
    /** @brief 강제 인라인 힌트입니다(Clang/GCC). */
    #define SW_INLINE   inline __attribute__( ( always_inline ) )
    /** @brief 인라인 금지 힌트입니다. */
    #define SW_NOINLINE __attribute__( ( noinline ) )
    /** @brief 이 포인터가 다른 포인터와 겹치지 않는다(no alias)고 컴파일러에 알려 줍니다. */
    #define SW_RESTRICT __restrict__
#endif

// ------------------------------------------------------------------------------
// 11) CPU Pause / Yield. 스핀 대기 힌트(x64 PAUSE, arm64 YIELD).
// ------------------------------------------------------------------------------
#if defined( SW_X64 )
    #if defined( SW_PLATFORM_WINDOWS )
        #include <emmintrin.h>
        /** @brief x64 PAUSE 명령입니다(MSVC intrinsic). */
        #define SW_CPU_PAUSE() _mm_pause()
    #else
        /** @brief x64 PAUSE — Clang/GCC 내장 함수입니다. */
        #define SW_CPU_PAUSE() __builtin_ia32_pause()
    #endif
#elif defined( SW_ARM64 )
    #if defined( SW_PLATFORM_WINDOWS )
        /** @brief arm64 YIELD 명령입니다(MSVC intrinsic). */
        #define SW_CPU_PAUSE() __yield()
    #else
        /** @brief arm64 YIELD — 인라인 어셈블리입니다. */
        #define SW_CPU_PAUSE() asm volatile( "yield" ::: "memory" )
    #endif
#endif

namespace sw
{
    /**
     * @brief 스핀 대기 중에 CPU 에 "기다리는 중" 이라고 알려 줍니다(Pause/Yield).
     * @details
     * - **x64**: `PAUSE`(`_mm_pause`). 스핀 루프가 파이프라인을 헛돌리는 것을 줄여 전력을 아끼고, 루프를 빠져나올 때
     *   생기는 메모리 순서 위반 페널티를 없앱니다. 같은 코어의 다른 하이퍼스레드에 자원을 양보하는 효과도 있습니다.
     * - **arm64**: `yield`. 같은 코어를 나눠 쓰는 다른 스레드에 양보하라는 힌트입니다.
     */
    SW_INLINE void cpuPause() noexcept
    {
        SW_CPU_PAUSE();
    }
} // namespace sw
