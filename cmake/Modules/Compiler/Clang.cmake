# Clang / AppleClang compiler flags.

if(
	NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
	AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
)
	return()
endif()

find_program(SW_COMPILER_LAUNCHER NAMES sccache ccache HINTS "$ENV{PATH}")
if(SW_COMPILER_LAUNCHER)
	message(STATUS "[Clang.cmake] Compiler Cache launcher detected: ${SW_COMPILER_LAUNCHER}")
	# File-scope include: plain set() (PARENT_SCOPE is a no-op outside a function).
	set(CMAKE_C_COMPILER_LAUNCHER "${SW_COMPILER_LAUNCHER}")
	set(CMAKE_CXX_COMPILER_LAUNCHER "${SW_COMPILER_LAUNCHER}")
endif()

add_library(sw_compiler_clang INTERFACE)

# SYSTEM include flag for target_include_directories(... SYSTEM ...)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_C "/imsvc ")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/imsvc ")
else()
	set(CMAKE_INCLUDE_SYSTEM_FLAG_C "-isystem ")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-isystem ")
endif()

# -----------------------------------------------------------------------------
# Compile: charset → codegen → warnings → external → config
# MSVC=1 means clang-cl (MSVC-compatible driver).
# -----------------------------------------------------------------------------
target_compile_options(sw_compiler_clang INTERFACE
	# Charset
	$<$<NOT:$<BOOL:${MSVC}>>:-finput-charset=UTF-8>
	$<$<NOT:$<BOOL:${MSVC}>>:-fexec-charset=UTF-8>
	$<$<AND:$<CXX_COMPILER_ID:Clang>,$<BOOL:${MSVC}>>:/utf-8>

	# Codegen
	$<$<BOOL:${MSVC}>:/bigobj>

	# Warnings (enable)
	-Wall
	-Wextra

	# Warnings (disable noisy / intentional)
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

	# Vendored ThirdParty (clang-cl /external; SYSTEM uses /imsvc above)
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

# -----------------------------------------------------------------------------
# Link
# -----------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/../Toolchain/FindLlvmBin.cmake")
sw_find_llvm_bin(_llvm_bin)
find_program(SW_LLD_LINK_EXE NAMES lld-link lld HINTS "${_llvm_bin}")
if(SW_LLD_LINK_EXE)
	message(STATUS "[Clang.cmake] LLD Fast Linker detected: ${SW_LLD_LINK_EXE}")
	target_link_options(sw_compiler_clang INTERFACE "-fuse-ld=lld")
endif()

target_link_options(sw_compiler_clang INTERFACE
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:/INCREMENTAL>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:/DEBUG:FASTLINK>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/INCREMENTAL:NO>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/OPT:REF>
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/OPT:ICF>
)

target_compile_definitions(sw_compiler_clang INTERFACE SW_COMPILER_CLANG)

list(APPEND sw_flag_libraries sw_compiler_clang)
