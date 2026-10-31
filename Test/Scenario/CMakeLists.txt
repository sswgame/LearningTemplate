# ==============================================================================
# @file Test/Scenario/CMakeLists.txt
# @brief 시나리오를 CTest 항목으로 — 이 구성이 짓는 게임마다(활성 게임 `SW_ACTIVE_GAME` + 함께 짓는 게임 `SW_GAMES`) 그 게임의 `Content/game/<팩>/automation/` 바로 아래
#        `*.scenario.xml` 모두를 체인 하나(`Scenario.<팩>` — `App -scenario=a,b,c -scenario-keep-going -scenario-backends=all -unattended`, 라벨 hostgpu — 창과 GPU 가 있는 PC 에서)로 돌린다.
#        엔진 시나리오(`Content/engine/automation/`)는 활성 게임의 체인에 든다. 활성 게임이 아닌 게임은 `-game=<게임>` 을 붙인다(App 이 그 게임 팩 `Bin/Games/<게임>` 을 싣는다).
#        목록이 없다 — 폴더를 훑는다. 팩 이름은 게임 프리셋(`Config/Game/<게임>.json` 의 `_packRoot`)이 정한다.
#        그 App 은 창 · GPU 없이 백엔드마다 자식 App 을 하나씩 동시에 띄우고 결과를 모은다(`AutomationBackendMatrix` — 언리얼 Gauntlet 의 컨트롤러 자리).
#        자식 하나가 시나리오를 차례로 돌고, 단계마다 전역 변수를 되돌리고 `<Launch>` 값을 건 뒤 게임을 처음부터 다시 세운다(`AutomationSession::startStage` — 언리얼 PIE 가
#        플레이마다 새 GameInstance 를 만들고 CVar 를 되돌리는 것과 같다). 첫 씬을 정하는 것은 게임의 기동 흐름이라 벤치(`gv_stressTest*`)도 체인에 든다. 프로세스는 백엔드 수 + 부모 하나다. 시나리오 하나만 돌리려면 `App -scenario=<경로>` 를 직접 띄운다.
#        골든이 백엔드별이라 자식마다 제 골든과 견준다. 이 PC 에서 못 도는 백엔드는 그 백엔드만 건너뛴다.
# @details 종료 코드는 표 하나(`Config/Engine/ProcessExitCodes.json` → `SW_EXIT_CODE_<이름>`)다 — 건너뜀(`SW_EXIT_CODE_Skipped`)은 CTest 의 건너뜀이다. 행렬은 첫 실패 백엔드의 코드를 낸다.
#          단계별 결과는 `<빌드>/Testing/Logs/Scenario.<팩>.<백엔드>.log` 의 `chain stage i/n` 줄에 있다.
#          하위 폴더(예: `rhiswap/` — 네 백엔드가 모두 있어야 한다)는 조건이 붙은 시나리오라 체인이 줍지 않는다 — 손으로 돌린다.
#          게임의 하위 폴더 다섯은 게임마다 같은 `-game=` 규칙으로 따로 항목이 된다: `showcase/`(쇼케이스 체인 `Scenario.<팩>.showcase`), `desktop/`(혼자 도는 데스크톱 시나리오),
#          `sequence/`(두 번 기동 쌍), `editor/`(그 게임의 편집기 체인 `Scenario.<팩>.editor`), `load/`(부하 측정 — 라벨 load, 엔진 폴더도).
#          항목이 GPU 자식을 백엔드 수만큼 띄우므로 그 수를 슬롯으로 선언한다(`PROCESSORS`) — `ctest -j` 의 다른 항목(단위 테스트)과 겹쳐도 GPU 자식이 늘지 않는다.
# ==============================================================================

if(NOT TARGET App)
	return()   # 전용 서버 타깃은 App 을 짓지 않는다
endif()

list(LENGTH SW_RHI_BACKEND_NAMES swScenarioBackendCount) # `-scenario-backends=all` 이 띄우는 자식 수

set(swScenarioRoot "${CMAKE_SOURCE_DIR}/Content")
# 시나리오의 `<Launch>` 를 명령줄 인자(`-이름=값` 목록)로 읽는다.
function(sw_readScenarioLaunch scenarioPath out)
	file(STRINGS "${swScenarioRoot}/${scenarioPath}" listLaunchLine REGEX "^[ 	]*<Launch ")
	set(listArgument "")
	foreach(launchLine IN LISTS listLaunchLine)
		if(NOT launchLine MATCHES "variable=\"([^\"]+)\"[ 	]+value=\"([^\"]*)\"")
			message(FATAL_ERROR "${scenarioPath}: <Launch> must be <Launch variable=\"…\" value=\"…\"/> on one line — ${launchLine}")
		endif()
		list(APPEND listArgument "-${CMAKE_MATCH_1}=${CMAKE_MATCH_2}")
	endforeach()
	set(${out} "${listArgument}" PARENT_SCOPE)
