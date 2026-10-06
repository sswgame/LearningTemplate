# ==============================================================================
# @file Source/GameFramework/Kits/Online/Server/Mailbox/CMakeLists.txt
# @brief 우편함 키트(서버) — 목록 · 읽음 · 수령(원장 이동) · 모두 받기 · 지우기 · 만료 쓸기 · 전체 우편 · 서비스
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_Mailbox)
if(NOT TARGET GF_Server_Mailbox)
	return()
endif()
sw_linkSharedKit(GF_Server_Mailbox GF_Mailbox)
