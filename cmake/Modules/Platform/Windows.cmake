# Windows platform definitions.

if(NOT WIN32)
    return()
endif()

add_library(sw_platform_windows INTERFACE)

target_compile_definitions(sw_platform_windows INTERFACE
    SW_PLATFORM_WINDOWS
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(sw_platform_windows INTERFACE
    d3d11.lib
    d3d12.lib
    dxgi.lib
    d3dcompiler.lib
    opengl32.lib
    ws2_32.lib
)

if(NOT TARGET sw_graphics_libs)
    add_library(sw_graphics_libs INTERFACE)
    target_link_libraries(sw_graphics_libs INTERFACE sw_platform_windows)
endif()

list(APPEND sw_flag_libraries sw_platform_windows)
