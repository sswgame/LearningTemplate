# ==============================================================================
# @file CMakeLists.txt
# @brief SW Engine 최상위 루트 빌드 스크립트
# ==============================================================================
#
# cmake/ 레이아웃:
#   UserConfig.cmake          — 사용자 옵션
#   Toolchain.cmake           — 사용자 툴체인 게이트 (vcpkg)
#   Modules/Toolchain/        — 사용자 툴체인 세부 (overlay triplet, ports)
#   Modules/{Compiler,...}    — 플래그 INTERFACE (LoadFlagModules가 로드)
#   internal/                 — 엔진 타겟/탐색/출력/install/shipping/reflection
#
# ==============================================================================

cmake_minimum_required(VERSION 3.24)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# --- 사용자 설정 + 환경 준비 (project 이전) ---
include(cmake/UserConfig.cmake)
include(cmake/internal/SetupEnvironment.cmake)
include(cmake/Toolchain.cmake)

project(
    ${sw_workspace_name}
    VERSION ${sw_project_version}
    LANGUAGES C CXX
)

# --- 플래그 모듈 + 엔진 내부 헬퍼 ---
include(cmake/LoadFlagModules.cmake)
include(cmake/internal/BuildConfig.cmake)
include(cmake/internal/Discover.cmake)
include(cmake/internal/Install.cmake)
include(cmake/internal/Targets.cmake)
include(cmake/internal/Output.cmake)
include(cmake/internal/Reflection.cmake)
include(cmake/Dependencies.cmake)
include(cmake/internal/Scripts.cmake)

# --- 타겟 그래프 ---
# Source (Game 제외) → Tools(ReflectionParser) → Game → App 연결 → Test
add_subdirectory(Source)

add_subdirectory(Tools)

option(sw_build_Game "Build project Game" ON)
if(sw_build_Game)
    add_subdirectory(Source/Game)
endif()

if(TARGET App AND TARGET SWGame)
    if(SW_SHIPPING_BUILD)
        target_link_libraries(App PRIVATE SWGame)
    else()
        add_dependencies(App SWGame)
    endif()
endif()

if(sw_enable_testing AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test")
    include(CTest)
    enable_testing()
    add_subdirectory(Test)
endif()