endfunction()

# 체인 항목 하나 — 시나리오 모두를 백엔드마다 한 프로세스로. 모듈 리로드(이름 `*modulereload`)는 코드 이미지를 갈아 끼우므로 맨 뒤에 둔다 — 그 단계가 무엇을 남겨도 다른 단계에 닿지 않는다.
# 첫 단계의 `<Launch>` 는 명령줄로 준다(게임이 기동하며 그 값을 읽는다). 다음 단계의 값은 세션이 단계를 시작할 때 건다. ARGS 는 App 에 더 줄 인자(`-game=` · `-EnableEditor` …)다.
function(sw_addScenarioChain testName stageSeconds)
	cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "SCENARIOS;ARGS")
	set(listScenario ${arg_SCENARIOS})
	if(NOT listScenario)
		return()
	endif()
	list(SORT listScenario)
	set(listReloadScenario ${listScenario})
	list(FILTER listReloadScenario INCLUDE REGEX "modulereload\\.scenario\\.xml$")
	list(FILTER listScenario EXCLUDE REGEX "modulereload\\.scenario\\.xml$")
	list(APPEND listScenario ${listReloadScenario})
	list(LENGTH listScenario stageCount)
	list(GET listScenario 0 firstPath)
	sw_readScenarioLaunch("${firstPath}" listLaunchArgument)
	list(JOIN listScenario "," scenarioList)
	math(EXPR chainTimeout "${stageSeconds} * ${stageCount}")
	add_test(NAME ${testName}
		COMMAND $<TARGET_FILE:App> ${arg_ARGS} -scenario=${scenarioList} -scenario-keep-going -scenario-backends=all -unattended
			-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/${testName}.log ${listLaunchArgument}
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(${testName} PROPERTIES LABELS hostgpu TIMEOUT ${chainTimeout} SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped} PROCESSORS ${swScenarioBackendCount})
	message(STATUS "[Scenario] ${testName}: ${stageCount} scenario(s)")
endfunction()

# 부하 시나리오 하나 — 측정 창(`<Load durationSeconds="N"/>`)이 끝내는 성능 측정이라 체인에 넣지 않고 기본 백엔드 한 프로세스로 혼자 돈다
# (RUN_SERIAL — 다른 항목이 CPU · GPU 를 나눠 쓰면 잰 값이 흔들린다). 라벨은 `load` 하나라 기본 실행(`-L hostgpu` · `-L nogpu`)에 들지 않고
# `ctest -L load` 로 돈다(언리얼 Gauntlet 의 성능 테스트가 따로 도는 것과 같다). 시한은 측정 창 + 기동 여유다. 이름은 Scenario.<팩>.load.<이름> · Scenario.engine.load.<이름>.
function(sw_addLoadScenario loadPath)
	file(STRINGS "${swScenarioRoot}/${loadPath}" listLoadLine REGEX "<Load ")
	if(NOT listLoadLine MATCHES "durationSeconds=\"([0-9]+)\"")
		message(FATAL_ERROR "${loadPath}: a load scenario needs <Load durationSeconds=\"N\"/> (whole seconds) on one line")
	endif()
	math(EXPR loadTimeout "${CMAKE_MATCH_1} + 300")
	string(REGEX REPLACE "^(engine|game/[^/]+)/automation/load/(.+)\\.scenario\\.xml$" "\\1.load.\\2" loadName "${loadPath}")
	string(REPLACE "game/" "" loadName "${loadName}")
	sw_readScenarioLaunch("${loadPath}" listLoadLaunch)
	set(loadTestName Scenario.${loadName})
	add_test(NAME ${loadTestName}
		COMMAND $<TARGET_FILE:App> ${ARGN} -scenario=${loadPath} -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/${loadTestName}.log ${listLoadLaunch}
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(${loadTestName} PROPERTIES LABELS load TIMEOUT ${loadTimeout} RUN_SERIAL TRUE SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped})
endfunction()

file(GLOB listEngineLoadScenario RELATIVE "${swScenarioRoot}" CONFIGURE_DEPENDS "${swScenarioRoot}/engine/automation/load/*.scenario.xml")
foreach(loadPath IN LISTS listEngineLoadScenario)
	sw_addLoadScenario("${loadPath}")
endforeach()

