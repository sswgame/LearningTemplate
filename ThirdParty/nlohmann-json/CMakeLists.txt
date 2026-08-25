# ==============================================================================
# @file ThirdParty/nlohmann-json/CMakeLists.txt
# @brief nlohmann/json — 헤더 온리
# ==============================================================================

sw_addVcpkgConfigLib(
	NAME nlohmann_json
	PACKAGE nlohmann_json
	CONFIG_TARGET nlohmann_json::nlohmann_json
	HEADER_ONLY
	ATTACH_GLOBAL
)
