# ==============================================================================
# @file cmake/internal/Python.cmake
# @brief Scripts/*.py 호출 헬퍼 (execute_process / 경로)
# ==============================================================================

find_package(Python3 QUIET COMPONENTS Interpreter)

# sw_execute_python_script(<rel_script> [ARGS ...] [OUTPUT_VARIABLE var] [RESULT_VARIABLE var] [WARN|REQUIRED|QUIET])
if(NOT COMMAND sw_execute_python_script)
function(sw_execute_python_script SCRIPT_REL)
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

    set(_script "${CMAKE_SOURCE_DIR}/${SCRIPT_REL}")
    if(NOT EXISTS "${_script}")
        if(SW_PY_REQUIRED)
            message(FATAL_ERROR "[Python] Script not found: ${_script}")
        endif()
        if(NOT SW_PY_QUIET)
            message(WARNING "[Python] Script not found: ${_script}")
        endif()
        if(SW_PY_RESULT_VARIABLE)
            set(${SW_PY_RESULT_VARIABLE} 127 PARENT_SCOPE)
        endif()
        return()
    endif()

    set(_wd "${CMAKE_SOURCE_DIR}")
    if(SW_PY_WORKING_DIRECTORY)
        set(_wd "${SW_PY_WORKING_DIRECTORY}")
    endif()

    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_script}" ${SW_PY_ARGS}
        WORKING_DIRECTORY "${_wd}"
        RESULT_VARIABLE _sw_py_result
        OUTPUT_VARIABLE _sw_py_output
        ERROR_VARIABLE _sw_py_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )

    if(SW_PY_OUTPUT_VARIABLE)
        set(${SW_PY_OUTPUT_VARIABLE} "${_sw_py_output}" PARENT_SCOPE)
    endif()
    if(SW_PY_RESULT_VARIABLE)
        set(${SW_PY_RESULT_VARIABLE} "${_sw_py_result}" PARENT_SCOPE)
    endif()

    if(NOT _sw_py_result EQUAL 0)
        if(SW_PY_REQUIRED)
            message(FATAL_ERROR
                "[Python] ${SCRIPT_REL} failed (exit ${_sw_py_result}).\n"
                "${_sw_py_output}\n${_sw_py_error}"
            )
        elseif(SW_PY_WARN)
            message(WARNING
                "[Python] ${SCRIPT_REL} failed (exit ${_sw_py_result}).\n"
                "${_sw_py_output}\n${_sw_py_error}"
            )
        endif()
    elseif(_sw_py_output AND NOT SW_PY_QUIET)
        message(STATUS "${_sw_py_output}")
    endif()
endfunction()
endif()
