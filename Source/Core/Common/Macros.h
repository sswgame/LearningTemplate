/**
 * @file Macros.h
 * @brief Foundation 공통 매크로 (Assert, 플랫폼, SW_API 등).
 * @details 어서션, 컴파일러 분기, 인라인 강제, 플랫폼 판별.
 */
#pragma once
#include "Core/Common/Types.h"

#include <type_traits>

// ------------------------------------------------------------------------------
// 1) 전처리기 유틸리티 (Pre-processor Utilities)
// ------------------------------------------------------------------------------

#define SW_CONCAT_IMPL( x, y ) x##y
#define SW_CONCAT( x, y )	   SW_CONCAT_IMPL( x, y )

// ------------------------------------------------------------------------------
// 2) 디버그 브레이크 — 어서션 실패 시 디버거에 멈춤
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
// 2) 함수 시그니처 — 컴파일러별 pretty name
// ------------------------------------------------------------------------------
#ifdef _MSC_VER
	/** @brief MSVC 함수 시그니처 문자열입니다. */
	#define SW_FUNCTION_SIGNATURE __FUNCSIG__
#else
	/** @brief Clang/GCC pretty function 문자열입니다. */
	#define SW_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

// ------------------------------------------------------------------------------
// 3) 디버그 / 릴리즈 — 미지정 시 _DEBUG 로 판별
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
// 4) SW_ASSERT — 논리 불변식 (메시지 없음, 고빈도 경로)
//    SW_LOG_ASSERT 는 Logger.h. Release 에서는 둘 다 no-op
// ------------------------------------------------------------------------------
/**
 * 사용 가이드:
 *   SW_ASSERT(expr)        — 논리 불변식 검증 (메시지 없음, 고빈도 경로)
 *   SW_LOG_ASSERT(expr, …) — 실패 원인 추적이 필요한 곳 (Logger + break, 저빈도)
 * 두 매크로 모두 Release(SW_RELEASE)에서 no-op 입니다.
 * 프로덕션 환경에서도 반드시 확인해야 하는 조건은 직접 if-return 처리하세요.
 */
#if defined( SW_DEBUG )
	/** @brief 식이 거짓이면 디버거 브레이크입니다. Release 에서는 제거됩니다. */
	#define SW_ASSERT( expr )     \
		do                        \
		{                         \
			if ( !( expr ) )      \
			{                     \
				SW_DEBUG_BREAK(); \
			}                     \
		} while ( false )
#else
	/** @brief Release 에서는 어서션을 제거합니다. */
	#define SW_ASSERT( expr )
#endif

// ------------------------------------------------------------------------------
// 5) 플랫폼 — Windows / Linux / macOS 중 하나
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
// 6) DLL export / import — Core 는 SW_API, 게임 모듈은 SW_MODULE_API
// ------------------------------------------------------------------------------
#if defined( SW_PLATFORM_WINDOWS )
	#if defined( SW_EXPORTS )
		/** @brief Core DLL 을 빌드할 때 export 합니다. */
		#define SW_API __declspec( dllexport )
	#elif defined( SW_IMPORTS )
		/** @brief Core DLL 을 사용할 때 import 합니다. */
		#define SW_API __declspec( dllimport )
	#else
		/** @brief 정적 링크 시 장식이 없습니다. */
		#define SW_API
	#endif

	#if defined( SW_MODULE_EXPORTS )
		/** @brief 게임/에디터 모듈 DLL 을 빌드할 때 export 합니다. */
		#define SW_MODULE_API __declspec( dllexport )
	#elif defined( SW_MODULE_IMPORTS )
		/** @brief 게임/에디터 모듈 DLL 을 사용할 때 import 합니다. */
		#define SW_MODULE_API __declspec( dllimport )
	#else
		/** @brief STATIC 모듈(shipping) 또는 헤더만 참조할 때 dllimport를 쓰지 않음 */
		#define SW_MODULE_API
	#endif
#else
	/** @brief ELF/Mach-O 기본 visibility 입니다. */
	#define SW_API		  __attribute__( ( visibility( "default" ) ) )
	/** @brief 모듈 심볼도 기본 visibility 입니다. */
	#define SW_MODULE_API __attribute__( ( visibility( "default" ) ) )
#endif

/** @brief 코드 블록을 명시적으로 구분할 때 사용합니다 (세미콜론 없이: BLOCK( "..." )). */
#define BLOCK( message )

/**
 * @brief 비트마스크 비트를 만듭니다.
 * @note x >= 31 에서 signed overflow(UB)를 막기 위해 uint32 리터럴을 씁니다.
 */
#define SW_BIT( x ) ( 1u << ( x ) )

// ------------------------------------------------------------------------------
// 10-a) 비트 필드 Boolean 값 — 대입 시 의미를 명확히 합니다
//        비트 필드는 true/false 직접 대입 시 컴파일러 경고가 발생할 수 있으므로
//        SW_TRUE(1) / SW_FALSE(0) 을 사용합니다.
// ------------------------------------------------------------------------------
/** @brief 비트 필드에 참(1)을 대입할 때 사용합니다. */
#define SW_TRUE 1
/** @brief 비트 필드에 거짓(0)을 대입할 때 사용합니다. */
#define SW_FALSE 0

