# ==============================================================================
# @file ThirdParty/imgui-node-editor/CMakeLists.txt
# @brief imgui-node-editor — Editor 전용 (런타임 경로로 전파하지 않음)
# ==============================================================================

sw_addVcpkgStaticLib(
	NAME imgui_nodeeditor
	PACKAGE unofficial-imgui-node-editor
	CONFIG_TARGET unofficial::imgui-node-editor::imgui-node-editor
	HEADER imgui-node-editor
	LIB_BASENAME imgui-node-editor
	WARN "[imgui-node-editor] vcpkg package not found — Editor node panels will fail to link"
)
