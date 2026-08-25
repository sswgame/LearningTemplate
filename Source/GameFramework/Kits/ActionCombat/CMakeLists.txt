# ==============================================================================
# @file Source/GameFramework/Kits/ActionCombat/CMakeLists.txt
# @brief 액션 컴뱃 키트 — GameFramework.dll delay-load
# ==============================================================================

sw_addGfKit(GF_ActionCombat
	LINK_LIBRARIES GameFramework Engine
	DELAYLOAD_DLLS GameFramework.dll
)

file(GLOB AC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

sw_addReflectionStep(GF_ActionCombat
	HEADERS ${AC_HEADERS}
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)
