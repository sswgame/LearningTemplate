# ==============================================================================
# @file ThirdParty/rapidxml/CMakeLists.txt
# @brief RapidXML — 헤더 온리
# ==============================================================================

sw_addVcpkgConfigLib(
	NAME rapidxml
	PACKAGE rapidxml
	CONFIG_TARGET rapidxml::rapidxml
	HEADER_ONLY
	ATTACH_GLOBAL
)

if(DEFINED VCPKG_INSTALLED_DIR AND EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/rapidxml")
	target_include_directories(rapidxml SYSTEM INTERFACE
		"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/rapidxml"
	)
endif()
