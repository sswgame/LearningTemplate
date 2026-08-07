#pragma once

/**
 * @file CommonMacros.h
 * @brief 엔진 전체에서 공통으로 사용되는 매크로 정의 헤더입니다.
 * @details 어서션(Assert), 컴파일러 분기, 인라인 강제화, 플랫폼 판별 등의 기능을 제공합니다.
 */

// ============================================================================
// [디버그 브레이크]
// ============================================================================
#if defined( _MSC_VER )
	/** @brief SW_DEBUG_BREAK 매크로 정의입니다. */
	#define SW_DEBUG_BREAK() __debugbreak()
#elif defined( __clang__ ) || defined( __GNUC__ ) || defined( __GNUG__ )
	/** @brief SW_DEBUG_BREAK 매크로 정의입니다. */
	#define SW_DEBUG_BREAK() __builtin_trap()
#else
	#error "SW_DEBUG_BREAK is not supported on this compiler or platform."
#endif

// ============================================================================
// [함수 시그니처 판별]
// ============================================================================
#ifdef _MSC_VER
	/** @brief SW_FUNCTION_SIGNATURE 매크로 정의입니다. */
	#define SW_FUNCTION_SIGNATURE __FUNCSIG__
#else
	/** @brief SW_FUNCTION_SIGNATURE 매크로 정의입니다. */
	#define SW_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

// ============================================================================
// [디버그/릴리즈 환경 판별]
// ============================================================================
#if !defined( SW_DEBUG ) && !defined( SW_RELEASE )
	#if defined( _DEBUG ) || defined( DEBUG )
		/** @brief SW_DEBUG 매크로 정의입니다. */
		#define SW_DEBUG
	#else
		/** @brief SW_RELEASE 매크로 정의입니다. */
		#define SW_RELEASE
	#endif
#endif

// ============================================================================
// [커스텀 어서션 (SW_ASSERT)]
// ============================================================================
#if defined( SW_DEBUG )
	/** @brief SW_ASSERT 매크로 정의입니다. */
	#define SW_ASSERT( expr )     \
		do                        \
		{                         \
			if ( !( expr ) )      \
			{                     \
				SW_DEBUG_BREAK(); \
			}                     \
		} while ( false )
#else
	/** @brief SW_ASSERT 매크로 정의입니다. */
	#define SW_ASSERT( expr )
#endif

// ============================================================================
// [플랫폼 식별 및 OS 매크로]
// ============================================================================
#if !defined( SW_PLATFORM_WINDOWS ) && !defined( SW_PLATFORM_LINUX ) && !defined( SW_PLATFORM_MACOS )
	#if defined( _WIN32 ) || defined( _WIN64 )
		/** @brief SW_PLATFORM_WINDOWS 매크로 정의입니다. */
		#define SW_PLATFORM_WINDOWS
	#elif defined( __linux__ )
		/** @brief SW_PLATFORM_LINUX 매크로 정의입니다. */
		#define SW_PLATFORM_LINUX
	#elif defined( __APPLE__ )
		/** @brief SW_PLATFORM_MACOS 매크로 정의입니다. */
		#define SW_PLATFORM_MACOS
	#else
		#error "Unknown target platform."
	#endif
#endif

// ============================================================================
// [DLL 동적 링킹 Export/Import]
// ============================================================================
#if defined( SW_PLATFORM_WINDOWS )
	#if defined( SW_EXPORTS )
		/** @brief SW_API 매크로 정의입니다. */
		#define SW_API __declspec( dllexport )
	#elif defined( SW_IMPORTS )
		/** @brief SW_API 매크로 정의입니다. */
		#define SW_API __declspec( dllimport )
	#else
		/** @brief SW_API 매크로 정의입니다. */
		#define SW_API
	#endif

	#if defined( SW_MODULE_EXPORTS )
		#define SW_MODULE_API __declspec( dllexport )
	#elif defined( SW_MODULE_IMPORTS )
		#define SW_MODULE_API __declspec( dllimport )
	#else
		/** STATIC 모듈(shipping) 또는 헤더만 참조할 때 dllimport를 쓰지 않음 */
		#define SW_MODULE_API
	#endif
#else
	/** @brief SW_API 매크로 정의입니다. */
	#define SW_API __attribute__( ( visibility( "default" ) ) )
	#define SW_MODULE_API __attribute__( ( visibility( "default" ) ) )
#endif

/** @brief 코드 블록을 명시적으로 구분할 때 사용하는 매크로 (세미콜론 없이 사용: BLOCK( "..." )) */
#define BLOCK( message )

/** @brief 비트마스크 생성을 위한 헬퍼 매크로 */
#define SW_BIT( x ) ( 1 << x )

/** @brief 템플릿 제약(SFINAE) 설정 시 가독성을 높이기 위한 매크로 */
#define SW_REQUIRES( ... ) , std::enable_if_t<( __VA_ARGS__ ), int32> = 0

