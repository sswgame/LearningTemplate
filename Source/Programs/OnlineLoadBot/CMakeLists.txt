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
# 2) 코어(정적 라이브러리 — 봇 · 시나리오 · 지표 · 실행기 · 서버 조립)와 실행 파일. 코어를 따로 둔 것은 이후 테스트 틀이 링크해 끝단 테스트를 돌 수 있게 하기 위해서다.
# ------------------------------------------------------------------------------
file(GLOB swLoadBotCoreSource CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/LoadBot*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/LoadBot*.h")
add_library(OnlineLoadBotCore STATIC ${swLoadBotCoreSource})
target_link_libraries(OnlineLoadBotCore PUBLIC sw_public_source_includes) # "Programs/OnlineLoadBot/..." 는 Source/ 기준 전체 경로다
target_link_libraries(OnlineLoadBotCore
	PUBLIC
		Engine
		GameFramework
		${swLoadBotKit}
	PRIVATE
		sw_global_options
)
sw_configurePCH(OnlineLoadBotCore "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(OnlineLoadBotCore PROPERTIES FOLDER "Source/Programs")

add_executable(OnlineLoadBot Main.cpp)
# ModuleManager: 호스트 범위의 모듈 라이브러리 창구(`ModuleLoaderSubsystem`) — 전용 서버(`ServerApp`)와 같은 모양으로 엔진(DedicatedServer)을 세운다.
target_link_libraries(OnlineLoadBot PRIVATE OnlineLoadBotCore ModuleManager sw_global_options)
target_compile_definitions(OnlineLoadBot PRIVATE "SW_LOG_TAG=\"OnlineLoadBot\"")
sw_configurePCH(OnlineLoadBot "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(OnlineLoadBot PROPERTIES FOLDER "Source/Programs")

# Dev(Windows): 키트 DLL 은 `Bin/Modules` 에 있어 OS 로더가 시작 때 찾지 못한다 — `ToolBin` 에 내고 키트 DLL 을 옆에 복사한다.
# 작업 폴더는 `Bin` 이다(Engine · 서드파티 DLL 은 거기서 찾고, `FileUtil::getBinaryDirectory` 가 ToolBin → Bin 으로 본다). 서버 키트는 실행 파일이 심볼을 쓰지 않아
# 링커가 import 를 버린다 — `Main.cpp` 가 시작할 때 이름으로 올린다(정적 등록자가 서버 조립 레지스트리에 오른다).
if(WIN32 AND NOT SW_SHIPPING_BUILD)
	set(swToolBin "${CMAKE_BINARY_DIR}/ToolBin")
	set_target_properties(OnlineLoadBot PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${swToolBin}$<0:>" PDB_OUTPUT_DIRECTORY "${swToolBin}")
	set(swCopyCommands "")
	foreach(swKit IN LISTS swLoadBotKit)
		list(APPEND swCopyCommands COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${swKit}>" "${swToolBin}/")
	endforeach()
	add_custom_command(TARGET OnlineLoadBot POST_BUILD ${swCopyCommands} COMMENT "[OnlineLoadBot] Copying kit DLLs next to the tool executable" VERBATIM)
endif()
