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

# sccache/ccache는 SetupEnvironment.cmake(SW_USE_SCCACHE)에서만 설정한다.

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
# 2) 컴파일 — 문자셋 → 코드젠 → 경고 → 외부 → 설정
# ------------------------------------------------------------------------------
target_compile_options(sw_compiler_clang INTERFACE
	# 문자셋
	$<$<NOT:$<BOOL:${MSVC}>>:-finput-charset=UTF-8>
	$<$<NOT:$<BOOL:${MSVC}>>:-fexec-charset=UTF-8>
	$<$<AND:$<CXX_COMPILER_ID:Clang>,$<BOOL:${MSVC}>>:/utf-8>

	# 코드젠
	$<$<BOOL:${MSVC}>:/bigobj>
	$<$<BOOL:${MSVC}>:/GR->
	$<$<BOOL:${MSVC}>:/Zc:inline>
	$<$<BOOL:${MSVC}>:/Gw>
	$<$<BOOL:${MSVC}>:-clang:-fmerge-all-constants>
	$<$<BOOL:${MSVC}>:-clang:-fno-spell-checking>
	$<$<BOOL:${MSVC}>:-clang:-fdelayed-template-parsing>
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<COMPILE_LANGUAGE:CXX>>:-fno-rtti>

	# 경고 (활성화)
	-Wall
	-Wextra

	# 경고 (노이즈/의도적 비활성)
	-Wno-c++98-compat
	-Wno-c++98-compat-pedantic
	-Wno-pre-c++17-compat
	-Wno-unsafe-buffer-usage
	-Wno-global-constructors
	-Wno-exit-time-destructors
	-Wno-padded
	-Wno-switch-default
	-Wno-covered-switch-default
	-Wno-format-nonliteral
	-Wno-nonportable-include-path
	-Wno-cast-function-type-strict
	-Wno-invalid-offsetof
	-Wno-unused-command-line-argument
	$<$<BOOL:${MSVC}>:-Qunused-arguments>
	$<$<BOOL:${MSVC}>:-Wno-language-extension-token>

	# 벤더 ThirdParty (clang-cl /external; SYSTEM은 위의 /imsvc 사용)
	$<$<BOOL:${MSVC}>:/external:W0>
	$<$<BOOL:${MSVC}>:/external:I${CMAKE_SOURCE_DIR}/ThirdParty>

	# Debug
	$<$<CONFIG:Debug>:-g>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:/Z7>
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Debug>>:-O0>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:/Od>

	# Release
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Release>>:-O3>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/O2>
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Release>>:-mavx2>
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
