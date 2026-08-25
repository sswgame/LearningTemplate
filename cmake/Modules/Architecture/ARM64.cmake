# ==============================================================================
# @file cmake/Modules/Architecture/ARM64.cmake
# @brief ARM64 아키텍처 INTERFACE 매크로
# ==============================================================================

if(
    NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64"
    AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64"
)
    return()
endif()

add_library(swarchitecture_arm64 INTERFACE)
target_compile_definitions(swarchitecture_arm64 INTERFACE SW_ARM64)
list(APPEND sw_flag_libraries swarchitecture_arm64)
