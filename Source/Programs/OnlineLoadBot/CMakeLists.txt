# ==============================================================================
# @file Source/Programs/OnlineLoadBot/CMakeLists.txt
# @brief 온라인 부하 테스트 봇 — 키트 클라이언트 API 로 가짜 접속 수천 · 시나리오(JSON) · 지연 백분위 · 오류 · 알림 지표 (개발 도구, 배포물에 없다)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 기대는 키트 — 클라이언트 키트 일곱과 같은 기능의 서버 키트(한 프로세스 서버 조립 `LoadBotLocalServer`). 하나라도 꺼져 있으면 짓지 않는다.
# ------------------------------------------------------------------------------
set(swLoadBotKit
	GF_Account GF_Server_Account
	GF_Chat GF_Server_Chat
	GF_Leaderboard GF_Server_Leaderboard
	GF_LiveOps GF_Server_LiveOps
	GF_Matchmaking GF_Server_Matchmaking
	GF_ServerDirectory GF_Server_ServerDirectory
	GF_Social GF_Server_Social
)
foreach(swKit IN LISTS swLoadBotKit)
	if(NOT TARGET ${swKit})
		message(STATUS "[OnlineLoadBot] ${swKit} is not built in this configuration - skipping the load bot.")
		return()
	endif()
endforeach()

# ------------------------------------------------------------------------------
# 2) 봇 모듈(`OnlineLoadBotCore` — 봇 · 시나리오 · 지표 · 실행기 · 서버 조립 · 진입점 `LoadBotMain.cpp`)과 그것을 올리는 실행 파일.
#    실행 파일은 Engine · ModuleManager 만 링크한다. GameFramework · 키트 DLL 은 정적 초기화에서 이름 풀(`hashed_string`)을 써서 엔진 기동 뒤에만 올라올 수 있다 —
#    실행 파일이 직접 링크하면 main 전에 DLL 초기화가 실패한다(0xC0000142, 로그 없음, Windows 오류 대화상자로 CTest 가 시간 초과). App · Server 가 모듈을
#    기동 뒤에 올리는 것과 같은 모양으로, 실행 파일이 엔진의 `ModuleTypes` 단계에서 `SW_LOADBOT_MODULES` 를 순서대로 올리고 진입점(`LoadBotModuleEntry.h`)을 부른다.
#    모듈은 App 이 올리는 동적 모듈 레지스트리(`Bin/Modules`)에 넣지 않고 `Bin` 에 둔다(`findModuleLibraryPath` 가 Modules → Bin 으로 찾는다).
# ------------------------------------------------------------------------------
file(GLOB swLoadBotCoreSource CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/LoadBot*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/LoadBot*.h")
# SHARED(진입점 하나만 내보냄): MODULE 종류는 App 이 올리는 동적 모듈 레지스트리에 들어야 한다(`sw_verifyDynamicModuleRegistry`) — 이 모듈은 봇 실행 파일만 올린다.
add_library(OnlineLoadBotCore SHARED ${swLoadBotCoreSource})
target_link_libraries(OnlineLoadBotCore
	PRIVATE
		sw_public_source_includes # "Programs/OnlineLoadBot/..." 는 Source/ 기준 전체 경로다
		Engine
		GameFramework
		${swLoadBotKit}
		sw_global_options
)
target_compile_definitions(OnlineLoadBotCore PRIVATE "SW_LOG_TAG=\"OnlineLoadBot\"")
sw_configureDLLExports(OnlineLoadBotCore SHARED MODULE_SHARED)
sw_configurePCH(OnlineLoadBotCore "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(OnlineLoadBotCore PROPERTIES
	FOLDER "Source/Programs"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin$<0:>"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin$<0:>"
)

# 올리는 순서 = 의존 순서: GameFramework → 키트(런타임 키트 다음에 같은 기능의 서버 키트 — 위 목록 순서) → 봇 모듈.
set(swLoadBotModuleList GameFramework ${swLoadBotKit} OnlineLoadBotCore)
list(JOIN swLoadBotModuleList "\",\"" swLoadBotModuleText)

sw_addExecutable(OnlineLoadBot Main.cpp)
# ModuleManager: 호스트 범위의 모듈 라이브러리 창구(`ModuleLoaderSubsystem`) — 전용 서버(`ServerApp`)와 같은 모양으로 엔진(DedicatedServer)을 세운다.
target_link_libraries(OnlineLoadBot PRIVATE ModuleManager Engine sw_public_source_includes sw_global_options)
target_compile_definitions(OnlineLoadBot PRIVATE "SW_LOG_TAG=\"OnlineLoadBot\"" "SW_LOADBOT_MODULES=\"${swLoadBotModuleText}\"")
add_dependencies(OnlineLoadBot OnlineLoadBotCore) # 실행 파일을 지으면 올릴 모듈(과 그것이 링크하는 키트)도 선다
sw_configurePCH(OnlineLoadBot "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(OnlineLoadBot PROPERTIES FOLDER "Source/Programs")