// ============================================================================
// [배열 크기 계산 헬퍼 (SW_COUNT_OF)]
// ============================================================================
#ifdef __clang__
template <typename T SW_REQUIRES( __is_array( T ) )>
auto arrayCountHelper( T& t ) -> char ( & )[sizeof( t ) / sizeof( t[0] ) + 1];
#else
template <typename T, uint32 N>
char ( &arrayCountHelper( const T ( & )[N] ) )[N + 1];
#endif

/** @brief 정적 배열의 원소 개수를 컴파일 타임에 반환합니다. */
#define SW_COUNT_OF( array ) ( sizeof( arrayCountHelper( array ) ) - 1 )

// ============================================================================
// [구조체 멤버 오프셋 계산]
// ============================================================================
#ifdef __clang__
	/** @brief SW_OFFSET_OF 매크로 정의입니다. */
	#define SW_OFFSET_OF( struc, member ) __builtin_offsetof( struc, member )
#else
	/** @brief SW_OFFSET_OF 매크로 정의입니다. */
	#define SW_OFFSET_OF( struc, member ) offsetof( struc, member )
#endif

// ============================================================================
// [컴파일러 최적화 힌트 매크로]
// ============================================================================
#if defined( _MSC_VER )
	/** @brief SW_INLINE 매크로 정의입니다. */
	#define SW_INLINE		 __forceinline
	/** @brief SW_RESTRICT 매크로 정의입니다. */
	#define SW_RESTRICT		 __restrict
	/** @brief SW_LIKELY 매크로 정의입니다. */
	#define SW_LIKELY( x )	 ( x )
	/** @brief SW_UNLIKELY 매크로 정의입니다. */
	#define SW_UNLIKELY( x ) ( x )
#elif defined( __GNUC__ ) || defined( __clang__ )
	/** @brief SW_INLINE 매크로 정의입니다. */
	#define SW_INLINE		 __attribute__( ( always_inline ) ) inline
	/** @brief SW_RESTRICT 매크로 정의입니다. */
	#define SW_RESTRICT		 __restrict__
	/** @brief SW_LIKELY 매크로 정의입니다. */
	#define SW_LIKELY( x )	 __builtin_expect( !!( x ), 1 )
	/** @brief SW_UNLIKELY 매크로 정의입니다. */
	#define SW_UNLIKELY( x ) __builtin_expect( !!( x ), 0 )
#else
	/** @brief SW_INLINE 매크로 정의입니다. */
	#define SW_INLINE inline
	/** @brief SW_RESTRICT 매크로 정의입니다. */
	#define SW_RESTRICT
	/** @brief SW_LIKELY 매크로 정의입니다. */
	#define SW_LIKELY( x )	 ( x )
	/** @brief SW_UNLIKELY 매크로 정의입니다. */
	#define SW_UNLIKELY( x ) ( x )
#endif

// ============================================================================
// [컴파일러 경고 억제 매크로]
// ============================================================================
#if defined( _MSC_VER )
	/** @brief SW_DISABLE_WARNING_PUSH 매크로 정의입니다. */
	#define SW_DISABLE_WARNING_PUSH \
		__pragma( warning( push ) )
	/** @brief SW_DISABLE_WARNING_POP 매크로 정의입니다. */
	#define SW_DISABLE_WARNING_POP \
		__pragma( warning( pop ) )
	/** @brief SW_DISABLE_WARNING 매크로 정의입니다. */
	#define SW_DISABLE_WARNING( warningNumber ) \
		__pragma( warning( disable \
						   : warningNumber ) )
#elif defined( __GNUC__ ) || defined( __clang__ )
	/** @brief SW_PRAGMA 매크로 정의입니다. */
	#define SW_PRAGMA( X ) _Pragma( #X )
	/** @brief SW_DISABLE_WARNING_PUSH 매크로 정의입니다. */
	#define SW_DISABLE_WARNING_PUSH SW_PRAGMA( GCC diagnostic push )
	/** @brief SW_DISABLE_WARNING_POP 매크로 정의입니다. */
	#define SW_DISABLE_WARNING_POP	SW_PRAGMA( GCC diagnostic pop )
	/** @brief SW_DISABLE_WARNING 매크로 정의입니다. */
	#define SW_DISABLE_WARNING( warningName ) \
		SW_PRAGMA( GCC diagnostic ignored #warningName )
#else
	/** @brief SW_DISABLE_WARNING_PUSH 매크로 정의입니다. */
	#define SW_DISABLE_WARNING_PUSH
	/** @brief SW_DISABLE_WARNING_POP 매크로 정의입니다. */
	#define SW_DISABLE_WARNING_POP
	/** @brief SW_DISABLE_WARNING 매크로 정의입니다. */
	#define SW_DISABLE_WARNING( warningNumber )
#endif
