# ==============================================================================
# @file cmake/internal/Output.cmake
# @brief 엔진 내부: 바이너리/라이브러리 출력 경로 (preset별 CMAKE_BINARY_DIR 하위)
# @note CMAKE_INSTALL_PREFIX 는 CMakePresets.json이 담당 (여기서 덮어쓰지 않음)
# @note Ninja single-config presets → flat Bin/Lib (LiveReload MODULE과 동일 레이아웃)
# @note sw_output_directory == CMAKE_BINARY_DIR → e.g. build/Ninja-Debug/Bin
# ==============================================================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${sw_output_directory}/Bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${sw_output_directory}/Lib")
# Multi-config generators도 flat 유지 (Bin/Debug 분기 없음 — MODULE LiveReload와 맞춤)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Lib")
