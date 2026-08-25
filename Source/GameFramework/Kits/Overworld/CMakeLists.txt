# ==============================================================================
# @file Source/GameFramework/Kits/Overworld/CMakeLists.txt
# @brief 오버월드 키트 — GameFramework.dll delay-load
# ==============================================================================

if(SW_SHIPPING_BUILD)
	set(kitType STATIC)
else()
	set(kitType SHARED)
endif()

file(GLOB_RECURSE OW_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_library(GF_Overworld ${kitType} ${OW_SOURCES})

target_include_directories(GF_Overworld PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(GF_Overworld
	PUBLIC
		GameFramework
		Engine
		sw_public_source_includes
	PRIVATE
		sw_global_options
)
if(sw_flag_libraries)
	target_link_libraries(GF_Overworld PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(GF_Overworld PRIVATE "SW_LOG_TAG=\"GF_Overworld\"")
target_precompile_headers(GF_Overworld PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

sw_configureGfExports(GF_Overworld ${kitType})
if(kitType STREQUAL "SHARED")
	sw_setModuleBinOutput(GF_Overworld)
	if(WIN32)
		sw_addDelayloadHook(GF_Overworld DLLS GameFramework.dll)
	endif()
endif()
set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES GF_Overworld)

file(GLOB OW_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

sw_addReflectionStep(GF_Overworld
	HEADERS ${OW_HEADERS}
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)
