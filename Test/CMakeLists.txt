# ==============================================================================
# @file Test/CMakeLists.txt
# @brief 단위 테스트 — 링크하는 것마다 실행 파일 하나(CoreTest · EngineTest), 각 실행 파일이 CTest 항목 하나다. 형식은 Test/README.md.
# @details 폴더 목록이 없다 — CMakeLists.txt 가 있는 하위 폴더(TestFramework 빼고)를 이름 순으로 들어간다. 배포 구성은 짓지 않는다.
# ==============================================================================

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)   # 테스트는 LTO 를 받지 않는다(링크 시간)

file(GLOB swTestFrameworkSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/TestFramework/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/TestFramework/*.h")

# ------------------------------------------------------------------------------
# sw_addTestExecutable — 이 폴더(재귀)의 테스트 소스와 틀 소스로 실행 파일 하나를 짓고 CTest 에 올린다.
#   LINK    링크할 라이브러리(Core 정적 · Engine DLL — 틀 소스를 실행 파일마다 지어 SW_API 가 그 링크에 맞게 풀린다)
#   LABELS  CTest 라벨(nogpu: GPU · 창 없이 돈다)
# ------------------------------------------------------------------------------
function(sw_addTestExecutable TEST_NAME)
	cmake_parse_arguments(ARG "" "" "LINK;LABELS" ${ARGN})
	file(GLOB_RECURSE listTestSource CONFIGURE_DEPENDS "*.cpp" "*.h")
	add_executable(${TEST_NAME} ${listTestSource} ${swTestFrameworkSources})
	target_include_directories(${TEST_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/Test")
	target_link_libraries(${TEST_NAME} PRIVATE ${ARG_LINK} sw_public_source_includes sw_global_options)
	target_compile_definitions(${TEST_NAME} PRIVATE "SW_LOG_TAG=\"Test\"" SW_TEST_INTERNAL)
	sw_setUnityBuild(${TEST_NAME} OFF)
	set_target_properties(${TEST_NAME} PROPERTIES FOLDER "Test")
	add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(${TEST_NAME} PROPERTIES LABELS "${ARG_LABELS}" TIMEOUT 300)
endfunction()

file(GLOB listTestDirectory LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT listTestDirectory)
foreach(testDirectory IN LISTS listTestDirectory)
	if(NOT testDirectory STREQUAL "TestFramework" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${testDirectory}/CMakeLists.txt")
		add_subdirectory(${testDirectory})
	endif()
endforeach()
