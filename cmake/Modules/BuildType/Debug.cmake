# Debug / Release CONFIG definitions for flag INTERFACE libs.
#
# Single-config (Ninja): CMAKE_BUILD_TYPE matches CONFIG.
# Multi-config (VS): genex selects the active configuration.

add_library(sw_build_debug INTERFACE)
target_compile_definitions(sw_build_debug INTERFACE $<$<CONFIG:Debug>:SW_DEBUG>)
list(APPEND sw_flag_libraries sw_build_debug)
