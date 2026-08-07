# ==============================================================================
# @file ThirdParty/implot/CMakeLists.txt
# @brief ImPlot (Dear ImGui plotting) — Editor companion
# ==============================================================================

find_package(implot CONFIG QUIET)

if(TARGET implot::implot)
    add_library(implot INTERFACE)
    target_link_libraries(implot INTERFACE implot::implot)
elseif(TARGET implot)
    add_library(implot_iface INTERFACE)
    target_link_libraries(implot_iface INTERFACE implot)
    add_library(implot ALIAS implot_iface)
else()
    message(WARNING "[implot] vcpkg package not found — Editor plot panels will fail to link")
    add_library(implot INTERFACE)
endif()
