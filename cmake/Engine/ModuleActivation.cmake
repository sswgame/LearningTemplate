# ==============================================================================
# @file cmake/Engine/ModuleActivation.cmake
# @brief 모듈 켜짐 조회 — 모듈을 만드는 함수가 꺼진 모듈을 건너뛰고, 시험이 꺼진 키트를 쓰는 소스를 뺀다(답은 ModuleManifest.cmake 의 해석 결과)
# ==============================================================================

# 모듈 NAME 이 이 구성에서 켜져 있으면 OUT_VAR 를 ON 으로 둔다. 매니페스트가 없는 모듈은 구성을 세운다(모든 동적 모듈은 매니페스트를 갖는다).
function(sw_isModuleActive NAME OUT_VAR)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)

	if(NOT NAME IN_LIST swNames)
		message(FATAL_ERROR "[Module] '${NAME}' has no manifest — add ${NAME}.module.json next to its CMakeLists.txt")
	endif()

	get_property(swActive GLOBAL PROPERTY SW_MODULE_${NAME}_ACTIVE)
	set(${OUT_VAR} ${swActive} PARENT_SCOPE)
endfunction()

# 꺼진 모듈 NAME 의 폴더를 "이 구성이 짓지 않는다" 로 적고 OUT_VAR 를 ON 으로 둔다(켜져 있으면 OFF). 모듈 타깃을 만드는 함수의 첫 줄에서 부른다.
function(sw_skipInactiveModule NAME OUT_VAR)
	sw_isModuleActive(${NAME} swActive)

	if(swActive)
		set(${OUT_VAR} OFF PARENT_SCOPE)
		return()
	endif()

	get_property(swReason GLOBAL PROPERTY SW_MODULE_${NAME}_REASON)
	message(STATUS "[Module] ${NAME} is not built — ${swReason}")
	sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}")
	set(${OUT_VAR} ON PARENT_SCOPE)
endfunction()

# 소스 목록 LIST_VAR 에서 꺼진 키트의 헤더(`GameFramework/Kits/<묶음>/<키트>/…`)를 include 하는 파일을 뺀다 — 시험 실행 파일이 꺼진 키트를 링크하지 않게.
# 뺀 것은 "이 구성이 짓지 않는 소스" 로 적는다.
function(sw_excludeSourcesOfInactiveKits LIST_VAR)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)
	set(swInactivePrefixes "")

	foreach(swName IN LISTS swNames)
		get_property(swKind GLOBAL PROPERTY SW_MODULE_${swName}_KIND)
		get_property(swActive GLOBAL PROPERTY SW_MODULE_${swName}_ACTIVE)

		if(swKind STREQUAL "Kit" AND NOT swActive)
			get_property(swDirectory GLOBAL PROPERTY SW_MODULE_${swName}_DIR)
			file(RELATIVE_PATH swRelative "${CMAKE_SOURCE_DIR}/Source" "${swDirectory}")
			list(APPEND swInactivePrefixes "${swRelative}/")
		endif()
	endforeach()

	if(NOT swInactivePrefixes)
		return()
	endif()

	set(swKept "")
	set(swDropped "")

	foreach(swSource IN LISTS ${LIST_VAR})
		set(swUsesInactive OFF)

		if(swSource MATCHES "\\.(cpp|h)$")
			file(STRINGS "${swSource}" swIncludes REGEX "^#include \"GameFramework/Kits/")

			foreach(swPrefix IN LISTS swInactivePrefixes)
				string(FIND "${swIncludes}" "\"${swPrefix}" swHit)

				if(NOT swHit EQUAL -1)
					set(swUsesInactive ON)
				endif()
			endforeach()
		endif()

		if(swUsesInactive)
			list(APPEND swDropped "${swSource}")
		else()
			list(APPEND swKept "${swSource}")
		endif()
	endforeach()

	if(swDropped)
		sw_declareUnbuiltSources(${swDropped})
	endif()

	set(${LIST_VAR} ${swKept} PARENT_SCOPE)
endfunction()
