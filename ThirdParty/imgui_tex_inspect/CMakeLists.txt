# ==============================================================================
# @file ThirdParty/imgui_tex_inspect/CMakeLists.txt
# @brief andyborrell/imgui_tex_inspect (DX11 + OpenGL backends)
# ==============================================================================

set(_tex_inspect_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui_tex_inspect.cpp"
)
# Upstream exposes a single BackEnd_* ABI — only one renderer backend may be linked.
if(WIN32)
    list(APPEND _tex_inspect_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/backends/tex_inspect_directx11.cpp"
    )
    set(_tex_inspect_backend "DX11")
else()
    list(APPEND _tex_inspect_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/backends/tex_inspect_opengl.cpp"
    )
    set(_tex_inspect_backend "OpenGL")
endif()

add_library(imgui_tex_inspect STATIC ${_tex_inspect_sources})
add_library(imgui_tex_inspect::imgui_tex_inspect ALIAS imgui_tex_inspect)

target_include_directories(imgui_tex_inspect
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/backends"
)

if(TARGET imgui)
    target_link_libraries(imgui_tex_inspect PUBLIC imgui)
elseif(TARGET imgui::imgui)
    target_link_libraries(imgui_tex_inspect PUBLIC imgui::imgui)
endif()

if(_tex_inspect_backend STREQUAL "OpenGL")
    if(TARGET glad)
        target_link_libraries(imgui_tex_inspect PRIVATE glad)
    elseif(TARGET glad::glad)
        target_link_libraries(imgui_tex_inspect PRIVATE glad::glad)
    endif()
    target_compile_definitions(imgui_tex_inspect PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)
elseif(_tex_inspect_backend STREQUAL "DX11")
    target_link_libraries(imgui_tex_inspect PRIVATE d3d11 d3dcompiler)
endif()

message(STATUS "[imgui_tex_inspect] backend=${_tex_inspect_backend}")

set_target_properties(imgui_tex_inspect PROPERTIES
    FOLDER "ThirdParty"
    POSITION_INDEPENDENT_CODE ON
)
