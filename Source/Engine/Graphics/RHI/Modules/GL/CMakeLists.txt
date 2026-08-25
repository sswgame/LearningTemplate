# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/GL/CMakeLists.txt
# @brief RHI_GL MODULE — OpenGL 백엔드
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) MODULE — RhiSources.cmake의 device .cpp를 Engine과 공유
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/RhiSources.cmake")

sw_addLibrary(RHI_GL
	TYPE MODULE
	SOURCES ${SW_RHI_GL_DEVICE_SOURCES}
	LINK_LIBRARIES Engine sw_graphics_gl_libs sw_third_party_includes
	LOG_TAG RHI
	PCH
)
set_target_properties(RHI_GL PROPERTIES FOLDER "Source/Engine/Graphics/RHI/Modules")
