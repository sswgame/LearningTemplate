# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/GL/CMakeLists.txt
# @brief RHI_GL MODULE — OpenGL 백엔드
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) MODULE — RhiSources.cmake의 device .cpp를 Engine과 공유
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/RhiSources.cmake")

add_library(RHI_GL MODULE "${CMAKE_CURRENT_SOURCE_DIR}/ModuleEntry.cpp" ${SW_RHI_GL_DEVICE_SOURCES})

target_include_directories(RHI_GL PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(RHI_GL
	PRIVATE
		Engine
		sw_graphics_gl_libs
		sw_third_party_includes
		sw_global_options
)
if(sw_flag_libraries)
	target_link_libraries(RHI_GL PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(RHI_GL
	PRIVATE
		"SW_LOG_TAG=\"RHI\""
		SW_MODULE_EXPORTS
)
target_precompile_headers(RHI_GL PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
sw_setModuleBinOutput(RHI_GL)
set_target_properties(RHI_GL PROPERTIES FOLDER "Source/Engine/Graphics/RHI/Modules")
