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

file(GLOB_RECURSE GF_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
list(FILTER GF_SOURCES EXCLUDE REGEX "Kits/")

add_library(GameFramework ${gfLibType} ${GF_SOURCES})
set_target_properties(GameFramework PROPERTIES FOLDER "Source/GameFramework")

target_include_directories(GameFramework PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(GameFramework
	PUBLIC
		Engine
		sw_public_source_includes
	PRIVATE
		sw_global_options
)
if(sw_flag_libraries)
	target_link_libraries(GameFramework PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(GameFramework
	PRIVATE
		"SW_LOG_TAG=\"GameFramework\""
		SW_GAME_INTERNAL
)
sw_configurePch(GameFramework "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

sw_configureGfExports(GameFramework ${gfLibType})
if(gfLibType STREQUAL "SHARED")
	sw_setModuleBinOutput(GameFramework)
endif()
set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES GameFramework)
if(COMMAND sw_setUnityBuild)
	sw_setUnityBuild(GameFramework BATCH_SIZE 8)
endif()

file(GLOB_RECURSE GF_HEADERS
	"${CMAKE_CURRENT_SOURCE_DIR}/Base/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Data/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Transition/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/UI/*.h"
)

sw_addReflectionStep(GameFramework
	HEADERS ${GF_HEADERS}
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)

add_subdirectory(Kits)
