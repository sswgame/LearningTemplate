# ==============================================================================
# @file Source/GameFramework/Kits/Online/Server/Leaderboard/CMakeLists.txt
# @brief 순위표 키트(서버) — 점수 · 통계 영속 + 캐시 순위(잃으면 다시 채움) · 업적 보상 우편 · 시즌 정산 · 서비스 호스트 바인딩
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_Leaderboard)
if(NOT TARGET GF_Server_Leaderboard)
	return()
endif()
sw_linkSharedKit(GF_Server_Leaderboard GF_Leaderboard)
