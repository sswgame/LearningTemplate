# ==============================================================================
# @file Test/CMakeLists.txt
# @brief CTest 등록만 한다 — 테스트 코드는 여기 없다. 단위 테스트는 기능 폴더 옆 `Tests/`(`SW_TEST_CASE`)에 있고 `App --test` 한 번이 모두 돈다.
#        시나리오는 `Content/**/automation/*.scenario.xml` 이고 `Test/Scenario/` 가 하나씩 올린다. 형식과 쓰는 법은 Test/README.md.
# ==============================================================================

if(NOT TARGET App)
	return()   # 전용 서버 타깃은 App 을 짓지 않는다
endif()

# 단위 테스트 — 헤드리스(창 · GPU 없음)라 nogpu. 순서는 매번 섞이고 씨앗을 찍는다. 보고는 실행마다 다른 폴더(`Saved/Test/<프로세스>/report.json`, 찍힌다).
add_test(NAME UnitTests
	COMMAND $<TARGET_FILE:App> --test -unattended
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
set_tests_properties(UnitTests PROPERTIES LABELS nogpu TIMEOUT 600)

add_subdirectory(Scenario)
