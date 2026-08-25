# ==============================================================================
# @file cmake/Modules/Platform/Windows.cmake
# @brief Windows 플랫폼 정의 (OS + 소켓). GPU 라이브러리 → sw_graphics_*_libs
# ==============================================================================

if(NOT WIN32)
    return()
endif()

add_library(sw_platform_windows INTERFACE)

# ------------------------------------------------------------------------------
# 1) OS 매크로 · 소켓
# ------------------------------------------------------------------------------
target_compile_definitions(sw_platform_windows INTERFACE
    SW_PLATFORM_WINDOWS
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(sw_platform_windows INTERFACE
    ws2_32.lib
)

# ------------------------------------------------------------------------------
# 2) 백엔드별 그래픽 라이브러리 — RHI MODULE / 모놀리식이 골라 링크
# ------------------------------------------------------------------------------
if(NOT TARGET sw_graphics_dx11_libs)
    add_library(sw_graphics_dx11_libs INTERFACE)
    target_link_libraries(sw_graphics_dx11_libs INTERFACE d3d11.lib d3dcompiler.lib dxgi.lib)
endif()

if(NOT TARGET sw_graphics_dx12_libs)
    add_library(sw_graphics_dx12_libs INTERFACE)
    target_link_libraries(sw_graphics_dx12_libs INTERFACE d3d12.lib d3dcompiler.lib dxgi.lib)
endif()

if(NOT TARGET sw_graphics_gl_libs)
    add_library(sw_graphics_gl_libs INTERFACE)
    target_link_libraries(sw_graphics_gl_libs INTERFACE opengl32.lib)
endif()

if(NOT TARGET sw_graphics_libs)
    add_library(sw_graphics_libs INTERFACE)
    target_link_libraries(sw_graphics_libs INTERFACE
        sw_graphics_dx11_libs
        sw_graphics_dx12_libs
        sw_graphics_gl_libs
    )
endif()

list(APPEND sw_flag_libraries sw_platform_windows)
