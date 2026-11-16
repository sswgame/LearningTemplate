# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Social/Client/CMakeLists.txt
# @brief 친구 목록 키트(클라이언트) — 친구 목록 화면. 전용 서버는 짓지 않는다(언리얼 Type = ClientOnly 모듈)
# ==============================================================================

sw_add_game_framework_kit(GF_Client_Social)
if(NOT TARGET GF_Client_Social)
	return()
endif()
sw_link_runtime_kit(GF_Client_Social GF_Social)
