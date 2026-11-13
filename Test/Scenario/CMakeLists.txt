# ==============================================================================
# @file Test/Scenario/CMakeLists.txt
# @brief 시나리오를 CTest 항목으로 — 이 구성이 짓는 게임마다(활성 게임 `SW_ACTIVE_GAME` + 함께 짓는 게임 `SW_GAMES`) 그 게임의 `Content/game/<팩>/automation/` 바로 아래
#        `*.scenario.xml` 모두를 체인 하나(`Scenario.<팩>` — `App -scenario=a,b,c -scenario-keep-going -scenario-backends=all -unattended`, 라벨 hostgpu — 창과 GPU 가 있는 PC 에서)로 돌린다.
#        엔진 시나리오(`Content/engine/automation/`)는 활성 게임의 체인에 든다. 활성 게임이 아닌 게임은 `-game=<게임>` 을 붙인다(App 이 그 게임 팩 `Bin/Games/<게임>` 을 싣는다).
#        목록이 없다 — 폴더를 훑는다. 팩 이름은 게임 프리셋(`Config/Game/<게임>.json` 의 `_packRoot`)이 정한다.
#        그 App 은 창 · GPU 없이 백엔드마다 자식 App 을 하나씩 동시에 띄우고 결과를 모은다(`AutomationPerBackend` — 언리얼 Gauntlet 의 컨트롤러 자리).
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

list(LENGTH SW_RHI_BACKEND_NAMES scenario_backend_count) # `-scenario-backends=all` 이 띄우는 자식 수

set(scenario_root "${CMAKE_SOURCE_DIR}/Content")
sw_add_source_tree_root("${scenario_root}/engine/automation") # 시나리오 폴더만 뿌리로 훑는다(`Content/` 전체는 에셋 — cmake/Engine/SourceTree.cmake)
# 시나리오의 `<Launch>` 를 명령줄 인자(`-이름=값` 목록)로 읽는다.
function(sw_read_scenario_launch scenario_path out)
	file(STRINGS "${scenario_root}/${scenario_path}" list_launch_line REGEX "^[ 	]*<Launch ")
	set(list_argument "")
	foreach(launch_line IN LISTS list_launch_line)
		if(NOT launch_line MATCHES "variable=\"([^\"]+)\"[ 	]+value=\"([^\"]*)\"")
			message(FATAL_ERROR "${scenario_path}: <Launch> must be <Launch variable=\"…\" value=\"…\"/> on one line — ${launch_line}")
		endif()
		list(APPEND list_argument "-${CMAKE_MATCH_1}=${CMAKE_MATCH_2}")
	endforeach()
	set(${out} "${list_argument}" PARENT_SCOPE)
endfunction()

