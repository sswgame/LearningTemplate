/**
 * @file TargetMacroCheck.h
 * @brief CMake 가 정한 타깃 매크로(플랫폼 · 아키텍처 · 컴파일러)가 이 TU 를 컴파일하는 컴파일러와 맞는지 검사합니다.
 * @details 코드는 `SW_PLATFORM_*` · `SW_X64` / `SW_ARM64` · `SW_COMPILER_*` 만 읽습니다. 빌드 타깃 종류는 `SW_WITH_CLIENT_CODE` · `SW_WITH_SERVER_CODE` 입니다. 컴파일러 내장 매크로(`_WIN32` · `_MSC_VER` ·
 *          `__clang__` · `__x86_64__` …)를 읽는 곳은 **이 파일 하나뿐**입니다(`CheckTargetMacros` 게이트). 판정은
 *          `cmake/Modules/{Platform,Architecture,Compiler}/` 가 하고, 여기서는 그 판정이 실제 컴파일러와 어긋나거나 빠지면 빌드를 세웁니다.
 *
 *          - 컴파일러: clang-cl 은 `__clang__` 과 `_MSC_VER` 를 둘 다 정의하고 `SW_COMPILER_CLANG` 를 받습니다. `SW_COMPILER_MSVC` 는
 *            cl.exe 뿐입니다. 그러니 "MSVC 확장(`__forceinline` · `__FUNCSIG__` · `__debugbreak` · `__declspec`)을 쓸 수 있는가" 를
 *            `SW_COMPILER_MSVC` 로 물으면 clang-cl 에서 틀립니다.
 *          - Windows 는 MS ABI 툴체인(cl · clang-cl)으로만 짓습니다(`.lib` 링크 · `/DELAYLOAD` · `delayimp`). 아래에서 그것을 검사하므로
 *            MSVC 확장을 쓸 수 있는지는 `SW_PLATFORM_WINDOWS` 로 묻습니다.
 *          - 엔진은 64 비트(x64 · arm64)만 짓습니다.
 *          - libclang 으로 헤더를 읽는 `ReflectionParser` 는 CMake 를 거치지 않으므로 같은 매크로를 스스로 넘깁니다(`ParserConfig::load`).
 */
#pragma once
// ------------------------------------------------------------------------------
// 1) 플랫폼 — Windows / Linux 중 정확히 하나(macOS 는 지원하지 않는다)
// ------------------------------------------------------------------------------

#if ( defined( SW_PLATFORM_WINDOWS ) + defined( SW_PLATFORM_LINUX ) ) != 1
    #error "Exactly one of SW_PLATFORM_WINDOWS / SW_PLATFORM_LINUX must be defined (cmake/Modules/Platform via sw_global_options)."
#endif

#if defined( SW_PLATFORM_WINDOWS ) && !defined( _WIN32 )
    #error "SW_PLATFORM_WINDOWS is defined but the compiler does not target Windows (_WIN32)."
#endif

#if defined( SW_PLATFORM_WINDOWS ) && !defined( _MSC_VER )
    #error "SW_PLATFORM_WINDOWS requires an MSVC-ABI toolchain (cl or clang-cl, _MSC_VER); MinGW is not supported."
#endif

#if defined( SW_PLATFORM_LINUX ) && !defined( __linux__ )
    #error "SW_PLATFORM_LINUX is defined but the compiler does not target Linux (__linux__)."
#endif

// ------------------------------------------------------------------------------
// 2) 아키텍처 — x64 / arm64 중 정확히 하나
// ------------------------------------------------------------------------------
#if ( defined( SW_X64 ) + defined( SW_ARM64 ) ) != 1
    #error "Exactly one of SW_X64 / SW_ARM64 must be defined (cmake/Modules/Architecture via sw_global_options)."
#endif

#if defined( SW_X64 ) && !( defined( _M_X64 ) || defined( __x86_64__ ) )
    #error "SW_X64 is defined but the compiler does not target x86-64 (_M_X64 / __x86_64__)."
#endif

#if defined( SW_ARM64 ) && !( defined( _M_ARM64 ) || defined( __aarch64__ ) )
    #error "SW_ARM64 is defined but the compiler does not target AArch64 (_M_ARM64 / __aarch64__)."
#endif

// ------------------------------------------------------------------------------
// 3) 컴파일러 — Clang(clang-cl 포함) / MSVC(cl.exe) / GCC 중 정확히 하나
// ------------------------------------------------------------------------------
#if ( defined( SW_COMPILER_CLANG ) + defined( SW_COMPILER_MSVC ) + defined( SW_COMPILER_GCC ) ) != 1
    #error "Exactly one of SW_COMPILER_CLANG / SW_COMPILER_MSVC / SW_COMPILER_GCC must be defined (cmake/Modules/Compiler via sw_global_options)."
#endif

#if defined( SW_COMPILER_CLANG ) && !defined( __clang__ )
    #error "SW_COMPILER_CLANG is defined but the compiler is not Clang (__clang__)."
#endif

#if defined( SW_COMPILER_MSVC ) && !( defined( _MSC_VER ) && !defined( __clang__ ) )
    #error "SW_COMPILER_MSVC is defined but the compiler is not cl.exe (_MSC_VER without __clang__)."
#endif

#if defined( SW_COMPILER_GCC ) && !( defined( __GNUC__ ) && !defined( __clang__ ) )
    #error "SW_COMPILER_GCC is defined but the compiler is not GCC (__GNUC__ without __clang__)."
#endif

// ------------------------------------------------------------------------------
// 4) 빌드 타깃 종류 — 클라이언트 · 서버 코드 중 하나 이상(Game 은 둘 다)
// ------------------------------------------------------------------------------
#if !defined( SW_WITH_CLIENT_CODE ) && !defined( SW_WITH_SERVER_CODE )
    #error "At least one of SW_WITH_CLIENT_CODE / SW_WITH_SERVER_CODE must be defined (SW_TARGET_TYPE via sw_global_options)."
#endif
