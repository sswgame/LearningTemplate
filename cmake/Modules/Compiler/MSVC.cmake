# MSVC compiler flags.

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    return()
endif()

set(CMAKE_MSVC_PARALLEL_COMPILE ON PARENT_SCOPE)

add_library(sw_compiler_msvc INTERFACE)

target_compile_options(sw_compiler_msvc INTERFACE /utf-8 /W4 /EHsc /MP /bigobj /wd4201 /wd4251)

target_compile_options(sw_compiler_msvc INTERFACE
    $<$<CONFIG:Debug>:/Zi>
    $<$<CONFIG:Debug>:/Od>
)

target_compile_options(sw_compiler_msvc INTERFACE
    $<$<CONFIG:Release>:/O2>
)

target_link_options(sw_compiler_msvc INTERFACE
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Debug>:/DEBUG:FASTLINK>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
)

set_property(TARGET sw_compiler_msvc PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

target_compile_definitions(sw_compiler_msvc INTERFACE SW_COMPILER_MSVC)

list(APPEND sw_flag_libraries sw_compiler_msvc)