# 체인 항목 하나 — 시나리오 모두를 백엔드마다 한 프로세스로. 모듈 리로드(이름 `*modulereload`)는 코드 이미지를 갈아 끼우므로 맨 뒤에 둔다 — 그 단계가 무엇을 남겨도 다른 단계에 닿지 않는다.
# 첫 단계의 `<Launch>` 는 명령줄로 준다(게임이 기동하며 그 값을 읽는다). 다음 단계의 값은 세션이 단계를 시작할 때 건다. ARGS 는 App 에 더 줄 인자(`-game=` · `-EnableEditor` …)다.
function(sw_add_scenario_chain test_name stage_seconds)
	cmake_parse_arguments(PARSE_ARGV 2 ARG "" "SLOTS;SPLIT" "SCENARIOS;ARGS")
	set(list_scenario ${ARG_SCENARIOS})
	if(NOT list_scenario)
		return()
	endif()
	list(SORT list_scenario)
	set(list_reload_scenario ${list_scenario})
	list(FILTER list_reload_scenario INCLUDE REGEX "modulereload\\.scenario\\.xml$")
	list(FILTER list_scenario EXCLUDE REGEX "modulereload\\.scenario\\.xml$")
	list(APPEND list_scenario ${list_reload_scenario})
	list(LENGTH list_scenario stage_count)
	list(GET list_scenario 0 first_path)
	sw_read_scenario_launch("${first_path}" list_launch_argument)
	list(JOIN list_scenario "," scenario_list)
	# SLOTS: 동시에 도는 백엔드 수(`-scenario-backend-slots`). 비우면 모두 함께. 슬롯이 적으면 자식이 차례로 돌아 시한 · 쓰는 코어도 그만큼이다.
	set(slot_count ${scenario_backend_count})
	set(list_slot_argument "")
	if(ARG_SLOTS)
		set(slot_count ${ARG_SLOTS})
		set(list_slot_argument -scenario-backend-slots=${ARG_SLOTS})
	endif()
	# SPLIT: 시나리오를 백엔드에 나눠 주는 방식(`-scenario-backend-split`). 그림 · 골든 · 기록 대조의 정본은 Release(모든 게임 · 네 백엔드)다 — Debug 는 Release 가 못 잡는
	# 단언(`SW_ASSERT` 는 Debug 에만 있다) · 검사를 시나리오마다 한 번 지나면 되므로 모든 시나리오를 백엔드에 하나씩 나눈다(`all`, 날마다 배정이 돌아 나흘이면 모든 조합).
	# 같은 체인을 두 구성에서 네 백엔드로 다 돌던 것이 묶음 검증 Debug ctest 29 분의 대부분이었다(2026-11-13).
	set(list_split_argument "")
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		set(list_split_argument -scenario-backend-split=all)
	elseif(ARG_SPLIT)
		set(list_split_argument -scenario-backend-split=${ARG_SPLIT})
	endif()
	math(EXPR wave_count "(${scenario_backend_count} + ${slot_count} - 1) / ${slot_count}")
	math(EXPR chain_timeout "${stage_seconds} * ${stage_count} * ${wave_count}")
	add_test(NAME ${test_name}
		COMMAND $<TARGET_FILE:App> ${ARG_ARGS} -scenario=${scenario_list} -scenario-keep-going -scenario-backends=all ${list_split_argument} ${list_slot_argument} -unattended
			-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/${test_name}.log ${list_launch_argument}
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(${test_name} PROPERTIES LABELS hostgpu TIMEOUT ${chain_timeout} SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped} PROCESSORS ${slot_count})
	message(STATUS "[Scenario] ${test_name}: ${stage_count} scenario(s)")
endfunction()

# 부하 시나리오 하나 — 측정 창(`<Load durationSeconds="N"/>`)이 끝내는 성능 측정이라 체인에 넣지 않고 기본 백엔드 한 프로세스로 혼자 돈다
# (RUN_SERIAL — 다른 항목이 CPU · GPU 를 나눠 쓰면 잰 값이 흔들린다). 라벨은 `load` 하나라 기본 실행(`-L hostgpu` · `-L nogpu`)에 들지 않고
# `ctest -L load` 로 돈다(언리얼 Gauntlet 의 성능 테스트가 따로 도는 것과 같다). 시한은 측정 창 + 기동 여유다. 이름은 Scenario.<팩>.load.<이름> · Scenario.engine.load.<이름>.
function(sw_add_load_scenario load_path)
	file(STRINGS "${scenario_root}/${load_path}" list_load_line REGEX "<Load ")
	if(NOT list_load_line MATCHES "durationSeconds=\"([0-9]+)\"")
		message(FATAL_ERROR "${load_path}: a load scenario needs <Load durationSeconds=\"N\"/> (whole seconds) on one line")
	endif()
	math(EXPR load_timeout "${CMAKE_MATCH_1} + 300")
	string(REGEX REPLACE "^(engine|game/[^/]+)/automation/load/(.+)\\.scenario\\.xml$" "\\1.load.\\2" load_name "${load_path}")
	string(REPLACE "game/" "" load_name "${load_name}")
	sw_read_scenario_launch("${load_path}" list_load_launch)
	set(load_test_name Scenario.${load_name})
	add_test(NAME ${load_test_name}
		COMMAND $<TARGET_FILE:App> ${ARGN} -scenario=${load_path} -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/${load_test_name}.log ${list_load_launch}
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(${load_test_name} PROPERTIES LABELS load TIMEOUT ${load_timeout} RUN_SERIAL TRUE SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped})
endfunction()

