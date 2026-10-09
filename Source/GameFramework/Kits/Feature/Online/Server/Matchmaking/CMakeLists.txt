# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Server/Matchmaking/CMakeLists.txt
# @brief 매칭 키트(서버) — 매처(실력 창 넓히기 · 지역 풀기 · 파티는 한 팀 · 팀 균형) · 대기열 권한 · 전용 서버 배정
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_Matchmaking)
if(NOT TARGET GF_Server_Matchmaking)
	return()
endif()
sw_linkSharedKit(GF_Server_Matchmaking GF_Matchmaking)
