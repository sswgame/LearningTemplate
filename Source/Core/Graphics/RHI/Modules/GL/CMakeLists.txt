# ==============================================================================
# @file RHI_GL MODULE — OpenGL backend plugin
# ==============================================================================

sw_add_library(RHI_GL MODULE
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../../GL/OpenGLRHIDevice.cpp"
    LINK_LIBRARIES
        Core
        sw_graphics_libs
)

sw_configure_module_dll_exports(RHI_GL MODULE)
sw_set_log_tag(RHI_GL RHI)
sw_set_module_bin_output(RHI_GL)

set_target_properties(RHI_GL PROPERTIES FOLDER "Source/Core/Graphics/RHI/Modules")