sw_glob_source_tree(list_engine_load_scenario RELATIVE "${scenario_root}" "${scenario_root}/engine/automation/load/*.scenario.xml")
foreach(load_path IN LISTS list_engine_load_scenario)
	sw_add_load_scenario("${load_path}")
endforeach()

# 이 구성이 짓는 게임 — 활성 게임은 그 게임 모듈 타깃(`SW_ACTIVE_GAME_TARGET`)이 있을 때, 함께 짓는 게임은 `SW_GAME_PACKS`(`sw_select_game_packs`)다.
get_property(active_game_target GLOBAL PROPERTY SW_ACTIVE_GAME_TARGET)
set(list_built_game "")
if(active_game_target)
	get_property(list_built_game GLOBAL PROPERTY SW_GAME_PACKS)
	list(PREPEND list_built_game ${SW_ACTIVE_GAME})
endif()

set(sequential_scenario_packs shooter3d) # 백엔드를 차례로 돌릴 게임 팩 체인(아래 `Scenario.<팩>`)
foreach(game IN LISTS list_built_game)
	# Debug 가 함께 짓는 게임은 키트가 컴파일되는지와 단위 테스트(`UnitTests.<게임>`)를 보려는 것이다 - 시나리오 체인은 Release 가 돈다(텍스처 임포트가 Debug 보다 훨씬 빠르다).
	if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT game STREQUAL SW_ACTIVE_GAME)
		continue()
	endif()
	sw_require_json_value(game_pack_root "${CMAKE_SOURCE_DIR}/${SW_DIR_RUNTIME_GAME_PRESET}/${game}.json" _packRoot) # `game/<팩>`
	get_filename_component(pack "${game_pack_root}" NAME)
	set(game_argument "")
	if(NOT game STREQUAL SW_ACTIVE_GAME)
		set(game_argument "-game=${game}")
	endif()
	set(automation_dir "${scenario_root}/${game_pack_root}/automation")
	sw_add_source_tree_root("${automation_dir}")

	# 게임 체인 — 활성 게임의 체인에는 엔진 시나리오도 든다(엔진 시나리오는 게임 하나로 한 번 돈다).
	set(list_chain_glob "${automation_dir}/*.scenario.xml")
	if(game STREQUAL SW_ACTIVE_GAME)
		list(APPEND list_chain_glob "${scenario_root}/engine/automation/*.scenario.xml")
	endif()
	sw_glob_source_tree(list_scenario RELATIVE "${scenario_root}" ${list_chain_glob})
	# GPU 가 무거운 체인은 백엔드를 차례로 돌린다 — 넷을 함께 돌려도 빨라지지 않고(Shooter3D 225 초 대 차례로 227 초) 프레임만 7 → 100 ms 로 느려져
	# 시한(crowd 150 초)에 닿고 골든이 흔들렸다(2026-11-02 측정, NEXT 6.2). 다른 체인은 함께 도는 편이 기동을 겹쳐 빠르다.
	set(chain_slot "")
	if(pack IN_LIST sequential_scenario_packs)
		set(chain_slot SLOTS 1)
	endif()
	sw_add_scenario_chain(Scenario.${pack} 300 ${chain_slot} SCENARIOS ${list_scenario} ARGS ${game_argument})

	# 쇼케이스 — 쇼케이스 씬 하나를 정해진 시각에 찍는 시나리오. 게임 체인과 같은 길(백엔드 넷)이되 제 씬 · 제 골든(`showcase/golden/`)을 두는 묶음이라 따로 항목이 된다.
	sw_glob_source_tree(list_showcase_scenario RELATIVE "${scenario_root}" "${automation_dir}/showcase/*.scenario.xml")
	sw_add_scenario_chain(Scenario.${pack}.showcase 300 SCENARIOS ${list_showcase_scenario} ARGS ${game_argument})

	# 데스크톱 시나리오 — OS 전역 상태(커서 클립 · 전경)를 쓰거나 프로세스를 끝내므로 체인에 넣지 못하고(백엔드 넷이 동시에 돈다)
	# 하나씩 기본 백엔드 한 프로세스로 혼자 돈다(RUN_SERIAL — 다른 항목의 창이 전경을 빼앗지 않게). 언리얼 Gauntlet 이 데스크톱 테스트를 따로 도는 것과 같다.
	sw_glob_source_tree(list_desktop_scenario RELATIVE "${scenario_root}" "${automation_dir}/desktop/*.scenario.xml")
	foreach(desktop_path IN LISTS list_desktop_scenario)
		get_filename_component(desktop_name "${desktop_path}" NAME)
		string(REGEX REPLACE "\\.scenario\\.xml$" "" desktop_name "${desktop_name}")
		sw_read_scenario_launch("${desktop_path}" list_desktop_launch)
		set(desktop_test_name Scenario.${pack}.desktop.${desktop_name})
		add_test(NAME ${desktop_test_name}
			COMMAND $<TARGET_FILE:App> ${game_argument} -scenario=${desktop_path} -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/${desktop_test_name}.log ${list_desktop_launch}
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(${desktop_test_name} PROPERTIES LABELS hostgpu TIMEOUT 300 RUN_SERIAL TRUE SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped})
	endforeach()

	sw_glob_source_tree(list_game_load_scenario RELATIVE "${scenario_root}" "${automation_dir}/load/*.scenario.xml")
	foreach(load_path IN LISTS list_game_load_scenario)
		sw_add_load_scenario("${load_path}" ${game_argument})
	endforeach()

	# 두 번 기동 쌍 — `sequence/<묶음>.1` · `.2`. 빈 폴더의 사용자 설정 파일로 .1 을 돌려 남기고 같은 파일로 .2 를 띄워 남았는지 본다
	# (편집기 상태 쌍과 같은 실행기 — 프로세스를 다시 띄우는 것 자체가 검사 대상이다). 설정 파일을 같이 쓰므로 기본 백엔드 하나다.
	sw_glob_source_tree(list_sequence_first RELATIVE "${scenario_root}" "${automation_dir}/sequence/*.1.scenario.xml")
	foreach(first_path IN LISTS list_sequence_first)
		string(REGEX REPLACE "\\.1\\.scenario\\.xml$" "" pair_path "${first_path}")
		get_filename_component(pair_name "${pair_path}" NAME)
		set(sequence_test_name Scenario.${pack}.sequence.${pair_name})
		set(sequence_state_dir "${CMAKE_BINARY_DIR}/Testing/StateDir/${pack}.${pair_name}")
		add_test(NAME ${sequence_test_name}
			COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> -DPAIR=${pair_path} -DSTATE_DIR=${sequence_state_dir} "-DSTATE_ARG=-gv_userSettingsFile=${sequence_state_dir}/usersettings.json"
				-DUSER_DATA_DIR=$<TARGET_FILE_DIR:App>/Saved/Automation/UserData/sequence.${pack}.${pair_name}
				"-DGAME_ARG=${game_argument}" -DLOG_NAME=${sequence_test_name} -DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs -DSKIP_CODE=${SW_EXIT_CODE_Skipped} -P ${CMAKE_CURRENT_SOURCE_DIR}/RunStateDirPair.cmake
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(${sequence_test_name} PROPERTIES LABELS hostgpu TIMEOUT 600 SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped})
	endforeach()

	# 게임의 편집기 시나리오 — 그 게임의 데이터를 편집기로 다루는 흐름이라 그 게임을 싣는 편집기 체인(`Scenario.<팩>.editor`)으로 돈다. 플래그는 엔진 편집기 체인(아래)과 같다.
	if(TARGET EditorModule)
		sw_glob_source_tree(list_game_editor_scenario RELATIVE "${scenario_root}" "${automation_dir}/editor/*.scenario.xml")
		sw_add_scenario_chain(Scenario.${pack}.editor 120 SPLIT independent SCENARIOS ${list_game_editor_scenario} ARGS ${game_argument} -EnableEditor)
	endif()
