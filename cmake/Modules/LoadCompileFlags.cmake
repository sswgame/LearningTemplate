# ==============================================================================
# @file cmake/Modules/LoadCompileFlags.cmake
# @brief Compiler/Platform/Architecture/BuildType/Options INTERFACE 모듈 (명시적 순서)
# @note cmake/Modules/Toolchain 은 VcpkgIntegration.cmake가 담당
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 플래그 모듈 순서 — 아키텍처 → 플랫폼 → 컴파일러 → BuildType → Options
# 해당하지 않는 모듈은 즉시 return
# ------------------------------------------------------------------------------
set(swModulesRoot "${CMAKE_CURRENT_LIST_DIR}")

include("${swModulesRoot}/Architecture/ARM64.cmake")
include("${swModulesRoot}/Architecture/X64.cmake")

include("${swModulesRoot}/Platform/Linux.cmake")
include("${swModulesRoot}/Platform/MacOS.cmake")
include("${swModulesRoot}/Platform/Windows.cmake")

include("${swModulesRoot}/Compiler/Clang.cmake")
include("${swModulesRoot}/Compiler/GCC.cmake")
include("${swModulesRoot}/Compiler/MSVC.cmake")

include("${swModulesRoot}/BuildType/Debug.cmake")
include("${swModulesRoot}/BuildType/Release.cmake")

include("${swModulesRoot}/Options/CppStandard.cmake")
include("${swModulesRoot}/Options/Sanitizer.cmake")
include("${swModulesRoot}/Options/UnityBuild.cmake")

unset(swModulesRoot)
