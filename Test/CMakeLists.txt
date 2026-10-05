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
# 에디터 · App 시험은 클라이언트 코드다 — 전용 서버 타깃(Server)에는 에디터 · App · 창이 없어 짓지 않는다.
if(SW_TARGET_TYPE STREQUAL "Server")
	sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}/EditorTest" "${CMAKE_CURRENT_SOURCE_DIR}/EditorUiTest" "${CMAKE_CURRENT_SOURCE_DIR}/AppTest")
else()
	add_subdirectory(EditorTest)
	add_subdirectory(EditorUiTest)
	add_subdirectory(AppTest)
endif()
add_subdirectory(ServerTest)
add_subdirectory(PythonTest)
add_subdirectory(FuzzTest)

target_compile_definitions(TestFramework PUBLIC SW_TEST_INTERNAL)
