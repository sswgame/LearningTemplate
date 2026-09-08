# ==============================================================================
# @file cmake/Engine/ThirdPartyLibs.cmake
# @brief 서드파티를 어떻게 붙이나 — SYSTEM include, vcpkg CONFIG 패키지, 설치 트리 STATIC 폴백
# ==============================================================================

# ------------------------------------------------------------------------------
# ThirdParty 래퍼 — SYSTEM include / vcpkg CONFIG / STATIC 폴백
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

function(sw_addVcpkgConfigLib)
	cmake_parse_arguments(ARG "HEADER_ONLY;ATTACH_GLOBAL" "NAME;PACKAGE;CONFIG_TARGET" "" ${ARGN})

	if(NOT ARG_NAME)
		message(FATAL_ERROR "sw_addVcpkgConfigLib: NAME required")
	endif()

	if(NOT ARG_PACKAGE)
		set(ARG_PACKAGE ${ARG_NAME})
	endif()

	if(NOT ARG_CONFIG_TARGET)
		set(ARG_CONFIG_TARGET "${ARG_NAME}::${ARG_NAME}")
	endif()

	find_package(${ARG_PACKAGE} CONFIG QUIET)

	if(TARGET ${ARG_NAME})
	# Target already exists as imported library from find_package
	elseif(TARGET ${ARG_CONFIG_TARGET})
		add_library(${ARG_NAME} INTERFACE)
		target_link_libraries(${ARG_NAME} INTERFACE ${ARG_CONFIG_TARGET})
	else()
		add_library(${ARG_NAME} INTERFACE)

		if(NOT TARGET ${ARG_CONFIG_TARGET})
			add_library(${ARG_CONFIG_TARGET} ALIAS ${ARG_NAME})
		endif()

		if(ARG_HEADER_ONLY AND COMMAND sw_linkVcpkgHeaderOnlyTarget)
			sw_linkVcpkgHeaderOnlyTarget(${ARG_NAME})
		endif()
	endif()

	if(ARG_ATTACH_GLOBAL AND TARGET sw_third_party_includes)
		target_link_libraries(sw_third_party_includes INTERFACE ${ARG_NAME})
	endif()
endfunction()

function(sw_addVcpkgStaticLib)
	cmake_parse_arguments(ARG "" "NAME;PACKAGE;CONFIG_TARGET;HEADER;LIB_BASENAME;WARN" "" ${ARGN})

	if(NOT ARG_NAME OR NOT ARG_HEADER OR NOT ARG_LIB_BASENAME)
		message(FATAL_ERROR "sw_addVcpkgStaticLib: NAME, HEADER, LIB_BASENAME required")
	endif()

	if(NOT ARG_PACKAGE)
		set(ARG_PACKAGE ${ARG_NAME})
	endif()

	if(NOT ARG_CONFIG_TARGET)
		set(ARG_CONFIG_TARGET "${ARG_NAME}::${ARG_NAME}")
	endif()

	find_package(${ARG_PACKAGE} CONFIG QUIET)

	if(TARGET ${ARG_CONFIG_TARGET})
		add_library(${ARG_NAME} INTERFACE)
		target_link_libraries(${ARG_NAME} INTERFACE ${ARG_CONFIG_TARGET})
		return()
	endif()

	if(TARGET ${ARG_NAME})
		return()
	endif()

	set(root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

	if(NOT EXISTS "${root}/include/${ARG_HEADER}")
		if(ARG_WARN)
			message(WARNING "${ARG_WARN}")
		else()
			message(WARNING "[${ARG_NAME}] vcpkg 설치 트리에서 헤더 '${ARG_HEADER}'를 찾을 수 없습니다 (${root}/include).")
		endif()

		add_library(${ARG_NAME} INTERFACE)
		return()
	endif()

	add_library(${ARG_CONFIG_TARGET} STATIC IMPORTED GLOBAL)

	if(WIN32)
		set(dbg "${root}/debug/lib/${ARG_LIB_BASENAME}d.lib")

		if(NOT EXISTS "${dbg}")
			set(dbg "${root}/debug/lib/${ARG_LIB_BASENAME}.lib")
		endif()

		set(rel "${root}/lib/${ARG_LIB_BASENAME}.lib")
	else()
		set(dbg "${root}/debug/lib/lib${ARG_LIB_BASENAME}d.a")

		if(NOT EXISTS "${dbg}")
			set(dbg "${root}/debug/lib/lib${ARG_LIB_BASENAME}.a")
		endif()

		set(rel "${root}/lib/lib${ARG_LIB_BASENAME}.a")
	endif()

	set_target_properties(${ARG_CONFIG_TARGET} PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${root}/include"
		IMPORTED_LOCATION_DEBUG "${dbg}"
		IMPORTED_LOCATION_RELEASE "${rel}"
		IMPORTED_LOCATION_RELWITHDEBINFO "${rel}"
		IMPORTED_LOCATION_MINSIZEREL "${rel}"
		IMPORTED_LOCATION "${rel}"
	)
	add_library(${ARG_NAME} INTERFACE)
	target_link_libraries(${ARG_NAME} INTERFACE ${ARG_CONFIG_TARGET})
	message(STATUS "[${ARG_NAME}] Using vcpkg installed tree (manual import)")
endfunction()
