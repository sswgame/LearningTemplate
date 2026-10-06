# ==============================================================================
# @file Source/GameFramework/Kits/Online/Server/Economy/CMakeLists.txt
# @brief 경제 키트(서버) — 구매 · 영수증 논리, 영수증 검증 제공자(제품 이름은 Receipt/Provider/<제품>/ 안에만) · 서비스
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_Economy)
if(NOT TARGET GF_Server_Economy)
	return()
endif()
sw_linkSharedKit(GF_Server_Economy GF_Economy)
