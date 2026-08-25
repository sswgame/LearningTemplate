# ==============================================================================
# @file Source/Games/Demo/CMakeLists.txt
# @brief Demo 게임 MODULE (오버월드 + 액션 룸)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) SWGame — 세 키트 링크. Dev MODULE / Shipping STATIC
#    Dev delay-load: LiveReload 섀도우에서 GameFramework/Kit import 해석
# ------------------------------------------------------------------------------
if(SW_SHIPPING_BUILD)
	set(SWGAME_LIB_TYPE STATIC)
else()
	set(SWGAME_LIB_TYPE MODULE)
endif()

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_library(SWGame ${SWGAME_LIB_TYPE} ${GAME_SOURCES})

target_include_directories(SWGame PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(SWGame
	PRIVATE
		Engine
		RuntimeAPI
		GameFramework
		GF_Overworld
		GF_TurnBattle
		GF_ActionCombat
		sw_global_options
)
if(sw_flag_libraries)
	target_link_libraries(SWGame PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(SWGame
	PRIVATE
		"SW_LOG_TAG=\"Game\""
		SW_GAME_INTERNAL
)
if(SWGAME_LIB_TYPE STREQUAL "MODULE")
	target_compile_definitions(SWGame PRIVATE SW_MODULE_EXPORTS)
	sw_setModuleBinOutput(SWGame)
endif()

target_precompile_headers(SWGame PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

if(COMMAND sw_setUnityBuild)
	sw_setUnityBuild(SWGame BATCH_SIZE 8)
endif()

if(NOT SW_SHIPPING_BUILD AND WIN32)
	sw_addDelayloadHook(SWGame DLLS
		GameFramework.dll
		GF_Overworld.dll
		GF_TurnBattle.dll
		GF_ActionCombat.dll
	)
endif()

file(GLOB_RECURSE DEMO_GAME_HEADERS
	"${CMAKE_CURRENT_SOURCE_DIR}/Actors/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Combat/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/UI/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/World/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Effects/*.h"
)

sw_addReflectionStep(SWGame
	HEADERS
		${DEMO_GAME_HEADERS}
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)
