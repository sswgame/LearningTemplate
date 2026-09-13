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
#
# **지금 부르는 곳이 없다(2026-09-13).** Engine 이 TU 19 개를 여기로 빼 두고 있었는데, 그 목록은
# TU 를 나눌 때마다 같이 고쳐야 해서 계속 썩었다(아래 FATAL_ERROR 가 그래서 붙었다). 이름이 부딪치는
# 문제는 목록이 아니라 **이름 규칙**으로 막는 것이 맞다 — 익명 네임스페이스 헬퍼는 TU 이름을 따고
# (AGENTS.md) `CheckCodeConventions` 의 `Naming/DuplicateInternalHelper` 가 검사한다.
#
# 남겨 두는 이유: 이름으로 못 푸는 자리가 실제로 있을 수 있다(플랫폼 `#if` 로 갈리는 블록 등).
# 그때는 **무엇이 부딪치는지 CI-* 빌드로 먼저 보고** 이 함수를 쓰되, 목록이 다시 길어지면 규칙이
# 깨지고 있다는 신호다.
# ------------------------------------------------------------------------------
function(sw_skipUnitySources TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		return()
	endif()

	# 없는 경로를 조용히 건너뛰면 파일이 옮겨졌을 때 제외가 무효가 된 걸 아무도 모른다
	# (Renderer 재편 뒤 FrameRenderer 제외 6개가 그렇게 죽어 있었다). 경로가 틀리면 즉시 실패한다.
	foreach(src IN LISTS ARGN)
		if(NOT EXISTS "${src}")
			message(FATAL_ERROR "[UnityBuild] sw_skipUnitySources(${TARGET_NAME}): 없는 경로 -> ${src}")
		endif()
		set_source_files_properties("${src}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	endforeach()
endfunction()
