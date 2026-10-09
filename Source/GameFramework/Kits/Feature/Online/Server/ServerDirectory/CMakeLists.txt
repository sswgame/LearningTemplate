# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Server/ServerDirectory/CMakeLists.txt
# @brief 서버 디렉터리 키트(서버) — 점검 · 공지 영속 + 감사 · 배정 서비스 · 서비스 호스트 등록
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_ServerDirectory)
if(NOT TARGET GF_Server_ServerDirectory)
	return()
endif()
sw_linkSharedKit(GF_Server_ServerDirectory GF_ServerDirectory)
