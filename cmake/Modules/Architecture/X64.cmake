# x64 architecture definitions.

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64")
    return()
endif()

add_library(sw_architecture_x64 INTERFACE)
target_compile_definitions(sw_architecture_x64 INTERFACE SW_X64)
list(APPEND sw_flag_libraries sw_architecture_x64)
