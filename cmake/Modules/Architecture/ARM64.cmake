# ==============================================================================
# @file cmake/Modules/Architecture/ARM64.cmake
# @brief ARM64 아키텍처 INTERFACE 매크로 (판정은 DetectArchitecture.cmake)
# ==============================================================================

if(NOT sw_target_architecture STREQUAL "arm64")
    return()
endif()

add_library(sw_architecture_arm64 INTERFACE)
target_compile_definitions(sw_architecture_arm64 INTERFACE SW_ARM64)
list(APPEND sw_flag_libraries sw_architecture_arm64)
