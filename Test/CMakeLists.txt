# ==============================================================================
# @file Test/CMakeLists.txt
# @brief CTest 등록만 한다 — 테스트 코드는 여기 없다. 단위 테스트는 기능 폴더 옆 `Tests/`(`SW_TEST_CASE`)에 있고 `App --test` 한 번이 모두 돈다.
#        시나리오는 `Content/**/automation/*.scenario.xml` 이고 `Test/Scenario/` 가 하나씩 올린다. 형식과 쓰는 법은 Test/README.md.
# ==============================================================================

if(NOT TARGET App)
	return()   # 전용 서버 타깃은 App 을 짓지 않는다
endif()

# 단위 테스트 — 헤드리스(창 · GPU 없음)라 nogpu. 순서는 매번 섞이고 씨앗을 찍는다. 보고는 실행마다 다른 폴더(`Saved/Test/<프로세스>/report.json`, 찍힌다).
# 몫(SW_UNIT_TEST_SHARDS)이 1 보다 크면 케이스를 이름 해시로 나눠 몫마다 프로세스 하나(`UnitTests.<i>`)가 돈다 — `ctest -j` 가 나란히 돌린다.
# 몫마다 엔진 기동(약 1 초)이 붙으므로 케이스 시간이 기동보다 커질 때 올린다. 지금(케이스 수십 개 · 합계 수십 ms)은 1 이다.
set(SW_UNIT_TEST_SHARDS 1 CACHE STRING "단위 테스트를 나눠 돌릴 프로세스 수(App --test --test-shard=i/n)")
if(SW_UNIT_TEST_SHARDS LESS_EQUAL 1)
	add_test(NAME UnitTests
		COMMAND $<TARGET_FILE:App> --test -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(UnitTests PROPERTIES LABELS nogpu TIMEOUT 600)
else()
	math(EXPR swLastShard "${SW_UNIT_TEST_SHARDS} - 1")
	foreach(shardIndex RANGE ${swLastShard})
		add_test(NAME UnitTests.${shardIndex}
			COMMAND $<TARGET_FILE:App> --test -unattended --test-shard=${shardIndex}/${SW_UNIT_TEST_SHARDS}
				-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.${shardIndex}.log
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(UnitTests.${shardIndex} PROPERTIES LABELS nogpu TIMEOUT 600)
	endforeach()
endif()

add_subdirectory(Scenario)
