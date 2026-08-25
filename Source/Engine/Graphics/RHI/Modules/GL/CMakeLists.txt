# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/GL/CMakeLists.txt
# @brief RHI_GL MODULE — OpenGL 백엔드
# ==============================================================================

include("${CMAKE_SOURCE_DIR}/cmake/Engine/RhiBackendSources.cmake")
sw_addRhiBackendModule(RHI_GL sw_graphics_gl_libs SOURCES ${SW_RHI_GL_DEVICE_SOURCES})
