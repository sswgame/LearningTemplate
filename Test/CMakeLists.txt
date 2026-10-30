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

# 편집기 자체 테스트 — 편집기를 띄워 자체 테스트(`SW_EDITOR_SELF_TEST`) 모두와 편집기 단위 테스트(`Source/Editor/**/Tests/`, 자체 테스트 `editor.unitTests` 가 돌린다)를 돌리고
# 결과를 종료 코드로 낸다(`QuitRequest` — 하나라도 지면 실패 코드). 편집기 단위 테스트는 엔진의 `App --test` 가 돌지 못한다 — 편집기 모듈이 기동 뒤에 올라온다.
# 창과 GPU 가 있어야 하므로 hostgpu. 배포 · 전용 서버 구성에는 편집기가 없다.
if(TARGET EditorModule)
	add_test(NAME EditorTests
		COMMAND $<TARGET_FILE:App> -EnableEditor -gv_editorSelfTest=* -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/EditorTests.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(EditorTests PROPERTIES LABELS hostgpu TIMEOUT 900 SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped})

	# 헤드리스 도구 명령 하나 — 현지화 표가 코드 · 데이터와 맞는지 보고, 도구 실행의 기동 · 종료 길을 지난다(모듈이 내려갈 때 서비스를 찾다 죽던 결함이
	# 모든 도구 명령에 있었는데 위 항목들은 도구 명령을 돌리지 않아 숨었다). 임포트 검사는 산출물을 커밋하지 않아 새 체크아웃에서 늘 지므로 이것을 고른다.
	add_test(NAME ToolCheckText
		COMMAND $<TARGET_FILE:App> --check-text -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/ToolCheckText.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(ToolCheckText PROPERTIES LABELS nogpu TIMEOUT 300)
endif()

add_subdirectory(Scenario)
