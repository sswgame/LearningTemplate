# ==============================================================================
# @file cmake/Modules/Architecture/X64.cmake
# @brief x64 아키텍처 INTERFACE 매크로 (판정은 DetectArchitecture.cmake)
# ==============================================================================

if(NOT sw_target_architecture STREQUAL "x64")
	return()
endif()

add_library(sw_architecture_x64 INTERFACE)
target_compile_definitions(sw_architecture_x64 INTERFACE SW_X64)
list(APPEND sw_flag_libraries sw_architecture_x64)
