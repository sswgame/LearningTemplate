# ==============================================================================
# @file cmake/internal/SetupEnvironment.cmake
# @brief Scripts/setup/SetupEnvironment.py 실행 (IDE/LLVM 경로 → engine_config.json)
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/Python.cmake")

set(_sw_setup_args "")
if(SW_VCPKG_AUTO_BOOTSTRAP)
    # FindVcpkg inside SetupEnvironment stays bootstrap-off; env flag for explicit tools.
    set(ENV{SW_VCPKG_AUTO_BOOTSTRAP} "1")
endif()

sw_execute_python_script(
    "Scripts/setup/SetupEnvironment.py"
    WARN
    RESULT_VARIABLE sw_setup_env_result
)
