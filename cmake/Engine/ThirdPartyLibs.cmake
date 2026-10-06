# ==============================================================================
# @file cmake/Engine/ThirdPartyLibs.cmake
# @brief 서드파티를 어떻게 붙이나 — SYSTEM include, vcpkg CONFIG 패키지(못 찾으면 구성 실패), 설정 파일 없는 헤더 전용 포트
# ==============================================================================

# ------------------------------------------------------------------------------
# ThirdParty 래퍼 — SYSTEM include
# ------------------------------------------------------------------------------
function(sw_thirdPartySystemIncludes TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "INTERFACE;PUBLIC;PRIVATE" ${ARGN})

	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "[ThirdParty] Target not found: ${TARGET_NAME}")
	endif()

	if(ARG_INTERFACE)
		target_include_directories(${TARGET_NAME} SYSTEM INTERFACE ${ARG_INTERFACE})
	endif()

	if(ARG_PUBLIC)
		target_include_directories(${TARGET_NAME} SYSTEM PUBLIC ${ARG_PUBLIC})
	endif()

	if(ARG_PRIVATE)
		target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${ARG_PRIVATE})
	endif()
endfunction()

# ------------------------------------------------------------------------------
# sw_addVcpkgPackage — CMake 설정 파일이 있는 vcpkg 패키지를 NAME 이라는 INTERFACE 타깃으로 감싼다.
#   못 찾으면 **구성을 세운다**(find_package REQUIRED) — 빈 타깃으로 대신하면 링크(또는 실행)에서야 드러난다.
#   NAME · PACKAGE(기본 NAME) · CONFIG_TARGET(기본 NAME::NAME) · ATTACH_GLOBAL(sw_third_party_includes 에도 건다)
# ------------------------------------------------------------------------------
function(sw_addVcpkgPackage)
	cmake_parse_arguments(ARG "ATTACH_GLOBAL" "NAME;PACKAGE;CONFIG_TARGET" "" ${ARGN})
	if(NOT ARG_NAME)
		message(FATAL_ERROR "sw_addVcpkgPackage: NAME is required")
	endif()
	if(NOT ARG_PACKAGE)
		set(ARG_PACKAGE ${ARG_NAME})
	endif()
	if(NOT ARG_CONFIG_TARGET)
		set(ARG_CONFIG_TARGET "${ARG_NAME}::${ARG_NAME}")
	endif()

	find_package(${ARG_PACKAGE} CONFIG REQUIRED)
	if(NOT TARGET ${ARG_NAME})
		if(NOT TARGET ${ARG_CONFIG_TARGET})
			message(FATAL_ERROR "[ThirdParty] found ${ARG_PACKAGE} but it has no target ${ARG_CONFIG_TARGET} — fix CONFIG_TARGET")
		endif()
		add_library(${ARG_NAME} INTERFACE)
		target_link_libraries(${ARG_NAME} INTERFACE ${ARG_CONFIG_TARGET})
	endif()
	if(ARG_ATTACH_GLOBAL AND TARGET sw_third_party_includes)
		target_link_libraries(sw_third_party_includes INTERFACE ${ARG_NAME})
	endif()
endfunction()

# ------------------------------------------------------------------------------
# sw_addVcpkgHeaderOnly — CMake 설정 파일이 없는 헤더 전용 vcpkg 포트(acl · cgltf). vcpkg include 폴더를 SYSTEM 으로 건다.
#   PROBE(예: cgltf.h)가 그 폴더에 없으면 구성을 세운다.
# ------------------------------------------------------------------------------
function(sw_addVcpkgHeaderOnly)
	cmake_parse_arguments(ARG "" "NAME;PROBE" "" ${ARGN})
	if(NOT ARG_NAME OR NOT ARG_PROBE)
		message(FATAL_ERROR "sw_addVcpkgHeaderOnly: NAME and PROBE are required")
	endif()
	sw_getVcpkgPaths(listIncludeDir listBinDir)
	set(isFound OFF)
	foreach(includeDir IN LISTS listIncludeDir)
		if(EXISTS "${includeDir}/${ARG_PROBE}")
			set(isFound ON)
		endif()
	endforeach()
	if(NOT isFound)
		message(FATAL_ERROR "[ThirdParty] ${ARG_NAME}: ${ARG_PROBE} is not in the vcpkg include folders (${listIncludeDir}) — check vcpkg.json and run vcpkg install")
	endif()
	add_library(${ARG_NAME} INTERFACE)
	sw_linkVcpkgHeaderOnlyTarget(${ARG_NAME})
endfunction()
