# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/ServerDirectory/Server/CMakeLists.txt
# @brief 서버 디렉터리 키트(서버) — 점검 · 공지 영속 + 감사 · 배정 서비스 · 서비스 호스트 등록
# ==============================================================================

sw_add_game_framework_kit(GF_Server_ServerDirectory)
if(NOT TARGET GF_Server_ServerDirectory)
	return()
endif()
sw_link_runtime_kit(GF_Server_ServerDirectory GF_ServerDirectory)
sw_link_runtime_kit(GF_Server_ServerDirectory GF_Server_OnlineCore)
