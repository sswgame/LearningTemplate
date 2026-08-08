# macOS platform definitions. GPU frameworks → sw_graphics_libs when needed.

if(NOT APPLE)
    return()
endif()

add_library(sw_platform_macos INTERFACE)
target_compile_definitions(sw_platform_macos INTERFACE SW_PLATFORM_MACOS)

find_library(COCOA_FRAMEWORK Cocoa)
if(COCOA_FRAMEWORK)
    target_link_libraries(sw_platform_macos INTERFACE ${COCOA_FRAMEWORK})
endif()

if(NOT TARGET sw_graphics_libs)
    add_library(sw_graphics_libs INTERFACE)
endif()

list(APPEND sw_flag_libraries sw_platform_macos)
