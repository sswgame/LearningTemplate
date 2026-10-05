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

	sw_skipUnityForX11Sources(${TARGET_NAME})
endfunction()

# ------------------------------------------------------------------------------
# 1-1) sw_skipUnityForX11Sources — X11 헤더를 include 하는 .cpp 를 Unity 배치에서 뺀다(리눅스만)
#
# X11 은 `Convex` · `None` · `KeyPress` 같은 흔한 단어를 매크로로 정의한다. 그 TU 가 묶음에 들어가면 매크로가 같은 묶음의
# 뒤 TU(서드파티 헤더 포함)로 샌다. 목록이 아니라 **소스의 include 줄**로 고른다 — X11 헤더는 .cpp 에서만 include 한다는
# 규칙을 `CheckX11Isolation.py` 가 지키므로 .cpp 의 직접 include 만 보면 된다.
# ------------------------------------------------------------------------------
function(sw_skipUnityForX11Sources TARGET_NAME)
	if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
		return()
	endif()

	get_target_property(listSource ${TARGET_NAME} SOURCES)
	get_target_property(sourceDir ${TARGET_NAME} SOURCE_DIR)

	foreach(src IN LISTS listSource)
		if(NOT src MATCHES "\\.(c|cc|cpp|cxx)$" OR src MATCHES "^\\$<")
			continue()
		endif()

		if(IS_ABSOLUTE "${src}")
			set(srcPath "${src}")
		else()
			set(srcPath "${sourceDir}/${src}")
		endif()

		# 생성 파일은 configure 때 아직 없다 — X11 을 include 하지도 않는다.
		if(NOT EXISTS "${srcPath}")
			continue()
		endif()

		file(STRINGS "${srcPath}" listX11Include LIMIT_COUNT 1
			REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](X11/|GL/glx|vulkan/vulkan_xlib|Core/Common/X11Headers\\.h)")

		if(listX11Include)
			set_source_files_properties("${srcPath}" TARGET_DIRECTORY ${TARGET_NAME} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		endif()
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_skipUnitySources — 지정 소스를 Unity 배치에서 제외
#
# **지금 부르는 곳이 없다.** 제외 목록은 TU 를 나눌 때마다 같이 고쳐야 해서 썩는다. 이름이 부딪치는
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

	# 없는 경로를 조용히 건너뛰면 파일이 옮겨졌을 때 제외가 무효가 된 걸 아무도 모른다. 경로가 틀리면 즉시 실패한다.
	foreach(src IN LISTS ARGN)
		if(NOT EXISTS "${src}")
			message(FATAL_ERROR "[UnityBuild] sw_skipUnitySources(${TARGET_NAME}): 없는 경로 -> ${src}")
		endif()
		set_source_files_properties("${src}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	endforeach()
endfunction()
