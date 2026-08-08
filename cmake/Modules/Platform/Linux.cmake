# Linux platform definitions (CMake-only: find_library, RPATH, INTERFACE libs).
# Host file fixes (Vulkan loader symlink) live in cmake/internal/VcpkgHostFixes.cmake.
# GPU (OpenGL/GLX/Vulkan) → sw_graphics_libs; X11/xcb stay on platform (windowing/WSI).

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

# Prefer the resolved loader directory in RPATH (no hardcoded sysroot paths).
find_library(SW_SYSTEM_VULKAN_LIBRARY NAMES vulkan)
if(SW_SYSTEM_VULKAN_LIBRARY)
    get_filename_component(_sw_vk_real "${SW_SYSTEM_VULKAN_LIBRARY}" REALPATH)
    get_filename_component(_sw_vk_libdir "${_sw_vk_real}" DIRECTORY)
    list(PREPEND CMAKE_BUILD_RPATH "${_sw_vk_libdir}")
    list(PREPEND CMAKE_INSTALL_RPATH "${_sw_vk_libdir}")
    message(STATUS "[Linux] Vulkan loader: ${SW_SYSTEM_VULKAN_LIBRARY} → ${_sw_vk_real}")
endif()

add_library(sw_platform_linux INTERFACE)
target_compile_definitions(sw_platform_linux INTERFACE SW_PLATFORM_LINUX)

find_package(X11)
if(X11_FOUND)
    target_link_libraries(sw_platform_linux INTERFACE ${X11_LIBRARIES})
    target_include_directories(sw_platform_linux INTERFACE ${X11_INCLUDE_DIR})
endif()

# WSLg Vulkan WSI often needs xcb (VK_KHR_xcb_surface) via XGetXCBConnection.
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

if(NOT TARGET sw_graphics_libs)
    add_library(sw_graphics_libs INTERFACE)
endif()

# OpenGL/GLX — Core PUBLIC sw_graphics_libs 로만 전파 (tools/tests는 미링크).
find_package(OpenGL)
if(OpenGL_FOUND)
    if(TARGET OpenGL::GL)
        target_link_libraries(sw_graphics_libs INTERFACE OpenGL::GL)
    elseif(OPENGL_gl_LIBRARY)
        target_link_libraries(sw_graphics_libs INTERFACE ${OPENGL_gl_LIBRARY})
    endif()

    if(TARGET OpenGL::GLX)
        target_link_libraries(sw_graphics_libs INTERFACE OpenGL::GLX)
    elseif(OPENGL_opengl_LIBRARY AND NOT TARGET OpenGL::GL)
        target_link_libraries(sw_graphics_libs INTERFACE ${OPENGL_opengl_LIBRARY})
    else()
        find_library(SW_GLX_LIBRARY NAMES GLX glx)
        if(SW_GLX_LIBRARY)
            target_link_libraries(sw_graphics_libs INTERFACE ${SW_GLX_LIBRARY})
        endif()
    endif()
else()
    message(WARNING "[Linux] OpenGL not found — GL/GLX RHI link may fail")
endif()

# Vulkan loader — prefer system libvulkan (vcpkg loader often lacks X11 WSI).
if(SW_SYSTEM_VULKAN_LIBRARY)
    target_link_libraries(sw_graphics_libs INTERFACE ${SW_SYSTEM_VULKAN_LIBRARY})
else()
    find_package(Vulkan QUIET)
    if(TARGET Vulkan::Vulkan)
        target_link_libraries(sw_graphics_libs INTERFACE Vulkan::Vulkan)
    elseif(Vulkan_LIBRARIES)
        target_link_libraries(sw_graphics_libs INTERFACE ${Vulkan_LIBRARIES})
    endif()
endif()

list(APPEND sw_flag_libraries sw_platform_linux)
