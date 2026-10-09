# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Server/LiveOps/CMakeLists.txt
# @brief 라이브 운영 키트(서버) — 기간 이벤트(영속 · 감사 · 버스 다시 읽기) · 원격 설정 값 풀기 · 푸시 알림 발송기 · 제공자 계약
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_LiveOps)
if(NOT TARGET GF_Server_LiveOps)
	return()
endif()
sw_linkSharedKit(GF_Server_LiveOps GF_LiveOps)
