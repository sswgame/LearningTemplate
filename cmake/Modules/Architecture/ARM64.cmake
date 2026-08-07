# ARM64 architecture definitions.

if(
    NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64"
    AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64"
)
    return()
endif()

add_library(sw_architecture_arm64 INTERFACE)
target_compile_definitions(sw_architecture_arm64 INTERFACE SW_ARM64)
list(APPEND sw_flag_libraries sw_architecture_arm64)
