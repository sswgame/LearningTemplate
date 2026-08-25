# ==============================================================================
# @file cmake/Modules/Compiler/Clang.cmake
# @brief Clang / AppleClang 컴파일러 플래그 INTERFACE
# ==============================================================================

if(
	NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
	AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
)
	return()
endif()

# sccache/ccache는 DetectToolchain.cmake(SW_USE_SCCACHE)에서만 설정한다.

add_library(sw_compiler_clang INTERFACE)

# ------------------------------------------------------------------------------
# 1) SYSTEM include 플래그 — target_include_directories(... SYSTEM ...)용
#    MSVC=1 이면 clang-cl (MSVC 호환 드라이버) → /imsvc
# ------------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_C "/imsvc ")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/imsvc ")
else()
	set(CMAKE_INCLUDE_SYSTEM_FLAG_C "-isystem ")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-isystem ")
endif()

# ------------------------------------------------------------------------------
# 2) 컴파일 — 문자셋 → 코드젠 → 경고 → 서드파티 → 빌드설정(Debug/Release)
# ------------------------------------------------------------------------------
target_compile_options(sw_compiler_clang INTERFACE
	# 문자셋 (Clang GNU vs Clang-cl MSVC)
	$<$<NOT:$<BOOL:${MSVC}>>:-finput-charset=UTF-8>
	$<$<NOT:$<BOOL:${MSVC}>>:-fexec-charset=UTF-8>
	$<$<AND:$<CXX_COMPILER_ID:Clang>,$<BOOL:${MSVC}>>:/utf-8>

	# 코드젠 / 최적화 기본 설정
	$<$<BOOL:${MSVC}>:/bigobj>
	$<$<BOOL:${MSVC}>:/GR->
	$<$<BOOL:${MSVC}>:/Gw>
	$<$<BOOL:${MSVC}>:/Zc:inline>
	$<$<BOOL:${MSVC}>:-clang:-fdelayed-template-parsing>
	$<$<BOOL:${MSVC}>:-clang:-fmerge-all-constants>
	$<$<BOOL:${MSVC}>:-clang:-fno-spell-checking>
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<COMPILE_LANGUAGE:CXX>>:-fno-rtti>

	# 경고 활성화
	-Wall
	-Wextra

	# 경고 비활성화 (알파벳 정렬)
	-Wno-c++98-compat
	-Wno-c++98-compat-pedantic
	-Wno-cast-function-type-strict
	-Wno-covered-switch-default
	-Wno-exit-time-destructors
	-Wno-format-nonliteral
	-Wno-global-constructors
	-Wno-invalid-offsetof
	-Wno-nonportable-include-path
	-Wno-padded
	-Wno-pre-c++17-compat
	-Wno-switch-default
	-Wno-unsafe-buffer-usage
	-Wno-unused-command-line-argument
	$<$<BOOL:${MSVC}>:-Qunused-arguments>
	$<$<BOOL:${MSVC}>:-Wno-language-extension-token>

	# 서드파티 외부 헤더 경고 억제 (clang-cl /external; SYSTEM은 위의 /imsvc 사용)
	$<$<BOOL:${MSVC}>:/external:I${CMAKE_SOURCE_DIR}/ThirdParty>
	$<$<BOOL:${MSVC}>:/external:W0>

	# 빌드 설정별 최적화 및 디버그 심볼
	# Debug: 디버그 심볼 & 최적화 끄기
	$<$<CONFIG:Debug>:-g>
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Debug>>:-O0>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:/Od>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:/Z7>

	# Release: 최고 최적화 & AVX2 활성화
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Release>>:-O3>
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Release>>:-mavx2>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/O2>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/arch:AVX2>
)

# ------------------------------------------------------------------------------
# 3) 링크 — LLD가 있으면 사용, clang-cl은 MSVC 스타일 링크 옵션
# ------------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/../Toolchain/FindLlvmBin.cmake")
sw_findLlvmBin(llvmBin)
find_program(SW_LLD_LINK_EXE NAMES lld-link lld HINTS "${llvmBin}")
if(SW_LLD_LINK_EXE)
	message(STATUS "[Clang.cmake] LLD Fast Linker detected: ${SW_LLD_LINK_EXE}")
endif()

target_link_options(sw_compiler_clang INTERFACE
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:/INCREMENTAL:NO>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/OPT:REF>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/OPT:ICF>
)

target_compile_definitions(sw_compiler_clang INTERFACE SW_COMPILER_CLANG)

list(APPEND sw_flag_libraries sw_compiler_clang)
