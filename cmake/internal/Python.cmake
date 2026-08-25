# ==============================================================================
# @file cmake/internal/Python.cmake
# @brief Scripts/*.py 호출 헬퍼 (execute_process / 경로)
# ==============================================================================

find_package(Python3 QUIET COMPONENTS Interpreter)

# CMake Tools 등 얇은 PATH에서도 Git for Windows를 쓰도록 기본 경로를 앞에 붙입니다.
# 주의: ENV{ProgramFiles(x86)} 는 괄호 때문에 if(DEFINED ...) 파싱이 깨지므로 쓰지 않음.
if(WIN32)
	set(_swGitCandidates
		"$ENV{ProgramFiles}/Git/cmd"
		"$ENV{ProgramFiles}/Git/bin"
		"C:/Program Files/Git/cmd"
		"C:/Program Files/Git/bin"
		"C:/Program Files (x86)/Git/cmd"
		"C:/Program Files (x86)/Git/bin"
		"$ENV{LOCALAPPDATA}/Programs/Git/cmd"
		"$ENV{LOCALAPPDATA}/Programs/Git/bin"
	)
	foreach(_swGitDir IN LISTS _swGitCandidates)
		if(EXISTS "${_swGitDir}/git.exe")
			set(ENV{PATH} "${_swGitDir};$ENV{PATH}")
			break()
		endif()
	endforeach()
	unset(_swGitDir)
	unset(_swGitCandidates)
endif()

if(NOT COMMAND sw_executePythonScript)
# ------------------------------------------------------------------------------
# 1) sw_executePythonScript — 프로젝트 안 Python 스크립트 실행
#    ARGS / OUTPUT_VARIABLE / RESULT_VARIABLE / WORKING_DIRECTORY
#    WARN|REQUIRED|QUIET. SCRIPT_REL은 저장소 루트 기준
# ------------------------------------------------------------------------------
function(sw_executePythonScript SCRIPT_REL)
    set(options WARN REQUIRED QUIET)
    set(oneValueArgs OUTPUT_VARIABLE RESULT_VARIABLE WORKING_DIRECTORY)
    set(multiValueArgs ARGS)
    cmake_parse_arguments(SW_PY "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT Python3_Interpreter_FOUND)
        if(SW_PY_REQUIRED)
            message(FATAL_ERROR "[Python] Python3 interpreter required for ${SCRIPT_REL}")
        endif()
        if(NOT SW_PY_QUIET)
            message(WARNING "[Python] Python3 not found; skipping ${SCRIPT_REL}")
        endif()
        if(SW_PY_RESULT_VARIABLE)
            set(${SW_PY_RESULT_VARIABLE} 127 PARENT_SCOPE)
        endif()
        return()
    endif()

    # 호출 측 listfile이 아니라 이 함수가 정의된 cmake/internal 기준 → 저장소 루트
    get_filename_component(SW_ROOT_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
    set(script "${SW_ROOT_DIR}/${SCRIPT_REL}")
    if(NOT EXISTS "${script}")
        if(SW_PY_REQUIRED)
            message(FATAL_ERROR "[Python] Script not found: ${script}")
        endif()
        if(NOT SW_PY_QUIET)
            message(WARNING "[Python] Script not found: ${script}")
        endif()
        if(SW_PY_RESULT_VARIABLE)
            set(${SW_PY_RESULT_VARIABLE} 127 PARENT_SCOPE)
        endif()
        return()
    endif()

    set(wd "${SW_ROOT_DIR}")
    if(SW_PY_WORKING_DIRECTORY)
        set(wd "${SW_PY_WORKING_DIRECTORY}")
    endif()

    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${script}" ${SW_PY_ARGS}
        WORKING_DIRECTORY "${wd}"
        RESULT_VARIABLE swPyResult
        OUTPUT_VARIABLE swPyOutput
        ERROR_VARIABLE swPyError
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )

    if(SW_PY_OUTPUT_VARIABLE)
        set(${SW_PY_OUTPUT_VARIABLE} "${swPyOutput}" PARENT_SCOPE)
    endif()
    if(SW_PY_RESULT_VARIABLE)
        set(${SW_PY_RESULT_VARIABLE} "${swPyResult}" PARENT_SCOPE)
    endif()

    if(NOT swPyResult EQUAL 0)
        if(SW_PY_REQUIRED)
            message(FATAL_ERROR
                "[Python] ${SCRIPT_REL} failed (exit ${swPyResult}).\n"
                "${swPyOutput}\n${swPyError}"
            )
        elseif(SW_PY_WARN)
            message(WARNING
                "[Python] ${SCRIPT_REL} failed (exit ${swPyResult}).\n"
                "${swPyOutput}\n${swPyError}"
            )
        endif()
    elseif(swPyOutput AND NOT SW_PY_QUIET)
        message(STATUS "${swPyOutput}")
    endif()
endfunction()
endif()
