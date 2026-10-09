# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Server/Admin/CMakeLists.txt
# @brief GM 도구 키트(서버) — 권한 등급 문지기 · 조회 · 지급/회수 · 제재 · 우편 · 일괄 지급 · 전체 우편 · 감사 열람 · 권한, 모두 감사 + 멱등
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_Admin)
if(NOT TARGET GF_Server_Admin)
	return()
endif()
sw_linkSharedKit(GF_Server_Admin GF_Admin)
