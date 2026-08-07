# ==============================================================================
# @file cmake/internal/SetupEnvironment.cmake
# @brief 엔진 내부: Scripts/SetupEnvironment.py 실행 (IDE/LLVM 경로 등)
# ==============================================================================

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/SetupEnvironment.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE sw_setup_env_result
        OUTPUT_VARIABLE sw_setup_env_output
        ERROR_VARIABLE sw_setup_env_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT sw_setup_env_result EQUAL 0)
        message(WARNING
            "[SetupEnvironment] SetupEnvironment.py failed (exit ${sw_setup_env_result}).\n"
            "${sw_setup_env_output}\n${sw_setup_env_error}"
        )
    elseif(sw_setup_env_output)
        message(STATUS "${sw_setup_env_output}")
    endif()
else()
    message(WARNING "[SetupEnvironment] Python3 not found; skipping SetupEnvironment.py")
endif()
