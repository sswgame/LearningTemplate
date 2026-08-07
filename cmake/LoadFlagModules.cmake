# ==============================================================================
# @file cmake/LoadFlagModules.cmake
# @brief 엔진 내부: Compiler/Platform/Architecture/BuildType/Options INTERFACE 모듈 로드
# @note cmake/Modules/Toolchain 은 여기 포함하지 않음 (Toolchain.cmake가 담당)
# ==============================================================================

# Do NOT reset sw_flag_libraries — Toolchain(vcpkg) may already have appended entries.

file(
    GLOB_RECURSE sw_flag_module_files
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/Architecture/*.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/BuildType/*.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/Compiler/*.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/Options/*.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/Platform/*.cmake"
)

foreach(sw_flag_module_file IN LISTS sw_flag_module_files)
    include(${sw_flag_module_file})
endforeach()
