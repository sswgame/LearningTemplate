# ==============================================================================
# @file Test/Scenario/CMakeLists.txt
# @brief 시나리오를 CTest 항목으로 — `Content/engine/automation/` 과 활성 게임의 `Content/game/<게임>/automation/` 바로 아래 `*.scenario.xml` 마다
#        `App -scenario=<리소스 경로> -unattended` 하나(라벨 hostgpu — 창과 GPU 가 있는 PC 에서). 목록이 없다 — 폴더를 훑는다.
# @details 종료 코드는 실행기의 것이다(0 통과 · 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀 — 13 은 CTest 의 건너뜀).
#          하위 폴더(예: `rhiswap/` — 네 백엔드가 모두 있어야 한다)는 조건이 붙은 시나리오라 줍지 않는다 — 손으로 돌린다.
#          다른 게임의 시나리오는 그 게임이 활성일 때(구성의 SW_ACTIVE_GAME) 올라온다 — App 은 활성 게임 하나만 싣는다.
# ==============================================================================

if(NOT TARGET App)
	return()   # 전용 서버 타깃은 App 을 짓지 않는다
endif()

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
	add_test(NAME Scenario.${scenarioName}
		COMMAND $<TARGET_FILE:App> -scenario=${scenarioPath} -unattended
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(Scenario.${scenarioName} PROPERTIES LABELS hostgpu TIMEOUT 600 SKIP_RETURN_CODE 13 RUN_SERIAL ON)
endforeach()
