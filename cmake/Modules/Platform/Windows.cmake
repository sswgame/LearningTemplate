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
# NOMINMAX · WIN32_LEAN_AND_MEAN 은 컴파일 정의로 모든 TU 에 준다 — 헤더(`Core/Common/PlatformOsHeaders.h`)에서만 정의하면 그보다 먼저
# `windows.h` 를 include 한 TU(서드파티 헤더가 먼저 오는 파일 · PCH 를 쓰지 않는 파일)에는 min/max 매크로가 생긴다.
target_compile_definitions(sw_platform_windows INTERFACE
	SW_PLATFORM_WINDOWS
	SW_PLATFORM_NAME="Windows"  # sw::build::kPlatformName (Core/Common/BuildInfo.h)
	_CRT_SECURE_NO_WARNINGS
	NOMINMAX
	WIN32_LEAN_AND_MEAN
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
