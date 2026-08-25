# ==============================================================================
# @file cmake/LoadFlagModules.cmake
# @brief Compiler/Platform/Architecture/BuildType/Options INTERFACE 모듈 (명시적 순서)
# @note cmake/Modules/Toolchain 은 VcpkgGate.cmake가 담당
# @note docs/changelog 커스텀 타겟은 internal/AuxTargets.cmake
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 플래그 모듈 순서 — 아키텍처 → 플랫폼 → 컴파일러 → BuildType → Options
#    해당하지 않는 모듈은 즉시 return
#    sw_flag_libraries를 여기서 초기화하지 않음 (VcpkgGate가 이미 넣었을 수 있음)
# ------------------------------------------------------------------------------
set(swModulesRoot "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

include("${swModulesRoot}/Architecture/X64.cmake")
include("${swModulesRoot}/Architecture/ARM64.cmake")

include("${swModulesRoot}/Platform/Windows.cmake")
include("${swModulesRoot}/Platform/Linux.cmake")
include("${swModulesRoot}/Platform/MacOS.cmake")

include("${swModulesRoot}/Compiler/MSVC.cmake")
include("${swModulesRoot}/Compiler/Clang.cmake")
include("${swModulesRoot}/Compiler/GCC.cmake")

include("${swModulesRoot}/BuildType/Debug.cmake")
include("${swModulesRoot}/BuildType/Release.cmake")

include("${swModulesRoot}/Options/CppStandard.cmake")
include("${swModulesRoot}/Options/Sanitizer.cmake")
include("${swModulesRoot}/Options/UnityBuild.cmake")

unset(swModulesRoot)
