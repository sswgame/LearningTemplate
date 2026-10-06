# ==============================================================================
# @file ThirdParty/imgui-node-editor/CMakeLists.txt
# @brief imgui-node-editor — Editor 전용 (런타임 경로로 전파하지 않음)
# ==============================================================================

sw_addVcpkgPackage(
	NAME imgui_nodeeditor
	PACKAGE unofficial-imgui-node-editor
	CONFIG_TARGET unofficial::imgui-node-editor::imgui-node-editor
)
