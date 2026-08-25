# ==============================================================================
# @file ThirdParty/imguizmo/CMakeLists.txt
# @brief ImGuizmo — Editor 전용 (런타임 경로로 전파하지 않음)
# ==============================================================================

sw_addVcpkgStaticLib(
	NAME imguizmo
	HEADER ImGuizmo.h
	LIB_BASENAME imguizmo
	WARN "[imguizmo] vcpkg package not found — Editor gizmo panels will fail to link"
)
