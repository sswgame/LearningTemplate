/**
 * @file Macros.h
 * @brief Core 공통 매크로입니다 — 어서션, 컴파일러 · 플랫폼 판별, SW_API, 인라인 힌트 등.
 */
#pragma once
#include "Core/Common/Types.h"

#include <type_traits> // SW_REQUIRES · arrayCountHelper 의 std::enable_if_t

// ------------------------------------------------------------------------------
// 1) 전처리기 유틸리티 (Pre-processor Utilities)
// ------------------------------------------------------------------------------

#define SW_CONCAT_IMPL( x, y ) x##y
#define SW_CONCAT( x, y )      SW_CONCAT_IMPL( x, y )

/** @brief 구조체/클래스 내 멤버 오프셋을 바이트 단위로 반환합니다. */
#define SW_OFFSET_OF( type, member ) offsetof( type, member )

// ------------------------------------------------------------------------------
// 2) 디버그 브레이크 — 어서션이 실패하면 디버거에서 멈춘다
// ------------------------------------------------------------------------------
#if defined( _MSC_VER )
    /** @brief MSVC 디버거 브레이크입니다. */
    #define SW_DEBUG_BREAK() __debugbreak()
#elif defined( __clang__ ) || defined( __GNUC__ ) || defined( __GNUG__ )
    /** @brief Clang/GCC 트랩입니다. */
    #define SW_DEBUG_BREAK() __builtin_trap()
#else
    #error "SW_DEBUG_BREAK is not supported on this compiler or platform."
#endif

// ------------------------------------------------------------------------------
// 3) 함수 시그니처 — 컴파일러별 pretty name
// ------------------------------------------------------------------------------
#ifdef _MSC_VER
    /** @brief MSVC 함수 시그니처 문자열입니다. */
    #define SW_FUNCTION_SIGNATURE __FUNCSIG__
#else
    /** @brief Clang/GCC 의 함수 시그니처 문자열입니다(__PRETTY_FUNCTION__). */
    #define SW_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

// ------------------------------------------------------------------------------
// 4) 디버그 / 릴리즈 — 따로 정하지 않았으면 _DEBUG 로 가린다
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
// 5) SW_ASSERT — 논리 불변식 검사(메시지 없음, 자주 도는 경로용)
//    메시지가 필요하면 Logger.h 의 SW_LOG_ASSERT 를 쓴다. 그쪽은 배포본에서도 로그를 남긴다
// ------------------------------------------------------------------------------
/**
 * 사용 가이드:
 *   SW_ASSERT(expr)        — 논리 불변식 검사(메시지 없음, 자주 도는 경로)
 *   SW_LOG_ASSERT(expr, …) — 실패 원인을 추적해야 하는 곳(로그 + 브레이크, 드물게 도는 경로)
 *
 * 둘은 배포본에서 다르게 동작합니다(예전 주석에는 "둘 다 no-op" 이라고 적혀 있었지만 사실이 아니었습니다).
 *   - `SW_ASSERT`     : Debug 가 아니면 통째로 사라집니다. 식도 평가하지 않으므로, 부수 효과가 있는 식을 넣으면
 *                       배포본에서는 그 효과가 없어집니다.
 *   - `SW_LOG_ASSERT` : Debug 에서만 멈추고, 그 밖의 빌드에서는 Error 로그를 남깁니다. 예전에는 이쪽도 no-op 이라
 *                       배포본에서 계약이 깨진 순간을 놓쳤습니다(Logger.h 의 비-Debug 분기 참고).
 *
 * 배포본에서도 반드시 막아야 하는 조건에는 둘 다 맞지 않습니다. 직접 if 로 검사하고 빠져나가십시오.
 */
#if defined( SW_DEBUG )
    /** @brief 식이 거짓이면 디버거에서 멈춥니다. Debug 가 아니면 식째 사라집니다. */
    #define SW_ASSERT( expr )     \
        do                        \
        {                         \
            if ( !( expr ) )      \
            {                     \
                SW_DEBUG_BREAK(); \
            }                     \
        } while ( false )
#else
    /** @brief Debug 가 아니면 어서션을 없앱니다(식도 평가하지 않습니다). */
    #define SW_ASSERT( expr )
#endif

// ------------------------------------------------------------------------------
// 6) 플랫폼 — Windows / Linux / macOS 중 하나
// ------------------------------------------------------------------------------
#if !defined( SW_PLATFORM_WINDOWS ) && !defined( SW_PLATFORM_LINUX ) && !defined( SW_PLATFORM_MACOS )
    #if defined( _WIN32 ) || defined( _WIN64 )
        /** @brief Windows 타깃입니다. */
        #define SW_PLATFORM_WINDOWS
    #elif defined( __linux__ )
        /** @brief Linux 타깃입니다. */
        #define SW_PLATFORM_LINUX
    #elif defined( __APPLE__ )
        /** @brief macOS 타깃입니다. */
        #define SW_PLATFORM_MACOS
    #else
        #error "Unknown target platform."
    #endif
#endif

