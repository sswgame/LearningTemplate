# ==============================================================================
# @file ThirdParty/pugixml/CMakeLists.txt
# @brief pugixml — C++ XML 라이브러리
# ==============================================================================

sw_add_vcpkg_package(
	NAME pugixml
	PACKAGE pugixml
	CONFIG_TARGET pugixml::pugixml
	ATTACH_GLOBAL
)
