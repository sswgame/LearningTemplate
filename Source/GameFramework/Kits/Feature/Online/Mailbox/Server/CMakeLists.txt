# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Mailbox/Server/CMakeLists.txt
# @brief 우편함 키트(서버) — 목록 · 읽음 · 수령(원장 이동) · 모두 받기 · 지우기 · 만료 쓸기 · 전체 우편 · 서비스
# ==============================================================================

sw_add_game_framework_kit(GF_Server_Mailbox)
if(NOT TARGET GF_Server_Mailbox)
	return()
endif()
sw_link_runtime_kit(GF_Server_Mailbox GF_Mailbox)
sw_link_runtime_kit(GF_Server_Mailbox GF_Server_OnlineCore)
