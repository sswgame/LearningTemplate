# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/Vulkan/CMakeLists.txt
# @brief RHI_Vulkan MODULE — Vulkan 백엔드
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) MODULE — RhiSources.cmake의 device .cpp를 Engine과 공유
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/RhiSources.cmake")

add_library(RHI_Vulkan MODULE "${CMAKE_CURRENT_SOURCE_DIR}/ModuleEntry.cpp" ${SW_RHI_VULKAN_DEVICE_SOURCES})

target_include_directories(RHI_Vulkan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(RHI_Vulkan
	PRIVATE
		Engine
		sw_graphics_vulkan_libs
		sw_third_party_includes
		sw_global_options
)
if(sw_flag_libraries)
	target_link_libraries(RHI_Vulkan PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(RHI_Vulkan
	PRIVATE
		"SW_LOG_TAG=\"RHI\""
		SW_MODULE_EXPORTS
)
target_precompile_headers(RHI_Vulkan PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
sw_setModuleBinOutput(RHI_Vulkan)
set_target_properties(RHI_Vulkan PROPERTIES FOLDER "Source/Engine/Graphics/RHI/Modules")
