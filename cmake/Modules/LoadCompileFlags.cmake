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

include("${swModulesRoot}/Architecture/DetectArchitecture.cmake")
include("${swModulesRoot}/Architecture/ARM64.cmake")
include("${swModulesRoot}/Architecture/X64.cmake")

# 지원 플랫폼은 Windows · Linux 둘이다. macOS 는 지원하지 않는다 — 플랫폼 매크로(SW_PLATFORM_*)를 정할 모듈이 없으므로 여기서 멈춘다.
if(APPLE)
	message(FATAL_ERROR "[Platform] macOS is not a supported platform. The engine builds on Windows and Linux only.")
endif()

include("${swModulesRoot}/Platform/Linux.cmake")
include("${swModulesRoot}/Platform/Windows.cmake")

include("${swModulesRoot}/Compiler/Clang.cmake")
include("${swModulesRoot}/Compiler/GCC.cmake")
include("${swModulesRoot}/Compiler/MSVC.cmake")

include("${swModulesRoot}/BuildType/Debug.cmake")
include("${swModulesRoot}/BuildType/Release.cmake")

include("${swModulesRoot}/Options/CppStandard.cmake")
include("${swModulesRoot}/Options/Sanitizer.cmake")
include("${swModulesRoot}/Options/Fuzzing.cmake")
include("${swModulesRoot}/Options/UnityBuild.cmake")

unset(swModulesRoot)
