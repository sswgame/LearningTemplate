# ==============================================================================
# @file cmake/Modules/Environment/PythonUtils.cmake
# @brief Scripts/*.py 서브프로세스 실행 유틸리티 헬퍼 (sw_execute_python_script) — 파이썬 인터프리터는 여기 한 곳에서 찾는다
# ==============================================================================

# 이 파일은 여러 곳에서 include 된다(상수 · vcpkg · 툴체인 · 에셋 타겟). 인터프리터는 한 번 찾으면 된다 — include 마다
# FindPython 을 다시 돌리면 configure 한 번에 그것만 1 초 가까이 쓴다. 가드 대신 결과 변수를 본다: `Python3_Interpreter_FOUND` 는 일반
# 변수라, 처음 찾은 스코프 밖(함수 안에서 처음 include 된 경우 등)에서는 다시 찾아야 한다.
if(NOT Python3_Interpreter_FOUND)
	find_package(Python3 QUIET COMPONENTS Interpreter)
endif()

# Git for Windows 기본 경로를 PATH 앞에(한 번 — include_guard).
include("${CMAKE_CURRENT_LIST_DIR}/HostPath.cmake")

# ------------------------------------------------------------------------------
# 1) sw_execute_python_script — 프로젝트 안 Python 스크립트 실행
# ARGS / OUTPUT_VARIABLE / RESULT_VARIABLE / WORKING_DIRECTORY
# WARN|REQUIRED|QUIET. SCRIPT_REL은 저장소 루트 기준
# ------------------------------------------------------------------------------
function(sw_execute_python_script SCRIPT_REL)
	set(options WARN REQUIRED QUIET)
	set(one_value_args OUTPUT_VARIABLE RESULT_VARIABLE WORKING_DIRECTORY)
	set(multi_value_args ARGS)
	cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

	if(NOT Python3_Interpreter_FOUND)
		if(ARG_REQUIRED)
			message(FATAL_ERROR "[Python] Python3 interpreter required for ${SCRIPT_REL}")
		endif()

		if(NOT ARG_QUIET)
			message(WARNING "[Python] Python3 not found; skipping ${SCRIPT_REL}")
		endif()

		if(ARG_RESULT_VARIABLE)
			set(${ARG_RESULT_VARIABLE} 127 PARENT_SCOPE)
		endif()
		return()
	endif()

	# 호출 측 listfile이 아니라 이 함수가 정의된 cmake/Modules/Environment 기준(세 단계 위) → 저장소 루트
	get_filename_component(SW_ROOT_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../.." ABSOLUTE)
	set(script "${SW_ROOT_DIR}/${SCRIPT_REL}")

	if(NOT EXISTS "${script}")
		if(ARG_REQUIRED)
			message(FATAL_ERROR "[Python] Script not found: ${script}")
		endif()

		if(NOT ARG_QUIET)
			message(WARNING "[Python] Script not found: ${script}")
		endif()

		if(ARG_RESULT_VARIABLE)
			set(${ARG_RESULT_VARIABLE} 127 PARENT_SCOPE)
		endif()
		return()
	endif()

	set(work_dir "${SW_ROOT_DIR}")

	if(ARG_WORKING_DIRECTORY)
		set(work_dir "${ARG_WORKING_DIRECTORY}")
	endif()

	# 출력은 UTF-8 로 쓰고(파이썬 UTF-8 모드) UTF-8 로 읽는다 — Windows 의 execute_process 는 기본이 ANSI 코드 페이지(cp949)라 생성기의 한글 오류가 깨져 찍혔다.
	set(cmd "${Python3_EXECUTABLE}" -X utf8 "${script}" ${ARG_ARGS})
	execute_process(
		COMMAND ${cmd}
		WORKING_DIRECTORY "${work_dir}"
		ENCODING UTF-8
		RESULT_VARIABLE res
		OUTPUT_VARIABLE out
		ERROR_VARIABLE err
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_STRIP_TRAILING_WHITESPACE
	)

	if(ARG_OUTPUT_VARIABLE)
		set(${ARG_OUTPUT_VARIABLE} "${out}" PARENT_SCOPE)
	endif()

	if(ARG_RESULT_VARIABLE)
		set(${ARG_RESULT_VARIABLE} ${res} PARENT_SCOPE)
	endif()

	if(NOT res EQUAL 0)
		if(ARG_REQUIRED)
			message(FATAL_ERROR "[Python] ${SCRIPT_REL} failed (${res}):\n${out}\n${err}")
		elseif(ARG_WARN)
			message(WARNING "[Python] ${SCRIPT_REL} exited with ${res}:\n${out}\n${err}")
		elseif(NOT ARG_QUIET)
			message(STATUS "[Python] ${SCRIPT_REL} (${res}): ${err}")
		endif()
	endif()
endfunction()
