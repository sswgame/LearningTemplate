# Linux platform definitions.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
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

# Mirror Windows sw_platform_windows (opengl32.lib): bake GL/GLX into every target via
# sw_flag_libraries so SHARED Core can resolve glX* when SW_RHI_AS_MODULES=OFF
# (lld --no-allow-shlib-undefined on consumers).
find_package(OpenGL)
if(OpenGL_FOUND)
    if(TARGET OpenGL::GL)
        target_link_libraries(sw_platform_linux INTERFACE OpenGL::GL)
    elseif(OPENGL_gl_LIBRARY)
        target_link_libraries(sw_platform_linux INTERFACE ${OPENGL_gl_LIBRARY})
    endif()

    if(TARGET OpenGL::GLX)
        target_link_libraries(sw_platform_linux INTERFACE OpenGL::GLX)
    elseif(OPENGL_opengl_LIBRARY AND NOT TARGET OpenGL::GL)
        # GLVND split: libOpenGL + libGLX
        target_link_libraries(sw_platform_linux INTERFACE ${OPENGL_opengl_LIBRARY})
    else()
        find_library(SW_GLX_LIBRARY NAMES GLX glx)
        if(SW_GLX_LIBRARY)
            target_link_libraries(sw_platform_linux INTERFACE ${SW_GLX_LIBRARY})
        endif()
    endif()
else()
    message(WARNING "[Linux] OpenGL not found — GL/GLX RHI link may fail when SW_RHI_AS_MODULES=OFF")
endif()

# Vulkan loader (vkCreateXlibSurfaceKHR etc.) — same reason as GLX when RHI is in Core.
find_package(Vulkan QUIET)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(sw_platform_linux INTERFACE Vulkan::Vulkan)
elseif(Vulkan_LIBRARIES)
    target_link_libraries(sw_platform_linux INTERFACE ${Vulkan_LIBRARIES})
endif()

if(NOT TARGET sw_graphics_libs)
    add_library(sw_graphics_libs INTERFACE)
    target_link_libraries(sw_graphics_libs INTERFACE sw_platform_linux)
endif()

list(APPEND sw_flag_libraries sw_platform_linux)
