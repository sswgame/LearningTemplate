# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Trade/Server/CMakeLists.txt
# @brief 거래 키트(서버) — 상태 기계 · 거래 레코드 · 원장 정산(맡김 없이 분개 하나) · TradeService · TradeServer
# ==============================================================================

sw_add_game_framework_kit(GF_Server_Trade)
if(NOT TARGET GF_Server_Trade)
	return()
endif()
sw_link_runtime_kit(GF_Server_Trade GF_Trade)
sw_link_runtime_kit(GF_Server_Trade GF_Server_OnlineCore)
