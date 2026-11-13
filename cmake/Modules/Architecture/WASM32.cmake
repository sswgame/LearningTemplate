# ==============================================================================
# @file cmake/Modules/Architecture/WASM32.cmake
# @brief WASM32 아키텍처 INTERFACE 매크로 (판정은 DetectArchitecture.cmake)
# ==============================================================================

if(NOT SW_TARGET_ARCHITECTURE STREQUAL "wasm32")
	return()
endif()

add_library(sw_architecture_wasm32 INTERFACE)
target_compile_definitions(sw_architecture_wasm32 INTERFACE SW_WASM32)
list(APPEND SW_FLAG_LIBRARIES sw_architecture_wasm32)
