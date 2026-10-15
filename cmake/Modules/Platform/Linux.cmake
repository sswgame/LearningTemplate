# ==============================================================================
# @file cmake/Modules/Platform/Linux.cmake
# @brief Linux 플랫폼 정의 (find_library, RPATH, INTERFACE)
# @note GPU(OpenGL/GLX/Vulkan) → sw_graphics_libs
# X11/xcb는 플랫폼(윈도잉/WSI)에 유지
# ==============================================================================

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	return()
endif()

add_library(sw_platform_linux INTERFACE)
target_compile_definitions(sw_platform_linux INTERFACE SW_PLATFORM_LINUX SW_PLATFORM_NAME="Linux")  # 이름은 sw::build::kPlatformName

# 전용 서버(SW_TARGET_TYPE=Server)는 창이 없다 — X11 · xcb 를 찾지도 링크하지도 않는다. 서버 기계에는 X 가 없고, 링크해 두면 동적 링커가
# main 전에 libX11.so 를 찾다 실패한다. X11 을 쓰는 .cpp 는 `SW_WITH_CLIENT_CODE` 로 서버 빌드에서 빠진다(Window · Input 의 Linux 폴더).
if(NOT SW_TARGET_TYPE STREQUAL "Server")
	# ------------------------------------------------------------------------------
	# 1) X11 / xcb — 윈도잉 + WSLg Vulkan WSI (XGetXCBConnection / VK_KHR_xcb_surface)
	# ------------------------------------------------------------------------------
	# 클라이언트 빌드에는 모두 필수다 — 없으면 구성을 세운다(예전에는 경고만 남기고 링크 단계에서야 섰다).
	# 패키지: libx11-dev · libxcb1-dev · libx11-xcb-dev(.github/actions/setup-build-env 가 깐다).
	find_package(X11 REQUIRED)
	target_link_libraries(sw_platform_linux INTERFACE ${X11_LIBRARIES})
	target_include_directories(sw_platform_linux INTERFACE ${X11_INCLUDE_DIR})

	find_path(SW_XCB_INCLUDE_DIR NAMES xcb/xcb.h REQUIRED)
	find_library(SW_XCB_LIBRARY NAMES xcb REQUIRED)
	find_library(SW_X11_XCB_LIBRARY NAMES X11-xcb REQUIRED)
	target_include_directories(sw_platform_linux INTERFACE ${SW_XCB_INCLUDE_DIR})
	target_link_libraries(sw_platform_linux INTERFACE ${SW_XCB_LIBRARY} ${SW_X11_XCB_LIBRARY})
endif()

# ------------------------------------------------------------------------------
# 2) GPU — OpenGL/GLX + Vulkan 로더 → sw_graphics_*_libs / sw_graphics_libs
# Engine PUBLIC 링크로만 전파 (tools/tests는 미링크)
# Vulkan: vcpkg vulkan-loader[xcb,xlib], 없으면 시스템 폴백
# ------------------------------------------------------------------------------
if(NOT TARGET sw_graphics_gl_libs)
	add_library(sw_graphics_gl_libs INTERFACE)
endif()

# GL · Vulkan 로더는 필수다 — 못 찾으면 구성을 세운다(예전에는 경고만 남기고 RHI 링크에서야 섰다). 아래 갈래는 폴백이 아니라
# FindOpenGL · FindVulkan 이 시스템마다 다른 모양(타깃 · 변수 · GLVND)으로 답하는 것을 받는 자리다.
find_package(OpenGL REQUIRED)

if(OpenGL_FOUND)
	if(TARGET OpenGL::GL)
		target_link_libraries(sw_graphics_gl_libs INTERFACE OpenGL::GL)
	elseif(OPENGL_gl_LIBRARY)
		target_link_libraries(sw_graphics_gl_libs INTERFACE ${OPENGL_gl_LIBRARY})
	endif()

	if(TARGET OpenGL::GLX)
		target_link_libraries(sw_graphics_gl_libs INTERFACE OpenGL::GLX)
	elseif(OPENGL_opengl_LIBRARY AND NOT TARGET OpenGL::GL)
		target_link_libraries(sw_graphics_gl_libs INTERFACE ${OPENGL_opengl_LIBRARY})
	else()
		find_library(SW_GLX_LIBRARY NAMES GLX glx REQUIRED)
		target_link_libraries(sw_graphics_gl_libs INTERFACE ${SW_GLX_LIBRARY})
	endif()
endif()

if(NOT TARGET sw_graphics_vulkan_libs)
	add_library(sw_graphics_vulkan_libs INTERFACE)
endif()

find_package(Vulkan QUIET)

if(TARGET Vulkan::Vulkan)
	target_link_libraries(sw_graphics_vulkan_libs INTERFACE Vulkan::Vulkan)
elseif(Vulkan_LIBRARIES)
	target_link_libraries(sw_graphics_vulkan_libs INTERFACE ${Vulkan_LIBRARIES})
else()
	# FindVulkan 은 헤더까지 찾아야 답한다 — 헤더는 vcpkg 가 주고 로더만 시스템에 있는 경우 라이브러리를 직접 찾는다.
	find_library(SW_SYSTEM_VULKAN_LIBRARY NAMES vulkan REQUIRED)
	target_link_libraries(sw_graphics_vulkan_libs INTERFACE ${SW_SYSTEM_VULKAN_LIBRARY})
	message(STATUS "[Linux] Vulkan loader (system): ${SW_SYSTEM_VULKAN_LIBRARY}")
endif()

if(NOT TARGET sw_graphics_libs)
	add_library(sw_graphics_libs INTERFACE)
	target_link_libraries(sw_graphics_libs INTERFACE
		sw_graphics_gl_libs
		sw_graphics_vulkan_libs
	)
endif()

list(APPEND sw_flag_libraries sw_platform_linux)
