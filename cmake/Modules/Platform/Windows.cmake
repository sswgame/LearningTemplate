# Windows platform definitions (OS + sockets). GPU libs → sw_graphics_libs.

if(NOT WIN32)
    return()
endif()

add_library(sw_platform_windows INTERFACE)

target_compile_definitions(sw_platform_windows INTERFACE
    SW_PLATFORM_WINDOWS
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(sw_platform_windows INTERFACE
    ws2_32.lib
)

if(NOT TARGET sw_graphics_libs)
    add_library(sw_graphics_libs INTERFACE)
endif()
target_link_libraries(sw_graphics_libs INTERFACE
    d3d11.lib
    d3d12.lib
    dxgi.lib
    d3dcompiler.lib
    opengl32.lib
)

list(APPEND sw_flag_libraries sw_platform_windows)
