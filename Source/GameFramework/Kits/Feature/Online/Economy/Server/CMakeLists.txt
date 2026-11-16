# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Economy/Server/CMakeLists.txt
# @brief 경제 키트(서버) — 구매 · 영수증 논리, 영수증 검증 제공자(제품 이름은 Receipt/Provider/<제품>/ 안에만) · 서비스
# ==============================================================================

sw_add_game_framework_kit(GF_Server_Economy)
if(NOT TARGET GF_Server_Economy)
	return()
endif()
sw_link_runtime_kit(GF_Server_Economy GF_Economy)
sw_link_runtime_kit(GF_Server_Economy GF_Server_OnlineCore)
