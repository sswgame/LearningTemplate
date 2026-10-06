# ==============================================================================
# @file Test/CMakeLists.txt
# @brief 시험 — TestFramework 다음, CMakeLists.txt 가 있는 하위 폴더 전부(목록이 없다)
# ==============================================================================

# ------------------------------------------------------------------------------
# 0) 테스트 타겟들은 LTO(IPO)를 적용하지 않음 (빌드 속도 저하 방지)
# ------------------------------------------------------------------------------
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)

# ------------------------------------------------------------------------------
# 1) 시험 폴더 — TestFramework 가 먼저다(나머지가 링크한다). 그 밖의 폴더는 **목록이 없다** — CMakeLists.txt 가 있는 하위 폴더를
#    이름 순서로 들어간다. 어떤 구성에서 짓지 않는 폴더는 그 폴더의 CMakeLists 머리가 스스로 "짓지 않는다" 로 적고 돌아간다
#    (에디터 · App 시험은 Server 타깃에서 — 각 폴더 머리).
# ------------------------------------------------------------------------------
add_subdirectory(TestFramework)
file(GLOB listTestDirectory LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT listTestDirectory)
foreach(testDirectory IN LISTS listTestDirectory)
	if(NOT testDirectory STREQUAL "TestFramework" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${testDirectory}/CMakeLists.txt")
		add_subdirectory(${testDirectory})
	endif()
endforeach()

target_compile_definitions(TestFramework PUBLIC SW_TEST_INTERNAL)
