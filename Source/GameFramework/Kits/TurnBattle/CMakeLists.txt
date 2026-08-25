# ==============================================================================
# @file Source/GameFramework/Kits/TurnBattle/CMakeLists.txt
# @brief 턴제 배틀 키트 — GameFramework.dll delay-load
# ==============================================================================

sw_addGfKit(GF_TurnBattle
	LINK_LIBRARIES GameFramework Engine
	DELAYLOAD_DLLS GameFramework.dll
)
