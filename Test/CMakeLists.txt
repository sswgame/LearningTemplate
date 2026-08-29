# ==============================================================================
# @file Test/CMakeLists.txt
# @brief 단위 테스트 그래프 (명시 등록)
# ==============================================================================

# ------------------------------------------------------------------------------
# 0) 테스트 타겟들은 LTO(IPO)를 적용하지 않음 (빌드 속도 저하 방지)
# ------------------------------------------------------------------------------
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)

# ------------------------------------------------------------------------------
# 1) 테스트 순서 — TestFramework 먼저, 나머지가 링크
# ------------------------------------------------------------------------------
add_subdirectory(TestFramework)
add_subdirectory(CoreTest)
add_subdirectory(EngineTest)
add_subdirectory(ReflectionTest)
add_subdirectory(SmokeTest)
add_subdirectory(EditorTest)

target_compile_definitions(TestFramework PUBLIC SW_TEST_INTERNAL)
