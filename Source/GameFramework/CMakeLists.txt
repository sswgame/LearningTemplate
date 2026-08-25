# ==============================================================================
# @file Source/GameFramework/CMakeLists.txt
# @brief 장르 비의존 게임플레이 프레임워크 (Dev: SHARED / Shipping: STATIC)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) GameFramework — Kits/는 별도 타겟이라 GLOB에서 제외
#    export는 sw_configureGfExports (sw_addGfKit와 동일 로직)
# ------------------------------------------------------------------------------
if(SW_SHIPPING_BUILD)
	set(gfLibType STATIC)
else()
	set(gfLibType SHARED)
endif()

sw_addLibrary(GameFramework
	TYPE ${gfLibType}
	EXCLUDE "Kits/"
	LINK_LIBRARIES Engine
	LOG_TAG GameFramework
	PCH
	BIN_OUTPUT
)

sw_configureGfExports(GameFramework ${gfLibType})
set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES GameFramework)

target_link_libraries(GameFramework PUBLIC sw_public_source_includes)
if(COMMAND sw_setUnityBuild)
	sw_setUnityBuild(GameFramework BATCH_SIZE 8)
endif()

file(GLOB_RECURSE GF_HEADERS
	"${CMAKE_CURRENT_SOURCE_DIR}/Base/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Data/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/UI/*.h"
)

sw_addReflectionStep(GameFramework
	HEADERS ${GF_HEADERS}
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)

add_subdirectory(Kits)

target_compile_definitions(GameFramework PRIVATE SW_GAME_INTERNAL)
