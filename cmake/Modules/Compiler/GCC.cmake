# GCC compiler flags.

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    return()
endif()

add_library(sw_compiler_gcc INTERFACE)

target_compile_options(sw_compiler_gcc INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:
    -Wall
    -Wextra
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
    >
)

target_compile_definitions(sw_compiler_gcc INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:SW_COMPILER_GCC>
)

message(STATUS "[Compiler] GCC Compiler Options Configured.")

list(APPEND sw_flag_libraries sw_compiler_gcc)
