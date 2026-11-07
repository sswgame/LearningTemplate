# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/ServerDirectory/Client/CMakeLists.txt
# @brief 서버 고르기 키트(클라이언트) — 서버 고르기 화면. 전용 서버는 짓지 않는다(언리얼 Type = ClientOnly 모듈)
# ==============================================================================

sw_addGameFrameworkKit(GF_Client_ServerDirectory)
if(NOT TARGET GF_Client_ServerDirectory)
	return()
endif()
sw_linkRuntimeKit(GF_Client_ServerDirectory GF_ServerDirectory)
