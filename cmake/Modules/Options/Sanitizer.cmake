# Address/UB sanitizer flags (optional).

if(NOT sw_enable_sanitizer)
    return()
endif()

add_library(sw_sanitizer INTERFACE)

target_compile_options(sw_sanitizer INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=address>
)

target_compile_options(sw_sanitizer INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-fsanitize=address,undefined>
    $<$<CXX_COMPILER_ID:Clang>:-fsanitize=address,undefined>
    $<$<CXX_COMPILER_ID:AppleClang>:-fsanitize=address,undefined>
)

target_link_options(sw_sanitizer INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-fsanitize=address,undefined>
    $<$<CXX_COMPILER_ID:Clang>:-fsanitize=address,undefined>
    $<$<CXX_COMPILER_ID:AppleClang>:-fsanitize=address,undefined>
)

list(APPEND sw_flag_libraries sw_sanitizer)
