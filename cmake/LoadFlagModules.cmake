# ==============================================================================
# @file cmake/LoadFlagModules.cmake
# @brief Compiler/Platform/Architecture/BuildType/Options INTERFACE 모듈 (명시적 순서)
# @note cmake/Modules/Toolchain 은 VcpkgGate.cmake가 담당
# @note docs/changelog 커스텀 타겟은 internal/AuxTargets.cmake
# ==============================================================================

# Do NOT reset sw_flag_libraries — VcpkgGate may already have appended entries.
set(_sw_modules_root "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

# Architecture → Platform → Compiler → BuildType → Options
# (각 파일은 해당하지 않으면 early return)
include("${_sw_modules_root}/Architecture/X64.cmake")
include("${_sw_modules_root}/Architecture/ARM64.cmake")

include("${_sw_modules_root}/Platform/Windows.cmake")
include("${_sw_modules_root}/Platform/Linux.cmake")
include("${_sw_modules_root}/Platform/MacOS.cmake")

include("${_sw_modules_root}/Compiler/MSVC.cmake")
include("${_sw_modules_root}/Compiler/Clang.cmake")
include("${_sw_modules_root}/Compiler/GCC.cmake")

include("${_sw_modules_root}/BuildType/Debug.cmake")
include("${_sw_modules_root}/BuildType/Release.cmake")

include("${_sw_modules_root}/Options/CppStandard.cmake")
include("${_sw_modules_root}/Options/Sanitizer.cmake")
include("${_sw_modules_root}/Options/UnityBuild.cmake")

unset(_sw_modules_root)