// ------------------------------------------------------------------------------
// 7) DLL export / import — Engine.dll 은 SW_API, 게임/에디터 모듈은 SW_MODULE_API
// ------------------------------------------------------------------------------
#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_EXPORTS )
        /** @brief Engine.dll 을 빌드할 때 export 합니다. Core 의 OBJECT 라이브러리도 이 매크로로 내보냅니다. */
        #define SW_API __declspec( dllexport )
    #elif defined( SW_IMPORTS )
        /** @brief Engine.dll 을 사용할 때 import 합니다. */
        #define SW_API __declspec( dllimport )
    #else
        /** @brief 정적 링크할 때는 아무것도 붙이지 않습니다. */
        #define SW_API
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
    #define SW_API        __attribute__( ( visibility( "default" ) ) )
    /** @brief 모듈 심볼도 기본 visibility 로 내보냅니다. */
    #define SW_MODULE_API __attribute__( ( visibility( "default" ) ) )
#endif

/** @brief 코드 블록을 명시적으로 구분할 때 사용합니다 (세미콜론 없이: BLOCK( "..." )). */
#define BLOCK( message )

/**
 * @brief 비트마스크 비트를 만듭니다.
 * @note x 가 31 일 때 부호 있는 오버플로(UB)가 나지 않도록 unsigned 리터럴(1u)을 씁니다.
 */
#define SW_BIT( x ) ( 1u << ( x ) )

// ------------------------------------------------------------------------------
// 8) 비트 필드 불리언 값 — 1/0 이 개수가 아니라 상태라는 것을 드러낸다
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
// 하나가 된다. 매크로가 이름공간까지 붙여 부르므로 쓰는 쪽은 달라지지 않는다.
namespace sw
{
#ifdef __clang__
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
// 10) 인라인 · 노인라인 · restrict — 핫패스 최적화 힌트
// ------------------------------------------------------------------------------
#if defined( _MSC_VER )
    /** @brief 강제 인라인 힌트입니다. */
    #define SW_INLINE __forceinline
    /** @brief 인라인 금지 힌트입니다. */
    #define SW_NOINLINE __declspec( noinline )
    /** @brief 이 포인터가 다른 포인터와 겹치지 않는다(no alias)고 컴파일러에 알려 줍니다. */
    #define SW_RESTRICT __restrict
#elif defined( __GNUC__ ) || defined( __clang__ )
    /** @brief 강제 인라인 힌트입니다. */
    #define SW_INLINE   inline __attribute__( ( always_inline ) )
    /** @brief 인라인 금지 힌트입니다. */
    #define SW_NOINLINE __attribute__( ( noinline ) )
    /** @brief 이 포인터가 다른 포인터와 겹치지 않는다(no alias)고 컴파일러에 알려 줍니다. */
    #define SW_RESTRICT __restrict__
#else
    /** @brief 일반 inline 입니다. */
    #define SW_INLINE inline
    /** @brief 인라인 금지 힌트입니다. */
    #define SW_NOINLINE
    /** @brief restrict 를 지원하지 않으면 빈 매크로입니다. */
    #define SW_RESTRICT
#endif

// ------------------------------------------------------------------------------
// 11) CPU Pause / Yield — 스핀 대기 힌트 (x86/x64 · ARM/ARM64, 그 밖에는 아무것도 하지 않는다)
// ------------------------------------------------------------------------------
#if defined( _MSC_VER )
    #if defined( _M_IX86 ) || defined( _M_X64 )
        #include <emmintrin.h>
        /** @brief x86/x64 PAUSE 명령입니다. */
        #define SW_CPU_PAUSE() _mm_pause()
    #elif defined( _M_ARM ) || defined( _M_ARM64 ) || defined( _M_ARM64EC )
        /** @brief ARM/ARM64 YIELD 명령입니다. */
        #define SW_CPU_PAUSE() __yield()
    #else
        #define SW_CPU_PAUSE() ( (void)0 )
    #endif
#elif defined( __GNUC__ ) || defined( __clang__ )
    #if defined( __i386__ ) || defined( __x86_64__ )
        /** @brief x86/x64 PAUSE — Clang/GCC 내장 함수입니다. */
        #define SW_CPU_PAUSE() __builtin_ia32_pause()
    #elif defined( __arm__ ) || defined( __aarch64__ )
        /** @brief ARM/ARM64 YIELD — 인라인 어셈블리입니다. */
        #define SW_CPU_PAUSE() asm volatile( "yield" ::: "memory" )
    #else
        #define SW_CPU_PAUSE() ( (void)0 )
    #endif
#else
    #define SW_CPU_PAUSE() ( (void)0 )
#endif

namespace sw
{
    /**
     * @brief 스핀 대기 중에 CPU 에 "기다리는 중" 이라고 알려 줍니다(Pause/Yield).
     * @details
     * - **x86/x64**: `PAUSE`(`_mm_pause`). 스핀 루프가 파이프라인을 헛돌리는 것을 줄여 전력을 아끼고, 루프를 빠져나올 때
     *   생기는 메모리 순서 위반 페널티를 없앱니다. 같은 코어의 다른 하이퍼스레드에 자원을 양보하는 효과도 있습니다.
     * - **ARM/ARM64**: `yield`. 같은 코어를 나눠 쓰는 다른 스레드에 양보하라는 힌트입니다.
     * - **그 밖의 아키텍처**: 아무것도 하지 않습니다.
     */
    SW_INLINE void cpuPause() noexcept
    {
        SW_CPU_PAUSE();
    }
} // namespace sw
