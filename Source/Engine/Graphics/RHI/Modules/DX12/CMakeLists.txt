# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/DX12/CMakeLists.txt
# @brief RHI_DX12 MODULE — Direct3D 12 백엔드
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) MODULE — RhiSources.cmake의 device .cpp를 Engine과 공유
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/RhiSources.cmake")

add_library(RHI_DX12 MODULE "${CMAKE_CURRENT_SOURCE_DIR}/ModuleEntry.cpp" ${SW_RHI_DX12_DEVICE_SOURCES})

target_include_directories(RHI_DX12 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(RHI_DX12
	PRIVATE
		Engine
		sw_graphics_dx12_libs
		sw_third_party_includes
		sw_global_options
)
if(sw_flag_libraries)
	target_link_libraries(RHI_DX12 PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(RHI_DX12
	PRIVATE
		"SW_LOG_TAG=\"RHI\""
		SW_MODULE_EXPORTS
)
target_precompile_headers(RHI_DX12 PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
sw_setModuleBinOutput(RHI_DX12)
set_target_properties(RHI_DX12 PROPERTIES FOLDER "Source/Engine/Graphics/RHI/Modules")
