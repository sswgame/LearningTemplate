# ==============================================================================
# @file cmake/Modules/LoadCompileFlags.cmake
# @brief Compiler/Platform/Architecture/BuildType/Options INTERFACE 모듈 (명시적 순서)
# @note cmake/Modules/Toolchain 은 루트 CMakeLists.txt 의 vcpkg 게이트가 include 한다
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 플래그 모듈 순서 — 아키텍처 → 플랫폼 → 컴파일러 → BuildType → Options
# 해당하지 않는 모듈은 즉시 return
# ------------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/Architecture/DetectArchitecture.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Architecture/ARM64.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Architecture/X64.cmake")

# 지원 플랫폼은 Windows · Linux 둘이다. macOS 는 지원하지 않는다 — 플랫폼 매크로(SW_PLATFORM_*)를 정할 모듈이 없으므로 여기서 멈춘다.
if(APPLE)
	message(FATAL_ERROR "[Platform] macOS is not a supported platform. The engine builds on Windows and Linux only.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/Platform/Linux.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Platform/Windows.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/Compiler/Clang.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Compiler/GCC.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Compiler/MSVC.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/BuildType/Debug.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BuildType/Release.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/Options/CppStandard.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Options/Sanitizer.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Options/Fuzzing.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Options/UnityBuild.cmake")

unset(swModulesRoot)