endforeach()

# 엔진 편집기 시나리오 — `Content/engine/automation/editor/` 바로 아래 `*.scenario.xml` 을 편집기를 켠 체인 하나(`Scenario.editor`, 활성 게임)로 돌린다. 하위 폴더 `statedir/`
# (두 프로세스가 같은 상태 폴더를 이어 쓰는 쌍)는 아래에서 쌍마다 항목 하나로 돈다. 게임 체인처럼 백엔드마다 자식 App 하나가
# 동시에 돌고(`-scenario-backends=all`) 저마다 제 백엔드 골든(씬 · 게임 뷰)과 견준다 — 편집기 상태 폴더가 프로세스마다라 자식끼리 겹치지 않는다.
# 그린 화소를 판정하지 않는 시나리오(UI 로직 · 문턱 없는 캡처)는 백엔드를 돌아가며 하나에서만 돈다(`-scenario-backend-split=independent`, Debug 는 `all`) — 전부 네 번 돌면 백엔드당 약 1000 초였다.
if(TARGET EditorModule)
	sw_glob_source_tree(list_editor_scenario RELATIVE "${scenario_root}" "${scenario_root}/engine/automation/editor/*.scenario.xml")
	sw_add_scenario_chain(Scenario.editor 120 SPLIT independent SCENARIOS ${list_editor_scenario} ARGS -EnableEditor)
	# 저장된 상태 쌍(`statedir/<묶음>.1` · `.2`) — 끄고 다시 켜면 상태가 돌아오는지가 검사 대상이라 프로세스를 다시 띄우는 것 자체가 필요하다.
	# 빈 상태 폴더로 .1 을, 같은 폴더로 .2 를 잇는다(언리얼이 설정 저장을 에디터 재시작으로 보는 것과 같다). 상태 폴더를 같이 쓰므로 기본 백엔드 하나다.
	sw_glob_source_tree(list_state_dir_first RELATIVE "${scenario_root}" "${scenario_root}/engine/automation/editor/statedir/*.1.scenario.xml")
	foreach(first_path IN LISTS list_state_dir_first)
		string(REGEX REPLACE "\\.1\\.scenario\\.xml$" "" pair_path "${first_path}")
		get_filename_component(pair_name "${pair_path}" NAME)
		add_test(NAME Scenario.editor.statedir.${pair_name}
			COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> -DPAIR=${pair_path} -DSTATE_DIR=${CMAKE_BINARY_DIR}/Testing/StateDir/${pair_name}
				"-DSTATE_ARG=-gv_editorStateDir=${CMAKE_BINARY_DIR}/Testing/StateDir/${pair_name}" -DUSER_DATA_DIR=$<TARGET_FILE_DIR:App>/Saved/Automation/UserData/statedir.${pair_name} -DEDITOR=ON -DLOG_NAME=Scenario.editor.statedir.${pair_name} -DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs -DSKIP_CODE=${SW_EXIT_CODE_Skipped} -P ${CMAKE_CURRENT_SOURCE_DIR}/RunStateDirPair.cmake
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(Scenario.editor.statedir.${pair_name} PROPERTIES LABELS hostgpu TIMEOUT 600 SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped})
	endforeach()
endif()

# 모든 시나리오가 임포트한 산출물을 요구한다(Test/CMakeLists.txt 의 `Import.*`).
get_property(all_scenario_test DIRECTORY PROPERTY TESTS)
set_tests_properties(${all_scenario_test} PROPERTIES FIXTURES_REQUIRED ImportedAssets)
if(SW_CPU_HEAVY_TEST)
	set_tests_properties(${all_scenario_test} PROPERTIES DEPENDS ${SW_CPU_HEAVY_TEST}) # 시간 판정이 있는 시나리오는 CPU 를 다 쓰는 항목 뒤에 돈다(Test/CMakeLists.txt 의 SW_CPU_HEAVY_TEST)
endif()
