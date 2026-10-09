# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Social/Server/CMakeLists.txt
# @brief 친구 키트(서버) — 관계 규칙 · 관계 저장(양쪽 레코드 + 개수 한 트랜잭션) · 접속 상태(캐시 · 버스) · 길드 · 서비스 호스트 바인딩
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_Social)
if(NOT TARGET GF_Server_Social)
	return()
endif()
sw_linkSharedKit(GF_Server_Social GF_Social)
