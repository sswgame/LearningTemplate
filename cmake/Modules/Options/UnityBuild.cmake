# ==============================================================================
# @file cmake/Modules/Options/UnityBuild.cmake
# @brief Unity 빌드 헬퍼 — SW_ENABLE_UNITY_BUILD로 선택(기본 OFF)
# @note 헬퍼는 항상 정의하여 타겟이 호출할 수 있게 하고, 옵션이 꺼지면 no-op
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) sw_set_unity_build — 타겟 UNITY_BUILD ON/OFF (크기는 SW_UNITY_BATCH_SIZE 한 값, OFF 는 묶을 수 없는 타깃)
# ------------------------------------------------------------------------------
function(sw_set_unity_build TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		return()
	endif()

	cmake_parse_arguments(ARG "OFF" "" "" ${ARGN})

	if(ARG_OFF OR NOT SW_ENABLE_UNITY_BUILD)
		set_target_properties(${TARGET_NAME} PROPERTIES UNITY_BUILD OFF)
		return()
	endif()

	if(NOT SW_UNITY_BATCH_SIZE GREATER 1)
		message(FATAL_ERROR "[UnityBuild] SW_UNITY_BATCH_SIZE='${SW_UNITY_BATCH_SIZE}' must be 2 or more (turn unity off with SW_ENABLE_UNITY_BUILD=OFF)")
	endif()

	set_target_properties(${TARGET_NAME} PROPERTIES
		UNITY_BUILD ON
		UNITY_BUILD_BATCH_SIZE ${SW_UNITY_BATCH_SIZE}
	)

	sw_skip_unity_for_x11_sources(${TARGET_NAME})
	sw_skip_unity_for_test_sources(${TARGET_NAME})
endfunction()

# ------------------------------------------------------------------------------
# 1-0) sw_skip_unity_for_test_sources — 테스트 .cpp(`/Tests/` 아래)를 Unity 배치에서 뺀다
#
# 테스트 파일은 파일마다 익명 이름공간에 짧은 이름(`kNowMs` · `Util` · 가짜 서비스)을 둔다 — 한 묶음에 들면 서로 부딪힌다.
# 묶음 크기 · 파일 추가로 묶음 경계가 바뀔 때마다 다른 쌍이 깨지므로 이름을 고치지 않고 묶음에서 뺀다(엔진 소스만 묶는다 —
# 언리얼도 테스트 모듈을 따로 짓는다).
# ------------------------------------------------------------------------------
function(sw_skip_unity_for_test_sources TARGET_NAME)
	get_target_property(list_source ${TARGET_NAME} SOURCES)
	get_target_property(source_dir ${TARGET_NAME} SOURCE_DIR)
	foreach(src IN LISTS list_source)
		if(NOT src MATCHES "\\.(c|cc|cpp|cxx)$" OR src MATCHES "^\\$<" OR NOT src MATCHES "(^|/)Tests/")
			continue()
		endif()
		if(IS_ABSOLUTE "${src}")
			set(src_path "${src}")
		else()
			set(src_path "${source_dir}/${src}")
		endif()
		set_source_files_properties("${src_path}" TARGET_DIRECTORY ${TARGET_NAME} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# 1-1) sw_skip_unity_for_x11_sources — X11 헤더를 include 하는 .cpp 를 Unity 배치에서 뺀다(리눅스만)
#
# X11 은 `Convex` · `None` · `KeyPress` 같은 흔한 단어를 매크로로 정의한다. 그 TU 가 묶음에 들어가면 매크로가 같은 묶음의
# 뒤 TU(서드파티 헤더 포함)로 샌다. 목록이 아니라 **소스의 include 줄**로 고른다 — X11 헤더는 .cpp 에서만 include 한다는
# 규칙이므로 .cpp 의 직접 include 만 보면 된다.
# ------------------------------------------------------------------------------
function(sw_skip_unity_for_x11_sources TARGET_NAME)
	if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
		return()
	endif()

	get_target_property(list_source ${TARGET_NAME} SOURCES)
	get_target_property(source_dir ${TARGET_NAME} SOURCE_DIR)

	foreach(src IN LISTS list_source)
		if(NOT src MATCHES "\\.(c|cc|cpp|cxx)$" OR src MATCHES "^\\$<")
			continue()
		endif()

		if(IS_ABSOLUTE "${src}")
			set(src_path "${src}")
		else()
			set(src_path "${source_dir}/${src}")
		endif()

		# 생성 파일은 configure 때 아직 없다 — X11 을 include 하지도 않는다.
		if(NOT EXISTS "${src_path}")
			continue()
		endif()

		file(STRINGS "${src_path}" list_x11_include LIMIT_COUNT 1
			REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](X11/|GL/glx|vulkan/vulkan_xlib|Core/Foundation/X11Headers\\.h)")

		if(list_x11_include)
			set_source_files_properties("${src_path}" TARGET_DIRECTORY ${TARGET_NAME} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		endif()
	endforeach()
endfunction()

