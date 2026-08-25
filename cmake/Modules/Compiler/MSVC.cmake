# ==============================================================================
# @file cmake/Modules/Compiler/MSVC.cmake
# @brief MSVC 컴파일러 플래그 INTERFACE
# ==============================================================================

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	return()
endif()

# 파일 스코프 include: 일반 set() (함수 밖에서 PARENT_SCOPE는 무효).
set(CMAKE_MSVC_PARALLEL_COMPILE ON)

add_library(sw_compiler_msvc INTERFACE)

# ------------------------------------------------------------------------------
# 1) 컴파일 — 문자셋 → 코드젠 → 경고 → 외부 → 설정
# ------------------------------------------------------------------------------
target_compile_options(sw_compiler_msvc INTERFACE
	# 문자셋
	/utf-8

	# 코드젠
	/EHsc
	/GR-
	/MP
	/bigobj

	# 경고
	/W4
	/wd4201
	/wd4251

	# 벤더 ThirdParty (SYSTEM /external)
	/external:W0
	/external:I${CMAKE_SOURCE_DIR}/ThirdParty

	# Debug
	$<$<CONFIG:Debug>:/Zi>
	$<$<CONFIG:Debug>:/Od>

	# Release
	$<$<CONFIG:Release>:/O2>
)

# ------------------------------------------------------------------------------
# 2) 링크 · 런타임 · 매크로
# ------------------------------------------------------------------------------
target_link_options(sw_compiler_msvc INTERFACE
	$<$<CONFIG:Debug>:/INCREMENTAL>
	$<$<CONFIG:Debug>:/DEBUG:FASTLINK>
	$<$<CONFIG:Release>:/INCREMENTAL:NO>
	$<$<CONFIG:Release>:/OPT:REF>
	$<$<CONFIG:Release>:/OPT:ICF>
)

set_property(TARGET sw_compiler_msvc PROPERTY
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

target_compile_definitions(sw_compiler_msvc INTERFACE SW_COMPILER_MSVC)

list(APPEND sw_flag_libraries sw_compiler_msvc)
