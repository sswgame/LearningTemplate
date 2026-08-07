# ==============================================================================
# @file ThirdParty/imgui-notify/CMakeLists.txt
# @brief TyomaVader/ImGuiNotify (header-only toast notifications)
# ==============================================================================

add_library(imgui_notify INTERFACE)
target_include_directories(imgui_notify INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
# Prefer notifications relative to the main ImGui viewport (works without multi-viewport hooks).
target_compile_definitions(imgui_notify INTERFACE
    NOTIFY_RENDER_OUTSIDE_MAIN_WINDOW=false
)

if(TARGET imgui)
    target_link_libraries(imgui_notify INTERFACE imgui)
elseif(TARGET imgui::imgui)
    target_link_libraries(imgui_notify INTERFACE imgui::imgui)
endif()
