# ==============================================================================
# @file Tools/OnlineLoadBot/CMakeLists.txt
# @brief 온라인 부하 시험 봇 — 키트 클라이언트 API 로 가짜 접속 수천 · 시나리오(JSON) · 지연 백분위 · 오류 · 알림 지표 (개발 도구, 배포물에 없다)
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
# 2) 코어(정적 라이브러리 — 봇 · 시나리오 · 지표 · 실행기 · 서버 조립)와 실행 파일. EngineTest 가 코어를 링크해 끝단 시험을 돈다.
# ------------------------------------------------------------------------------
file(GLOB swLoadBotCoreSource CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/LoadBot*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/LoadBot*.h")
add_library(OnlineLoadBotCore STATIC ${swLoadBotCoreSource})
target_include_directories(OnlineLoadBotCore PUBLIC "${CMAKE_SOURCE_DIR}/Tools")
target_link_libraries(OnlineLoadBotCore
	PUBLIC
		Engine
		GameFramework
		${swLoadBotKit}
	PRIVATE
		sw_global_options
)
sw_configurePch(OnlineLoadBotCore "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(OnlineLoadBotCore PROPERTIES FOLDER "Tools")

add_executable(OnlineLoadBot Main.cpp)
target_link_libraries(OnlineLoadBot PRIVATE OnlineLoadBotCore sw_global_options)
target_compile_definitions(OnlineLoadBot PRIVATE "SW_LOG_TAG=\"OnlineLoadBot\"")
sw_configurePch(OnlineLoadBot "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(OnlineLoadBot PROPERTIES FOLDER "Tools")
