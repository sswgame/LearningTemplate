# ==============================================================================
# @file cmake/internal/Output.cmake
# @brief 엔진 내부: 바이너리/라이브러리 출력 경로
# @note CMAKE_INSTALL_PREFIX 는 CMakePresets.json이 담당 (여기서 덮어쓰지 않음)
# ==============================================================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${sw_output_directory}/Bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${sw_output_directory}/Lib")

foreach(sw_configuration Debug Release)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${sw_configuration} "${sw_output_directory}/Bin/${sw_configuration}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${sw_configuration} "${sw_output_directory}/Lib/${sw_configuration}")
endforeach()
