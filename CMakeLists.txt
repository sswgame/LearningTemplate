# ==============================================================================
# @file CMakeLists.txt
# @brief SW Engine 최상위 루트 빌드 스크립트
# ==============================================================================
#
# cmake/ 레이아웃:
#   UserConfig.cmake          — SW_* 사용자 옵션
#   VcpkgGate.cmake           — SW_USE_VCPKG 시 Modules/Toolchain/Vcpkg 로드
#                               (≠ CMAKE_TOOLCHAIN_FILE; preset이 Tools/vcpkg 지정)
#   LoadFlagModules.cmake     — Compiler/Platform/… INTERFACE (명시 include)
#   Modules/Toolchain/        — vcpkg 게이트 세부·overlay triplet
#   Modules/{Compiler,…}      — 플래그 INTERFACE (LoadFlagModules)
#   internal/                 — 타겟/탐색/출력/VcpkgRuntime/shipping/reflection/Python
#
# ==============================================================================

cmake_minimum_required(VERSION 3.24)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# --- 사용자 설정 + 환경 준비 (project 이전) ---
include(cmake/UserConfig.cmake)
include(cmake/internal/SetupEnvironment.cmake)
include(cmake/VcpkgGate.cmake)

project(
    ${sw_workspace_name}
    VERSION ${sw_project_version}
    LANGUAGES C CXX
)

# Manifest install 후 스탬프 기록 (다음 configure에서 skip)
include(cmake/internal/VcpkgManifestStamp.cmake)
# Linux: vcpkg vulkan-loader → system loader symlink (Scripts)
include(cmake/internal/VcpkgHostFixes.cmake)

# --- 플래그 모듈 + 엔진 내부 헬퍼 ---
include(cmake/LoadFlagModules.cmake)
include(cmake/internal/BuildConfig.cmake)
include(cmake/internal/Discover.cmake)
include(cmake/internal/Install.cmake)
include(cmake/internal/Targets.cmake)
include(cmake/internal/VcpkgRuntime.cmake)
include(cmake/internal/Output.cmake)
include(cmake/internal/Reflection.cmake)
include(cmake/internal/ThirdPartyWarnings.cmake)

# ThirdParty 서브트리 (경고 헬퍼는 ThirdPartyWarnings.cmake)
add_library(sw_third_party_includes INTERFACE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
    add_subdirectory(ThirdParty)
endif()

include(cmake/internal/AuxTargets.cmake)

# --- 타겟 그래프 ---
# Source (Game 제외) → Tools(ReflectionParser) → Game → App 연결 → Test
add_subdirectory(Source)

add_subdirectory(Tools)

if(SW_BUILD_GAME)
    add_subdirectory(Source/Game)
endif()

if(TARGET App AND TARGET SWGame)
    if(SW_SHIPPING_BUILD)
        target_link_libraries(App PRIVATE SWGame)
    else()
        add_dependencies(App SWGame)
    endif()
endif()

if(SW_ENABLE_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test")
    include(CTest)
    enable_testing()
    add_subdirectory(Test)
endif()
