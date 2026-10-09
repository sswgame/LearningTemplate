# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Account/Server/CMakeLists.txt
# @brief 로그인 서비스 키트(서버) — 계정 인증(소금 + 느린 해시) · 세션 토큰 발급 · 검증 · 만료 · 재접속 · 중복 로그인 · 게임(UDP) 접속 표
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_Account)
if(NOT TARGET GF_Server_Account)
	return()
endif()
sw_linkSharedKit(GF_Server_Account GF_Account)
