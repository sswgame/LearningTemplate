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

if(NOT TARGET sw_graphics_libs)
    add_library(sw_graphics_libs INTERFACE)
    target_link_libraries(sw_graphics_libs INTERFACE sw_platform_linux)
endif()

list(APPEND sw_flag_libraries sw_platform_linux)
