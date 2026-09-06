# ==============================================================================
# @file cmake/Modules/Platform/MacOS.cmake
# @brief macOS 플랫폼 정의. GPU 프레임워크는 필요 시 sw_graphics_libs로 연결
# ==============================================================================

if(NOT APPLE)
    return()
endif()

add_library(sw_platform_macos INTERFACE)
target_compile_definitions(sw_platform_macos INTERFACE SW_PLATFORM_MACOS)

# ------------------------------------------------------------------------------
# 1) Cocoa · GPU INTERFACE 껍데기
# ------------------------------------------------------------------------------
find_library(COCOA_FRAMEWORK Cocoa)

if(COCOA_FRAMEWORK)
    target_link_libraries(sw_platform_macos INTERFACE ${COCOA_FRAMEWORK})
endif()

if(NOT TARGET sw_graphics_gl_libs)
    add_library(sw_graphics_gl_libs INTERFACE)
endif()

if(NOT TARGET sw_graphics_vulkan_libs)
    add_library(sw_graphics_vulkan_libs INTERFACE)
endif()

if(NOT TARGET sw_graphics_libs)
    add_library(sw_graphics_libs INTERFACE)
    target_link_libraries(sw_graphics_libs INTERFACE
        sw_graphics_gl_libs
        sw_graphics_vulkan_libs
    )
endif()

list(APPEND sw_flag_libraries sw_platform_macos)
