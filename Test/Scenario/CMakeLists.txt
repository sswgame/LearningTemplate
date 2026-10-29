# ==============================================================================
# @file Test/Scenario/CMakeLists.txt
# @brief 시나리오를 CTest 항목으로 — `Content/engine/automation/` 과 활성 게임의 `Content/game/<게임>/automation/` 바로 아래 `*.scenario.xml` 마다
#        `App -scenario=<리소스 경로> -scenario-backends=all -unattended` 하나(라벨 hostgpu — 창과 GPU 가 있는 PC 에서). 목록이 없다 — 폴더를 훑는다.
#        그 App 은 창 · GPU 없이 백엔드마다 자식 App 을 동시에 띄우고 결과를 모은다(`AutomationBackendMatrix` — 언리얼 Gauntlet 의 컨트롤러 자리).
#        골든이 백엔드별이라 자식마다 제 골든과 견준다. 이 PC 에서 못 도는 백엔드(77)는 그 백엔드만 건너뛴다.
# @details 종료 코드는 실행기의 것이다(0 통과 · 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀 · 15 처리하지 않은 로그 — 13 은 CTest 의 건너뜀). 행렬은 첫 실패 백엔드의 코드를 낸다.
#          하위 폴더(예: `rhiswap/` — 네 백엔드가 모두 있어야 한다)는 조건이 붙은 시나리오라 줍지 않는다 — 손으로 돌린다.
#          항목끼리도(`ctest -j`) 겹치는 쓰기가 없어 병렬로 돈다: 로그는 `<빌드>/Testing/Logs/<항목>.<백엔드>.log`(`-log-file`), 산출물 · 설정은 프로세스별 폴더 · 파일이고,
#          실행이 나누는 캐시(Saved/ShaderCache)는 원자적 바꿔치기로만 쓴다. 항목 하나가 GPU 자식을 백엔드 수만큼 띄우므로 그 수를 슬롯으로 선언한다(`PROCESSORS`) —
#          `ctest -j4` 면 항목 하나(자식 넷)만 돈다. 예전에는 "`-j` 는 코어 수의 1/4" 을 문서로만 적어 `-j4` 가 자식 열여섯을 겹쳐 띄웠다.
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
foreach(scenarioPath IN LISTS listScenario)
	string(REGEX REPLACE "\.scenario\.xml$" "" scenarioName "${scenarioPath}")
	string(REGEX REPLACE "/automation/" "/" scenarioName "${scenarioName}")
	string(REPLACE "/" "." scenarioName "${scenarioName}")
	# 기동 때 정해지는 값(첫 씬 · 렌더 파이프라인)은 시나리오가 `<Launch variable= value=>` 로 선언하고, 실행기는 띄운 명령줄이 같은지 본다 — 여기서 명령줄로 옮긴다.
	file(STRINGS "${swScenarioRoot}/${scenarioPath}" listLaunchLine REGEX "^[ 	]*<Launch ")
	set(listLaunchArgument "")
	foreach(launchLine IN LISTS listLaunchLine)
		if(NOT launchLine MATCHES "variable=\"([^\"]+)\"[ 	]+value=\"([^\"]*)\"")
			message(FATAL_ERROR "${scenarioPath}: <Launch> must be <Launch variable=\"…\" value=\"…\"/> on one line — ${launchLine}")
		endif()
		list(APPEND listLaunchArgument "-${CMAKE_MATCH_1}=${CMAKE_MATCH_2}")
	endforeach()
	add_test(NAME Scenario.${scenarioName}
		COMMAND $<TARGET_FILE:App> -scenario=${scenarioPath} -scenario-backends=all -unattended
			-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/Scenario.${scenarioName}.log ${listLaunchArgument}
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(Scenario.${scenarioName} PROPERTIES LABELS hostgpu TIMEOUT 600 SKIP_RETURN_CODE 13 PROCESSORS ${swScenarioBackendCount})
endforeach()
