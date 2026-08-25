# ==============================================================================
# @file ThirdParty/imgui/CMakeLists.txt
# @brief ImGui — EditorModule만 직접 링크 (런타임 경로로 전파하지 않음)
# ==============================================================================

sw_addVcpkgConfigLib(NAME imgui CONFIG_TARGET imgui::imgui HEADER_ONLY)
