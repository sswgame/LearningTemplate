# ==============================================================================
# @file ThirdParty/imgui-node-editor/CMakeLists.txt
# @brief thedmd/imgui-node-editor — Editor companion (not imnodes)
# ==============================================================================

find_package(unofficial-imgui-node-editor CONFIG QUIET)

if(TARGET unofficial::imgui-node-editor::imgui-node-editor)
    add_library(imgui_node_editor INTERFACE)
    target_link_libraries(imgui_node_editor INTERFACE unofficial::imgui-node-editor::imgui-node-editor)
else()
    message(WARNING "[imgui-node-editor] vcpkg package not found — Editor node panels will fail to link")
    add_library(imgui_node_editor INTERFACE)
endif()
