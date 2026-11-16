# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Chat/Server/CMakeLists.txt
# @brief 채팅 키트(서버) — 금칙어 거르개 · 도배 막이 · 채널 · 귓속말 · 기록 · 서버 간 전달 · 호스트 등록
# ==============================================================================

sw_add_game_framework_kit(GF_Server_Chat)
if(NOT TARGET GF_Server_Chat)
	return()
endif()
sw_link_runtime_kit(GF_Server_Chat GF_Chat)
sw_link_runtime_kit(GF_Server_Chat GF_Server_OnlineCore)
