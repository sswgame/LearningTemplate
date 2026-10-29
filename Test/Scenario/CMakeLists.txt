# ==============================================================================
# @file Test/Scenario/CMakeLists.txt
# @brief 시나리오를 CTest 항목 하나로 — `Content/engine/automation/` 과 활성 게임의 `Content/game/<게임>/automation/` 바로 아래 `*.scenario.xml` 모두를
#        체인 하나(`App -scenario=a,b,c -scenario-keep-going -scenario-backends=all -unattended`, 라벨 hostgpu — 창과 GPU 가 있는 PC 에서)로 돌린다. 목록이 없다 — 폴더를 훑는다.
#        게임의 기동 흐름이 씬을 정하는 시나리오(첫 씬 없이 `<Launch>` 만 있다)만 자기 프로세스다 — 아래 규칙.
#        그 App 은 창 · GPU 없이 백엔드마다 자식 App 을 하나씩 동시에 띄우고 결과를 모은다(`AutomationBackendMatrix` — 언리얼 Gauntlet 의 컨트롤러 자리).
#        자식 하나가 시나리오를 차례로 돌고, 단계마다 전역 변수를 되돌리고 `<Launch>` 값을 건 뒤 첫 씬을 새로 연다(`AutomationSession::startStage` — 언리얼 Automation 이
#        테스트마다 맵을 열고 CVar 를 되돌리는 것과 같다). 프로세스는 백엔드 수 + 부모 하나다. 시나리오 하나만 돌리려면 `App -scenario=<경로>` 를 직접 띄운다.
#        골든이 백엔드별이라 자식마다 제 골든과 견준다. 이 PC 에서 못 도는 백엔드는 그 백엔드만 건너뛴다.
# @details 종료 코드는 표 하나(`Config/Engine/ProcessExitCodes.json` → `SW_EXIT_CODE_<이름>`)다 — 건너뜀(`SW_EXIT_CODE_Skipped`)은 CTest 의 건너뜀이다. 행렬은 첫 실패 백엔드의 코드를 낸다.
#          단계별 결과는 `<빌드>/Testing/Logs/Scenario.<게임>.<백엔드>.log` 의 `chain stage i/n` 줄에 있다.
#          하위 폴더(예: `rhiswap/` — 네 백엔드가 모두 있어야 한다)는 조건이 붙은 시나리오라 줍지 않는다 — 손으로 돌린다.
#          항목이 GPU 자식을 백엔드 수만큼 띄우므로 그 수를 슬롯으로 선언한다(`PROCESSORS`) — `ctest -j` 의 다른 항목(단위 테스트)과 겹쳐도 GPU 자식이 늘지 않는다.
#          다른 게임의 시나리오는 그 게임이 활성일 때(구성의 SW_ACTIVE_GAME) 올라온다 — App 은 활성 게임 하나만 싣는다.
# ==============================================================================

if(NOT TARGET App)
	return()   # 전용 서버 타깃은 App 을 짓지 않는다
endif()

list(LENGTH SW_RHI_BACKEND_NAMES swScenarioBackendCount) # `-scenario-backends=all` 이 띄우는 자식 수

set(swScenarioRoot "${CMAKE_SOURCE_DIR}/Content")
string(TOLOWER "${SW_ACTIVE_GAME}" swActiveGamePack)
file(GLOB listScenario RELATIVE "${swScenarioRoot}" CONFIGURE_DEPENDS
	"${swScenarioRoot}/engine/automation/*.scenario.xml"
	"${swScenarioRoot}/game/${swActiveGamePack}/automation/*.scenario.xml"
)
list(SORT listScenario)
# 모듈 리로드는 코드 이미지를 갈아 끼우므로 맨 뒤에 둔다 — 그 단계가 무엇을 남겨도 다른 시나리오의 결과에 닿지 않는다.
set(swLastScenario "engine/automation/modulereload.scenario.xml")
if(swLastScenario IN_LIST listScenario)
	list(REMOVE_ITEM listScenario "${swLastScenario}")
	list(APPEND listScenario "${swLastScenario}")
endif()
# 시나리오의 `<Launch>` 를 명령줄 인자로 읽는다(`out` 에 `-이름=값` 목록, `outFirstScene` 에 첫 씬).
function(sw_readScenarioLaunch scenarioPath out outFirstScene)
	file(STRINGS "${swScenarioRoot}/${scenarioPath}" listLaunchLine REGEX "^[ 	]*<Launch ")
	set(listArgument "")
	set(firstScene "")
	foreach(launchLine IN LISTS listLaunchLine)
		if(NOT launchLine MATCHES "variable=\"([^\"]+)\"[ 	]+value=\"([^\"]*)\"")
			message(FATAL_ERROR "${scenarioPath}: <Launch> must be <Launch variable=\"…\" value=\"…\"/> on one line — ${launchLine}")
		endif()
		list(APPEND listArgument "-${CMAKE_MATCH_1}=${CMAKE_MATCH_2}")
		if(CMAKE_MATCH_1 STREQUAL "gv_firstScene")
			set(firstScene "${CMAKE_MATCH_2}")
		endif()
	endforeach()
	set(${out} "${listArgument}" PARENT_SCOPE)
	set(${outFirstScene} "${firstScene}" PARENT_SCOPE)
endfunction()

# 시나리오 하나 · 여럿을 백엔드마다 한 프로세스로 — 첫 단계의 `<Launch>` 는 명령줄로 준다(첫 단계가 기동한 씬을 다시 열지 않게). 다음 단계의 값은 세션이 단계를 시작할 때 건다.
function(sw_addScenarioTest testName)
	set(listPath ${ARGN})
	list(LENGTH listPath stageCount)
	list(GET listPath 0 firstPath)
	sw_readScenarioLaunch("${firstPath}" listLaunchArgument unusedFirstScene)
	list(JOIN listPath "," scenarioList)
	math(EXPR chainTimeout "300 * ${stageCount}")
	add_test(NAME ${testName}
		COMMAND $<TARGET_FILE:App> -scenario=${scenarioList} -scenario-keep-going -scenario-backends=all -unattended
			-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/${testName}.log ${listLaunchArgument}
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(${testName} PROPERTIES LABELS hostgpu TIMEOUT ${chainTimeout} SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped} PROCESSORS ${swScenarioBackendCount})
	message(STATUS "[Scenario] ${testName}: ${stageCount} scenario(s)")
endfunction()

# `<Launch>` 가 있는데 첫 씬을 선언하지 않은 시나리오는 게임의 기동 흐름이 씬을 정한다(Empty 의 벤치는 기동 때 `gv_stressTest*` 를 읽어 자기 씬을 만든다) —
# 세션이 다시 열 씬이 없으므로 자기 프로세스로 돈다. 나머지는 체인 하나다.
set(listChainScenario "")
foreach(scenarioPath IN LISTS listScenario)
	sw_readScenarioLaunch("${scenarioPath}" listLaunchArgument firstScene)
	if(listLaunchArgument AND firstScene STREQUAL "")
		string(REGEX REPLACE "\.scenario\.xml$" "" scenarioName "${scenarioPath}")
		string(REPLACE "/automation/" "." scenarioName "${scenarioName}")
		string(REPLACE "/" "." scenarioName "${scenarioName}")
		sw_addScenarioTest(Scenario.${scenarioName} "${scenarioPath}")
	else()
		list(APPEND listChainScenario "${scenarioPath}")
	endif()
endforeach()
if(listChainScenario)
	sw_addScenarioTest(Scenario.${swActiveGamePack} ${listChainScenario})
endif()
