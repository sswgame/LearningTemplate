# ==============================================================================
# @file Test/CMakeLists.txt
# @brief Test 디렉토리 하위의 모든 단위 테스트 프로젝트를 자동 탐색하여 빌드 트리에 추가
# ==============================================================================

# cmake/internal/Discover.cmake: 현재 디렉토리 기준 하위 CMakeLists.txt를 재귀 탐색
sw_discover_projects("${CMAKE_CURRENT_SOURCE_DIR}")
