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

	# 작업 폴더에 기대지 않는다(R5) — 루트는 실행 파일 위치로 정하고(`ProcessRoots`), 상대 경로 인자는 기동 때의 작업 폴더로 한 번 절대화한다.
	# 그래서 `Bin` 이 아닌 폴더에서 상대 `-log-file` 로 띄워도 같은 테스트가 돈다(예전에는 작업 폴더에서 위로 `Content` 를 찾았다).
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/Logs")
	add_test(NAME UnitTests.OtherWorkingFolder
		COMMAND $<TARGET_FILE:App> --test -unattended --test-filter=FileSystemTest.*,AbsolutePathTest.* -log-file=Logs/UnitTests.OtherWorkingFolder.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Testing")
	set_tests_properties(UnitTests.OtherWorkingFolder PROPERTIES LABELS nogpu TIMEOUT 300)

	# 틀린 명령줄은 기동을 멈춘다(값을 0 · false 로 바꿔 계속하던 폴백을 없앴다) — 모르는 인자 · 정수 아닌 정수 · 빈 경로.
	foreach(swBadArgument IN ITEMS "--no-such-argument" "-W=abc" "-log-file=")
		string(MAKE_C_IDENTIFIER "${swBadArgument}" swBadArgumentName)
		add_test(NAME CommandLine.Rejects${swBadArgumentName}
			COMMAND $<TARGET_FILE:App> --test -unattended ${swBadArgument}
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(CommandLine.Rejects${swBadArgumentName} PROPERTIES LABELS nogpu TIMEOUT 120 WILL_FAIL TRUE)
	endforeach()
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

# 키트 · 게임 단위 테스트 — 키트 `Tests/` 의 케이스는 그 키트를 싣는 게임으로 돌 때만 등록된다(활성 게임으로 도는 `UnitTests` 에는 없다).
# 함께 짓는 게임마다 그 게임으로 떠서 그 게임이 싣는 키트 · 게임 폴더의 케이스만 돈다(`--test-source`). 테스트 폴더가 하나도 없는 게임은 항목이 없다(빈 실행은 실패다).
get_property(swActiveGameTarget GLOBAL PROPERTY SW_ACTIVE_GAME_TARGET)
get_property(listCoBuiltGame GLOBAL PROPERTY SW_GAME_PACKS)
if(swActiveGameTarget)
	foreach(game IN LISTS listCoBuiltGame)
		get_property(listGameDependency GLOBAL PROPERTY SW_GAME_PACK_${game}_DEPENDENCIES)
		set(listTestFolder "")
		foreach(dependency IN LISTS listGameDependency)
			get_property(dependencyKind GLOBAL PROPERTY SW_MODULE_${dependency}_KIND)
			if(NOT dependencyKind STREQUAL "Kit" OR NOT TARGET ${dependency})
				continue()
			endif()
			get_target_property(kitFolder ${dependency} SOURCE_DIR)
			file(GLOB_RECURSE listKitTest "${kitFolder}/*/Tests/*.cpp" "${kitFolder}/Tests/*.cpp")
			if(listKitTest)
				file(RELATIVE_PATH kitRelative "${CMAKE_SOURCE_DIR}" "${kitFolder}")
				list(APPEND listTestFolder "${kitRelative}/")
			endif()
		endforeach()
		file(GLOB_RECURSE listGameTest "${CMAKE_SOURCE_DIR}/Source/Games/${game}/*/Tests/*.cpp" "${CMAKE_SOURCE_DIR}/Source/Games/${game}/Tests/*.cpp")
		if(listGameTest)
			list(APPEND listTestFolder "Source/Games/${game}/")
		endif()
		if(NOT listTestFolder)
			continue()
		endif()
		list(JOIN listTestFolder "," swTestSource)
		add_test(NAME UnitTests.${game}
			COMMAND $<TARGET_FILE:App> --test -unattended -game=${game} --test-source=${swTestSource} -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.${game}.log
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(UnitTests.${game} PROPERTIES LABELS nogpu TIMEOUT 600)
	endforeach()
endif()

# 편집기 자체 테스트(`SW_EDITOR_SELF_TEST`)와 편집기 단위 테스트는 따로 된 항목이 없다 — 시나리오 단계 `<EditorSelfTest>`(`engine/automation/editor/selftests.scenario.xml`)로
# `Scenario.editor` 체인 안에서 돌아 백엔드 넷과 로그 게이트를 함께 받는다(예전 EditorTests 는 기본 백엔드에서 케이스 성패만 봐 로그로만 드러나는 결함을 놓쳤다).
if(TARGET EditorModule)

	# 헤드리스 도구 명령 하나 — 현지화 표가 코드 · 데이터와 맞는지 보고, 도구 실행의 기동 · 종료 길을 지난다(모듈이 내려갈 때 서비스를 찾다 죽던 결함이
	# 모든 도구 명령에 있었는데 위 항목들은 도구 명령을 돌리지 않아 숨었다). 임포트 검사는 산출물을 커밋하지 않아 새 체크아웃에서 늘 지므로 이것을 고른다.
	add_test(NAME ToolCheckText
		COMMAND $<TARGET_FILE:App> --check-text -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/ToolCheckText.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(ToolCheckText PROPERTIES LABELS nogpu TIMEOUT 300)

	# 초상화 굽기 — 스튜디오 씬 → 패킷(게임 스레드)과 그리기 · 읽기(렌더 스레드 명령 `RenderThread::executeAndWait`)를 한 번 지난다.
	# 예전에는 게임 스레드가 렌더 스레드를 세우고 직접 그렸고, 그 길을 도는 테스트가 없었다. 창 · GPU 가 필요해 hostgpu 다.
	add_test(NAME Bake.Portraits
		COMMAND $<TARGET_FILE:App> --render-portraits=common/prefabs/blaster_a.prefab.xml -portrait-size=64 -portrait-dir=${CMAKE_BINARY_DIR}/Testing/Portraits -unattended
			-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/Bake.Portraits.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(Bake.Portraits PROPERTIES LABELS hostgpu TIMEOUT 300)
endif()

# 온라인 부하 봇의 짧은 시나리오 — 같은 프로세스 서버(`--local-server=0`, OS 가 고른 포트라 나란히 돌아도 다투지 않는다)에 봇 여덟이 붙어
# 로그인 · 채팅 · 순위를 한 번씩 돈다. 오류율 · 보고서 쓰기 실패가 종료 코드다(엔진 · 모듈이 서지 못하면 3). 봇 로그는 `-log-file` 로 같은 폴더에 남는다.
if(TARGET OnlineLoadBot)
	set(swLoadBotDir "${CMAKE_SOURCE_DIR}/Source/Programs/OnlineLoadBot")
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/Logs")
	add_test(NAME OnlineLoadBot.localServer
		COMMAND $<TARGET_FILE:OnlineLoadBot>
			--scenario=${swLoadBotDir}/Scenarios/smoke.json
			--load=${swLoadBotDir}/Scenarios/smoke.load.xml
			--local-server=0
			--online-config=${swLoadBotDir}/LoadBot.online.json
			--light-hash
			--report=${CMAKE_BINARY_DIR}/Testing/Logs/OnlineLoadBot.localServer.json
			-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/OnlineLoadBot.localServer.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(OnlineLoadBot.localServer PROPERTIES LABELS nogpu TIMEOUT 120)
endif()

add_subdirectory(Scenario)
