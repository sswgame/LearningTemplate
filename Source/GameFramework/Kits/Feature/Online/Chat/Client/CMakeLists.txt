# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Online/Chat/Client/CMakeLists.txt
# @brief 채팅 키트(클라이언트) — 채팅 화면. 전용 서버는 짓지 않는다(언리얼 Type = ClientOnly 모듈)
# ==============================================================================

sw_addGameFrameworkKit(GF_Client_Chat)
if(NOT TARGET GF_Client_Chat)
	return()
endif()
sw_linkRuntimeKit(GF_Client_Chat GF_Chat)