# 이 구성이 짓는 게임 — 활성 게임은 그 게임 모듈 타깃(`SW_ACTIVE_GAME_TARGET`)이 있을 때, 함께 짓는 게임은 `SW_GAME_PACKS`(`sw_selectGamePacks`)다.
get_property(swActiveGameTarget GLOBAL PROPERTY SW_ACTIVE_GAME_TARGET)
set(listBuiltGame "")
if(swActiveGameTarget)
	get_property(listBuiltGame GLOBAL PROPERTY SW_GAME_PACKS)
	list(PREPEND listBuiltGame ${SW_ACTIVE_GAME})
endif()

foreach(game IN LISTS listBuiltGame)
	sw_requireJSONValue(gamePackRoot "${CMAKE_SOURCE_DIR}/${SW_DIR_RUNTIME_GAME_PRESET}/${game}.json" _packRoot) # `game/<팩>`
	get_filename_component(pack "${gamePackRoot}" NAME)
	set(gameArgument "")
	if(NOT game STREQUAL SW_ACTIVE_GAME)
		set(gameArgument "-game=${game}")
	endif()
	set(automationDir "${swScenarioRoot}/${gamePackRoot}/automation")

	# 게임 체인 — 활성 게임의 체인에는 엔진 시나리오도 든다(엔진 시나리오는 게임 하나로 한 번 돈다).
	set(listChainGlob "${automationDir}/*.scenario.xml")
	if(game STREQUAL SW_ACTIVE_GAME)
		list(APPEND listChainGlob "${swScenarioRoot}/engine/automation/*.scenario.xml")
	endif()
	file(GLOB listScenario RELATIVE "${swScenarioRoot}" CONFIGURE_DEPENDS ${listChainGlob})
	sw_addScenarioChain(Scenario.${pack} 300 SCENARIOS ${listScenario} ARGS ${gameArgument})

	# 쇼케이스 — 쇼케이스 씬 하나를 정해진 시각에 찍는 시나리오. 게임 체인과 같은 길(백엔드 넷)이되 제 씬 · 제 골든(`showcase/golden/`)을 두는 묶음이라 따로 항목이 된다.
	file(GLOB listShowcaseScenario RELATIVE "${swScenarioRoot}" CONFIGURE_DEPENDS "${automationDir}/showcase/*.scenario.xml")
	sw_addScenarioChain(Scenario.${pack}.showcase 300 SCENARIOS ${listShowcaseScenario} ARGS ${gameArgument})

	# 데스크톱 시나리오 — OS 전역 상태(커서 클립 · 전경)를 쓰거나 프로세스를 끝내므로 체인에 넣지 못하고(백엔드 넷이 동시에 돈다)
	# 하나씩 기본 백엔드 한 프로세스로 혼자 돈다(RUN_SERIAL — 다른 항목의 창이 전경을 빼앗지 않게). 언리얼 Gauntlet 이 데스크톱 테스트를 따로 도는 것과 같다.
	file(GLOB listDesktopScenario RELATIVE "${swScenarioRoot}" CONFIGURE_DEPENDS "${automationDir}/desktop/*.scenario.xml")
	foreach(desktopPath IN LISTS listDesktopScenario)
		get_filename_component(desktopName "${desktopPath}" NAME)
		string(REGEX REPLACE "\\.scenario\\.xml$" "" desktopName "${desktopName}")
		sw_readScenarioLaunch("${desktopPath}" listDesktopLaunch)
		set(desktopTestName Scenario.${pack}.desktop.${desktopName})
		add_test(NAME ${desktopTestName}
			COMMAND $<TARGET_FILE:App> ${gameArgument} -scenario=${desktopPath} -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/${desktopTestName}.log ${listDesktopLaunch}
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(${desktopTestName} PROPERTIES LABELS hostgpu TIMEOUT 300 RUN_SERIAL TRUE SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped})
	endforeach()

	file(GLOB listGameLoadScenario RELATIVE "${swScenarioRoot}" CONFIGURE_DEPENDS "${automationDir}/load/*.scenario.xml")
	foreach(loadPath IN LISTS listGameLoadScenario)
		sw_addLoadScenario("${loadPath}" ${gameArgument})
	endforeach()

	# 두 번 기동 쌍 — `sequence/<묶음>.1` · `.2`. 빈 폴더의 사용자 설정 파일로 .1 을 돌려 남기고 같은 파일로 .2 를 띄워 남았는지 본다
	# (편집기 상태 쌍과 같은 실행기 — 프로세스를 다시 띄우는 것 자체가 검사 대상이다). 설정 파일을 같이 쓰므로 기본 백엔드 하나다.
	file(GLOB listSequenceFirst RELATIVE "${swScenarioRoot}" CONFIGURE_DEPENDS "${automationDir}/sequence/*.1.scenario.xml")
	foreach(firstPath IN LISTS listSequenceFirst)
		string(REGEX REPLACE "\\.1\\.scenario\\.xml$" "" pairPath "${firstPath}")
		get_filename_component(pairName "${pairPath}" NAME)
		set(sequenceTestName Scenario.${pack}.sequence.${pairName})
		set(sequenceStateDir "${CMAKE_BINARY_DIR}/Testing/StateDir/${pack}.${pairName}")
		add_test(NAME ${sequenceTestName}
			COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> -DPAIR=${pairPath} -DSTATE_DIR=${sequenceStateDir} "-DSTATE_ARG=-gv_userSettingsFile=${sequenceStateDir}/usersettings.json"
				"-DGAME_ARG=${gameArgument}" -DLOG_NAME=${sequenceTestName} -DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs -DSKIP_CODE=${SW_EXIT_CODE_Skipped} -P ${CMAKE_CURRENT_SOURCE_DIR}/RunStateDirPair.cmake
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(${sequenceTestName} PROPERTIES LABELS hostgpu TIMEOUT 600 SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped})
	endforeach()

	# 게임의 편집기 시나리오 — 그 게임의 데이터를 편집기로 다루는 흐름이라 그 게임을 싣는 편집기 체인(`Scenario.<팩>.editor`)으로 돈다. 플래그는 엔진 편집기 체인(아래)과 같다.
	if(TARGET EditorModule)
		file(GLOB listGameEditorScenario RELATIVE "${swScenarioRoot}" CONFIGURE_DEPENDS "${automationDir}/editor/*.scenario.xml")
		sw_addScenarioChain(Scenario.${pack}.editor 120 SCENARIOS ${listGameEditorScenario} ARGS ${gameArgument} -EnableEditor -scenario-backend-split)
	endif()
