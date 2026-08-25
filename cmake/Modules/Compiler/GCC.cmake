# ==============================================================================
# @file cmake/Modules/Compiler/GCC.cmake
# @brief GCC 컴파일러 플래그 INTERFACE
# ==============================================================================

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	return()
endif()

add_library(sw_compiler_gcc INTERFACE)

# ------------------------------------------------------------------------------
# 1) 컴파일 — 문자셋 → 코드젠 → 경고
# ------------------------------------------------------------------------------
target_compile_options(sw_compiler_gcc INTERFACE
	$<$<COMPILE_LANGUAGE:CXX>:
		# 문자셋
		-finput-charset=UTF-8
		-fexec-charset=UTF-8

		# 코드젠
		-fno-rtti

		# 경고
		-Wall
		-Wextra
	>
)

target_compile_definitions(sw_compiler_gcc INTERFACE
	$<$<COMPILE_LANGUAGE:CXX>:SW_COMPILER_GCC>
)

message(STATUS "[Compiler] GCC Compiler Options Configured.")

list(APPEND sw_flag_libraries sw_compiler_gcc)
