# ==============================================================================
# @file cmake/UserConfig.cmake
# @brief 사용자가 바꿀 수 있는 빌드 옵션 / 워크스페이스 메타데이터
# @note 툴체인 세부(compiler/vcpkg overlay)는 CMakePresets.json + cmake/Modules/Toolchain/
# ==============================================================================

set(sw_workspace_name "Workspace")
set(sw_project_version "1.0.0")
set(sw_cpp_standard 20)
set(sw_output_directory "${CMAKE_CURRENT_SOURCE_DIR}/build")

# --- 사용자 토글 ---
set(sw_use_vcpkg ON CACHE BOOL "Use vcpkg package integration")
set(sw_enable_testing ON CACHE BOOL "Build and register unit tests")
set(sw_enable_sanitizer OFF CACHE BOOL "Enable sanitizer flag module")
set(sw_enable_pch ON CACHE BOOL "Enable Core precompiled header")
set(sw_enable_unity_build OFF CACHE BOOL "Enable unity build flag module")
option(SW_BUILD_DOCS "Generate Doxygen docs target when available" OFF)
option(SW_AUTO_CHANGELOG "Add AutoChangelog target (manual build; not part of default ALL)" OFF)

set(CMAKE_COMPILE_PDB_NAME "compile")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler / platform / architecture INTERFACE libs accumulate here.
# Must be initialized BEFORE Toolchain so vcpkg can append without being wiped later.
set(sw_flag_libraries "")