endforeach()

# 엔진 편집기 시나리오 — `Content/engine/automation/editor/` 바로 아래 `*.scenario.xml` 을 편집기를 켠 체인 하나(`Scenario.editor`, 활성 게임)로 돌린다. 하위 폴더 `statedir/`
# (두 프로세스가 같은 상태 폴더를 이어 쓰는 쌍)는 아래에서 쌍마다 항목 하나로 돈다. 게임 체인처럼 백엔드마다 자식 App 하나가
# 동시에 돌고(`-scenario-backends=all`) 저마다 제 백엔드 골든(씬 · 게임 뷰)과 견준다 — 편집기 상태 폴더가 프로세스마다라 자식끼리 겹치지 않는다.
# 그린 화소를 판정하지 않는 시나리오(UI 로직 · 문턱 없는 캡처)는 백엔드를 돌아가며 하나에서만 돈다(`-scenario-backend-split`) — 전부 네 번 돌면 백엔드당 약 1000 초였다.
if(TARGET EditorModule)
	file(GLOB listEditorScenario RELATIVE "${swScenarioRoot}" CONFIGURE_DEPENDS "${swScenarioRoot}/engine/automation/editor/*.scenario.xml")
	sw_addScenarioChain(Scenario.editor 120 SCENARIOS ${listEditorScenario} ARGS -EnableEditor -scenario-backend-split)
	# 저장된 상태 쌍(`statedir/<묶음>.1` · `.2`) — 끄고 다시 켜면 상태가 돌아오는지가 검사 대상이라 프로세스를 다시 띄우는 것 자체가 필요하다.
	# 빈 상태 폴더로 .1 을, 같은 폴더로 .2 를 잇는다(언리얼이 설정 저장을 에디터 재시작으로 보는 것과 같다). 상태 폴더를 같이 쓰므로 기본 백엔드 하나다.
	file(GLOB listStateDirFirst RELATIVE "${swScenarioRoot}" CONFIGURE_DEPENDS "${swScenarioRoot}/engine/automation/editor/statedir/*.1.scenario.xml")
	foreach(firstPath IN LISTS listStateDirFirst)
		string(REGEX REPLACE "\\.1\\.scenario\\.xml$" "" pairPath "${firstPath}")
		get_filename_component(pairName "${pairPath}" NAME)
		add_test(NAME Scenario.editor.statedir.${pairName}
			COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> -DPAIR=${pairPath} -DSTATE_DIR=${CMAKE_BINARY_DIR}/Testing/StateDir/${pairName}
				"-DSTATE_ARG=-gv_editorStateDir=${CMAKE_BINARY_DIR}/Testing/StateDir/${pairName}" -DEDITOR=ON -DLOG_NAME=Scenario.editor.statedir.${pairName} -DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs -DSKIP_CODE=${SW_EXIT_CODE_Skipped} -P ${CMAKE_CURRENT_SOURCE_DIR}/RunStateDirPair.cmake
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(Scenario.editor.statedir.${pairName} PROPERTIES LABELS hostgpu TIMEOUT 600 SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped})
	endforeach()
endif()
