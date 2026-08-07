# ==============================================================================
# @file Tools/CMakeLists.txt
# @brief Tools 디렉토리 하위의 엔진 파이프라인/빌드 보조 도구 모듈 자동 탐색
# ==============================================================================

# cmake/internal/Discover.cmake: 현재 디렉토리 기준 하위 CMakeLists.txt를 재귀 탐색
sw_discover_projects("${CMAKE_CURRENT_SOURCE_DIR}")
