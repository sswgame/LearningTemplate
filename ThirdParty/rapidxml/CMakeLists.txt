# ==============================================================================
# @file ThirdParty/rapidxml/CMakeLists.txt
# @brief RapidXML — 헤더 온리
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) INTERFACE — vcpkg include, rapidxml.hpp가 rapidxml/ 하위에 있으면 추가
# ------------------------------------------------------------------------------
add_library(rapidxml INTERFACE)
add_library(rapidxml::rapidxml ALIAS rapidxml)
if(COMMAND sw_linkVcpkgHeaderOnlyTarget)
	sw_linkVcpkgHeaderOnlyTarget(rapidxml)
else()
	target_include_directories(rapidxml SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(DEFINED VCPKG_INSTALLED_DIR AND EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/rapidxml")
	target_include_directories(rapidxml SYSTEM INTERFACE
		"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/rapidxml")
endif()
if(TARGET sw_third_party_includes)
	target_link_libraries(sw_third_party_includes INTERFACE rapidxml)
endif()
