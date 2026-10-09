/**
 * @file Macros.h
 * @brief Core 공통 매크로입니다 — 어서션, 디버그 브레이크, SW_API, 인라인 힌트 등. 타깃 매크로 검사는 TargetMacroCheck.h 입니다.
 */
#pragma once
#include "Core/Common/TargetMacroCheck.h"
#include "Core/Common/Types.h"

#include <cstdio>      // SW_ASSERT 가 멈추기 전에 남기는 한 줄(Debug)
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
//    MSVC 확장(`__debugbreak` · `__FUNCSIG__` · `__forceinline` · `__declspec`)을 쓸 수 있는지는 SW_PLATFORM_WINDOWS 로 묻는다.
//    Windows 는 MS ABI 툴체인(cl · clang-cl)만 짓고(TargetMacroCheck.h), clang-cl 은 SW_COMPILER_CLANG 이라 SW_COMPILER_MSVC 로는 물을 수 없다.
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
 * 둘은 배포본에서 다르게 동작합니다.
 *   - `SW_ASSERT`     : Debug 가 아니면 통째로 사라집니다. 식도 평가하지 않으므로, 부수 효과가 있는 식을 넣으면
 *                       배포본에서는 그 효과가 없어집니다.
 *   - `SW_LOG_ASSERT` : Debug 에서만 멈추고, 그 밖의 빌드에서는 Error 로그를 남깁니다 — 배포본에서도 계약이 깨진 순간을
 *                       놓치지 않습니다(Logger.h 의 비-Debug 분기 참고).
 *
 * 배포본에서도 반드시 막아야 하는 조건에는 둘 다 맞지 않습니다. 직접 if 로 검사하고 빠져나가십시오.
 */
#if defined( SW_DEBUG )
namespace sw::internal
{
    /**
     * @brief `SW_ASSERT` 가 멈추기 **전에** 무엇이 어디서 어긋났는지 stderr 에 남깁니다.
     * @details 디버거 없이 돌면(CI · 테스트 자식 · 다른 사람의 PC) 남는 것은 크래시 리포트의 "EXCEPTION_BREAKPOINT"(리눅스는 SIGILL)와
     *          스택뿐이라, 이것이 없으면 어느 식이 어긋났는지 모른다. 식 · 파일 · 줄 · 함수를 남기고 버퍼를 비운 뒤 멈춘다(로거는 비동기라
     *          멈추면 잃을 수 있다).
     */
    inline void printAssertFailure( const utf8* pExpression, const utf8* pFile, int32 line, const utf8* pFunction ) noexcept
    {
        std::fprintf( stderr, "\n[SW_ASSERT] %s\n  at %s:%d\n  in %s\n", pExpression, pFile, line, pFunction );
        std::fflush( stderr );
    }
} // namespace sw::internal

    /**
     * @brief 식이 거짓이면 그 식 · 자리를 stderr 에 남기고 디버거에서 멈춥니다. Debug 가 아니면 식째 사라집니다.
     * @note 시험이 단언 가로채기(`test::ScopedAssertCapture`)를 걸어 두었으면 멈추지 않고 세기만 합니다(`tryCaptureAssert`).
     *       대화형 에디터 실행이면 App 이 건 대화상자가 멈출지 묻습니다(`shouldBreakOnAssert` — 이번만 · 디버거 · 이 자리 늘 무시).
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
// 6) 플랫폼 · 아키텍처 · 컴파일러 — `SW_PLATFORM_*` · `SW_X64` / `SW_ARM64` · `SW_COMPILER_*` 는 CMake 가 정의하고
//    `TargetMacroCheck.h`(이 파일 맨 위에서 포함)가 실제 컴파일러와 대조한다. 여기서 다시 판정하지 않는다.
// ------------------------------------------------------------------------------

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

// ------------------------------------------------------------------------------
// 8) 단언 가로채기 — 시험이 단언 경로를 지나가게 할 때만 쓴다
//    Debug 의 단언은 멈추므로(`SW_DEBUG_BREAK`) 단언이 걸리는 입력을 시험하면 프로세스가 죽는다. 가로채기를 건 동안은 멈추지 않고
//    센다 — 유니티 `LogAssert.Expect` · 언리얼 자동화의 기대 오류와 같은 일이다. 정의는 `Core/Common/Macros.cpp`.
// ------------------------------------------------------------------------------
#if defined( SW_DEBUG )
// 멈추는 단언(`SW_ASSERT` · 멈추는 `SW_LOG_ASSERT`)은 Debug 에만 있으므로 가로채기도 Debug 에만 둔다 — 배포본에는 시험이 단언을 끄는 창구가 없다.
namespace sw::internal
{
    /** @brief 가로채기가 걸려 있으면 단언 하나를 세고 true 입니다 — 부르는 쪽(`SW_ASSERT` · `SW_LOG_ASSERT`)은 멈추지 않습니다. */
    [[nodiscard]] SW_API bool tryCaptureAssert() noexcept;
    /** @brief 가로채기를 겁니다. 겹쳐 걸 수 있고, 건 횟수만큼 풀어야 멈춤이 돌아옵니다. */
    SW_API void beginAssertCapture() noexcept;
    /** @brief 가로채기 하나를 풉니다. */
    SW_API void endAssertCapture() noexcept;
    /** @brief 프로세스가 지금까지 가로챈 단언 수입니다(구간의 수는 시작과 끝의 차로 잰다). */
    [[nodiscard]] SW_API uint32 getCapturedAssertCount() noexcept;
} // namespace sw::internal

// ------------------------------------------------------------------------------
// 9) 단언 대화상자 — 대화형 에디터 실행에서만 App 이 건다(언리얼 ensure 대화상자). 정의는 `Core/Common/Macros.cpp`.
//    걸리지 않았으면(자동 실행 · 시험 · 에디터 없는 실행) 단언은 지금처럼 멈춘다.
// ------------------------------------------------------------------------------
namespace sw::internal
{
    /** @brief 대화상자가 고른 것입니다. */
    enum class AssertAction : uint8
    {
        Break,        ///< 디버거로 멈춘다(디버거가 없으면 크래시 리포트)
        IgnoreOnce,   ///< 이번만 넘긴다
        IgnoreAlways, ///< 이 자리(파일 · 줄)는 이 실행 동안 넘긴다
    };

    /** @brief 대화상자 함수입니다. @p pMessage 는 `SW_LOG_ASSERT` 의 메시지(없으면 nullptr)입니다. */
    using AssertDialogFunc = AssertAction ( * )( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line );

    /** @brief 대화상자를 겁니다. nullptr 이면 뗍니다(늘 멈춘다). "늘 무시" 로 고른 자리도 함께 잊습니다. */
    SW_API void setAssertDialog( AssertDialogFunc pfnDialog ) noexcept;
    /** @brief 대화상자가 걸려 있으면 true 입니다. */
    [[nodiscard]] SW_API bool hasAssertDialog() noexcept;
    /**
     * @brief 단언이 멈춰야 하면 true 입니다. 이 자리가 "늘 무시" 면 false, 대화상자가 없으면 true(지금 동작), 있으면 묻는다.
     * @details 대화상자는 한 번에 하나다 — 떠 있는 동안 다른 스레드의 단언은 잠금에서 기다린다.
     */
    [[nodiscard]] SW_API bool shouldBreakOnAssert( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line ) noexcept;
} // namespace sw::internal
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
// 10) 인라인 · 노인라인 · restrict — 핫패스 최적화 힌트
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
// 11) CPU Pause / Yield — 스핀 대기 힌트 (x64 PAUSE · arm64 YIELD)
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
