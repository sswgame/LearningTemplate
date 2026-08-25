# ==============================================================================
# @file Source/GameFramework/Kits/ActionCombat/CMakeLists.txt
# @brief 액션 컴뱃 키트 — GameFramework.dll delay-load
# ==============================================================================

if(SW_SHIPPING_BUILD)
	set(kitType STATIC)
else()
	set(kitType SHARED)
endif()

file(GLOB_RECURSE AC_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_library(GF_ActionCombat ${kitType} ${AC_SOURCES})

target_include_directories(GF_ActionCombat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(GF_ActionCombat
	PUBLIC
		GameFramework
		Engine
		sw_public_source_includes
	PRIVATE
		sw_global_options
)
if(sw_flag_libraries)
	target_link_libraries(GF_ActionCombat PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(GF_ActionCombat PRIVATE "SW_LOG_TAG=\"GF_ActionCombat\"")
target_precompile_headers(GF_ActionCombat PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

sw_configureGfExports(GF_ActionCombat ${kitType})
if(kitType STREQUAL "SHARED")
	sw_setModuleBinOutput(GF_ActionCombat)
	if(WIN32)
		sw_addDelayloadHook(GF_ActionCombat DLLS GameFramework.dll)
	endif()
endif()
set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES GF_ActionCombat)

file(GLOB AC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

sw_addReflectionStep(GF_ActionCombat
	HEADERS ${AC_HEADERS}
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)
