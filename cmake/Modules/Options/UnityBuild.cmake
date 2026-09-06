# ==============================================================================
# @file cmake/Modules/Options/UnityBuild.cmake
# @brief Unity 빌드 헬퍼 — SW_ENABLE_UNITY_BUILD로 선택(기본 OFF)
# @note 헬퍼는 항상 정의하여 타겟이 호출할 수 있게 하고, 옵션이 꺼지면 no-op
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) sw_setUnityBuild — 타겟 UNITY_BUILD ON/OFF (BATCH_SIZE, 기본 12)
# ------------------------------------------------------------------------------
function(sw_setUnityBuild TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		return()
	endif()

	cmake_parse_arguments(ARG "OFF" "BATCH_SIZE" "" ${ARGN})

	if(ARG_OFF OR NOT SW_ENABLE_UNITY_BUILD)
		set_target_properties(${TARGET_NAME} PROPERTIES UNITY_BUILD OFF)
		return()
	endif()

	set(batch 12)

	if(ARG_BATCH_SIZE)
		set(batch ${ARG_BATCH_SIZE})
	endif()

	set_target_properties(${TARGET_NAME} PROPERTIES
		UNITY_BUILD ON
		UNITY_BUILD_BATCH_SIZE ${batch}
	)
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_skipUnitySources — 지정 소스를 Unity 배치에서 제외
# ------------------------------------------------------------------------------
function(sw_skipUnitySources TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		return()
	endif()

	foreach(src IN LISTS ARGN)
		if(EXISTS "${src}")
			set_source_files_properties("${src}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		endif()
	endforeach()
endfunction()