/** @brief 템플릿 제약(SFINAE)을 한 줄로 붙입니다. */
#define SW_REQUIRES( ... ) , std::enable_if_t<( __VA_ARGS__ ), int32> = 0

// ------------------------------------------------------------------------------
// 7) SW_COUNT_OF — 정적 배열 원소 수 (Macros.h 단독 include 가능)
//    arrayCountHelper: uint32 대신 std::size_t — Types.h 없이도 컴파일
// ------------------------------------------------------------------------------
#ifdef __clang__
/** @brief 배열 참조에서 원소 수+1 짜리 배열 타입을 추론합니다 (Clang). */
template <typename T SW_REQUIRES( __is_array( T ) )>
auto arrayCountHelper( T& t ) -> utf8 ( & )[sizeof( t ) / sizeof( t[0] ) + 1];
#else
/** @brief 배열 참조에서 원소 수+1 짜리 배열 타입을 추론합니다. */
template <typename T, size_t N>
utf8 ( &arrayCountHelper( const T ( & )[N] ) )[N + 1];
#endif

/** @brief 정적 배열의 원소 개수를 컴파일 타임에 구합니다. */
#define SW_COUNT_OF( array ) ( sizeof( arrayCountHelper( array ) ) - 1 )

// ------------------------------------------------------------------------------
// 8) 인라인 · 노인라인 · restrict — 핫패스 최적화 힌트
// ------------------------------------------------------------------------------
#if defined( _MSC_VER )
	/** @brief 강제 인라인 힌트입니다. */
	#define SW_INLINE __forceinline
	/** @brief 인라인 금지 힌트입니다. */
	#define SW_NOINLINE __declspec( noinline )
	/** @brief 포인터 alias 없음을 힌트합니다. */
	#define SW_RESTRICT __restrict
#elif defined( __GNUC__ ) || defined( __clang__ )
	/** @brief 강제 인라인 힌트입니다. */
	#define SW_INLINE	inline __attribute__( ( always_inline ) )
	/** @brief 인라인 금지 힌트입니다. */
	#define SW_NOINLINE __attribute__( ( noinline ) )
	/** @brief 포인터 alias 없음을 힌트합니다. */
	#define SW_RESTRICT __restrict__
#else
	/** @brief 일반 inline 입니다. */
	#define SW_INLINE inline
	/** @brief 인라인 금지 힌트입니다. */
	#define SW_NOINLINE
	/** @brief restrict 미지원 시 빈 매크로입니다. */
	#define SW_RESTRICT
#endif

// ------------------------------------------------------------------------------
// 9) REFLECT_SCRIPT — 리플렉션 파서 annotate (일반 컴파일에서는 제거)

// ------------------------------------------------------------------------------

#if defined( __REFLECT_PARSER__ )
	/** @brief 스크립트 리플렉션 태그를 annotate 합니다. */
	#define REFLECT_SCRIPT( ... ) __attribute__( ( annotate( "reflect-script;" #__VA_ARGS__ ) ) )
#else
	/** @brief 일반 컴파일에서는 리플렉션 태그를 제거합니다. */
	#define REFLECT_SCRIPT( ... )
#endif

// ------------------------------------------------------------------------------
// 10) CPU Pause / Yield — x86/x64, ARM/ARM64, 이기종 플랫폼 호환 스핀 대기 힌트
// ------------------------------------------------------------------------------
#if defined( _MSC_VER )
	#if defined( _M_IX86 ) || defined( _M_X64 )
		#include <emmintrin.h>
		/** @brief x86/x64 PAUSE 명령어 */
		#define SW_CPU_PAUSE() _mm_pause()
	#elif defined( _M_ARM ) || defined( _M_ARM64 ) || defined( _M_ARM64EC )
		/** @brief ARM/ARM64 YIELD 명령어 */
		#define SW_CPU_PAUSE() __yield()
	#else
		#define SW_CPU_PAUSE() ( (void)0 )
	#endif
#elif defined( __GNUC__ ) || defined( __clang__ )
	#if defined( __i386__ ) || defined( __x86_64__ )
		/** @brief x86/x64 Clang/GCC 내장 PAUSE */
		#define SW_CPU_PAUSE() __builtin_ia32_pause()
	#elif defined( __arm__ ) || defined( __aarch64__ )
		/** @brief ARM/ARM64 인라인 어셈블리 YIELD */
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
	 * @brief 이기종 CPU 아키텍처(x86/x64, ARM/ARM64)를 지원하는 스핀락/비동기 대기용 CPU 일시정지(Pause/Yield) 함수입니다.
	 * @details
	 * - **x86/x64**: `_mm_pause` / `PAUSE` 명령어를 실행하여 파이프라인 과열을 막고 전력 소모를 줄이며 메모리 오더 위반 패널티를 제거합니다.
	 * - **ARM/ARM64**: `yield` 명령어를 실행하여 SMT/멀티코어 스레드 파이프라인에 양보 힌트를 전달합니다.
	 * - **기타 아키텍처**: no-op으로 안전하게 동작합니다.
	 */
	SW_INLINE void cpuPause() noexcept
	{
		SW_CPU_PAUSE();
	}
} // namespace sw
