# ==============================================================================
# @file cmake/Modules/Platform/Linux.cmake
# @brief Linux 플랫폼 정의 (find_library, RPATH, INTERFACE)
# @note GPU(OpenGL/GLX/Vulkan) → sw_graphics_libs
#       X11/xcb는 플랫폼(윈도잉/WSI)에 유지
# ==============================================================================

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

add_library(sw_platform_linux INTERFACE)
target_compile_definitions(sw_platform_linux INTERFACE SW_PLATFORM_LINUX)

# ------------------------------------------------------------------------------
# 1) X11 / xcb — 윈도잉 + WSLg Vulkan WSI (XGetXCBConnection / VK_KHR_xcb_surface)
# ------------------------------------------------------------------------------
find_package(X11)
if(X11_FOUND)
    target_link_libraries(sw_platform_linux INTERFACE ${X11_LIBRARIES})
    target_include_directories(sw_platform_linux INTERFACE ${X11_INCLUDE_DIR})
endif()

find_path(SW_XCB_INCLUDE_DIR NAMES xcb/xcb.h)
find_library(SW_XCB_LIBRARY NAMES xcb)
find_library(SW_X11_XCB_LIBRARY NAMES X11-xcb)
if(SW_XCB_INCLUDE_DIR)
    target_include_directories(sw_platform_linux INTERFACE ${SW_XCB_INCLUDE_DIR})
endif()
if(SW_XCB_LIBRARY)
    target_link_libraries(sw_platform_linux INTERFACE ${SW_XCB_LIBRARY})
endif()
if(SW_X11_XCB_LIBRARY)
    target_link_libraries(sw_platform_linux INTERFACE ${SW_X11_XCB_LIBRARY})
endif()
if(NOT SW_XCB_INCLUDE_DIR OR NOT SW_XCB_LIBRARY)
    message(WARNING "[Linux] libxcb headers/libs not found — install libxcb1-dev (Vulkan xcb WSI)")
endif()
if(NOT SW_X11_XCB_LIBRARY)
    message(WARNING "[Linux] libX11-xcb not found — install libx11-xcb-dev (XGetXCBConnection / WSLg Vulkan)")
endif()

# ------------------------------------------------------------------------------
# 2) GPU — OpenGL/GLX + Vulkan 로더 → sw_graphics_*_libs / sw_graphics_libs
#    Engine PUBLIC 링크로만 전파 (tools/tests는 미링크)
#    Vulkan: vcpkg vulkan-loader[xcb,xlib], 없으면 시스템 폴백
# ------------------------------------------------------------------------------
if(NOT TARGET sw_graphics_gl_libs)
    add_library(sw_graphics_gl_libs INTERFACE)
endif()

find_package(OpenGL)
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
        find_library(SW_GLX_LIBRARY NAMES GLX glx)
        if(SW_GLX_LIBRARY)
            target_link_libraries(sw_graphics_gl_libs INTERFACE ${SW_GLX_LIBRARY})
        endif()
    endif()
else()
    message(WARNING "[Linux] OpenGL not found — GL/GLX RHI link may fail")
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
    find_library(SW_SYSTEM_VULKAN_LIBRARY NAMES vulkan)
    if(SW_SYSTEM_VULKAN_LIBRARY)
        target_link_libraries(sw_graphics_vulkan_libs INTERFACE ${SW_SYSTEM_VULKAN_LIBRARY})
        message(STATUS "[Linux] Vulkan loader (system): ${SW_SYSTEM_VULKAN_LIBRARY}")
    else()
        message(WARNING "[Linux] Vulkan loader not found — Vulkan RHI link may fail")
    endif()
endif()

if(NOT TARGET sw_graphics_libs)
    add_library(sw_graphics_libs INTERFACE)
    target_link_libraries(sw_graphics_libs INTERFACE
        sw_graphics_gl_libs
        sw_graphics_vulkan_libs
    )
endif()

list(APPEND sw_flag_libraries sw_platform_linux)

