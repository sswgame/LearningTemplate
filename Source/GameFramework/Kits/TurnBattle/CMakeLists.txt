# ==============================================================================
# @file Source/GameFramework/Kits/TurnBattle/CMakeLists.txt
# @brief 턴제 배틀 키트 — GameFramework.dll delay-load
# ==============================================================================

if(SW_SHIPPING_BUILD)
	set(kitType STATIC)
else()
	set(kitType SHARED)
endif()

file(GLOB_RECURSE TB_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_library(GF_TurnBattle ${kitType} ${TB_SOURCES})

target_include_directories(GF_TurnBattle PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(GF_TurnBattle
	PUBLIC
		GameFramework
		Engine
		sw_public_source_includes
	PRIVATE
		sw_global_options
)
if(sw_flag_libraries)
	target_link_libraries(GF_TurnBattle PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(GF_TurnBattle PRIVATE "SW_LOG_TAG=\"GF_TurnBattle\"")
target_precompile_headers(GF_TurnBattle PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

sw_configureGfExports(GF_TurnBattle ${kitType})
if(kitType STREQUAL "SHARED")
	sw_setModuleBinOutput(GF_TurnBattle)
	if(WIN32)
		sw_addDelayloadHook(GF_TurnBattle DLLS GameFramework.dll)
	endif()
endif()
set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES GF_TurnBattle)
