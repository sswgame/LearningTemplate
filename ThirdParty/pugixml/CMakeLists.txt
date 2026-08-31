# ==============================================================================
# @file ThirdParty/pugixml/CMakeLists.txt
# @brief pugixml — C++ XML 라이브러리
# ==============================================================================

sw_addVcpkgConfigLib(
	NAME pugixml
	PACKAGE pugixml
	CONFIG_TARGET pugixml::pugixml
	ATTACH_GLOBAL
)
