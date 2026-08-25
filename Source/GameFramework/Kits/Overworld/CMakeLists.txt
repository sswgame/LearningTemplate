# ==============================================================================
# @file Source/GameFramework/Kits/Overworld/CMakeLists.txt
# @brief 오버월드 키트 — GameFramework.dll delay-load
# ==============================================================================

sw_addGfKit(GF_Overworld
	LINK_LIBRARIES GameFramework Engine
	DELAYLOAD_DLLS GameFramework.dll
)

file(GLOB OW_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

sw_addReflectionStep(GF_Overworld
	HEADERS ${OW_HEADERS}
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)
