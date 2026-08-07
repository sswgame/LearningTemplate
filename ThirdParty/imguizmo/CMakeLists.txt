# ==============================================================================
# @file ThirdParty/imguizmo/CMakeLists.txt
# @brief ImGuizmo (3D gizmo) — Editor companion
# ==============================================================================

find_package(imguizmo CONFIG QUIET)

if(TARGET imguizmo::imguizmo)
    add_library(imguizmo INTERFACE)
    target_link_libraries(imguizmo INTERFACE imguizmo::imguizmo)
elseif(TARGET imguizmo)
    add_library(imguizmo_iface INTERFACE)
    target_link_libraries(imguizmo_iface INTERFACE imguizmo)
    add_library(imguizmo ALIAS imguizmo_iface)
else()
    message(WARNING "[imguizmo] vcpkg package not found — Editor gizmo panels will fail to link")
    add_library(imguizmo INTERFACE)
endif()
