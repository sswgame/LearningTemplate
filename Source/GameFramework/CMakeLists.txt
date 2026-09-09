# ==============================================================================
# @file Source/GameFramework/CMakeLists.txt
# @brief 장르 비의존 게임플레이 프레임워크 (Dev: SHARED / Shipping: STATIC)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) GameFramework — Kits/는 별도 타겟이라 GLOB에서 제외
# export는 sw_configureGfExports (sw_addGfKit와 동일 로직)
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

# 리플렉션 대상 헤더는 소스와 **같은 규칙**으로 모은다. 예전에는 Base/Data/Transition/UI 네
# 폴더를 이름으로 적어 두었는데, 최상위 폴더를 하나 더 만들면 그 안의 REFLECT() 타입이
# 조용히 등록되지 않았다 — 컴파일은 되고 역직렬화만 실패하므로 찾기 어렵다.
file(GLOB_RECURSE GF_HEADERS CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)
list(FILTER GF_HEADERS EXCLUDE REGEX "Kits/")

sw_addReflectionStep(GameFramework
	HEADERS ${GF_HEADERS}
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)

add_subdirectory(Kits)
