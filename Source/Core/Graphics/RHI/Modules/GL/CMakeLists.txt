# ==============================================================================
# @file RHI_GL MODULE — OpenGL backend plugin
# ==============================================================================

include("${CMAKE_CURRENT_SOURCE_DIR}/../../RhiSources.cmake")

sw_add_library(RHI_GL
    TYPE MODULE
    SOURCES
        ${SW_RHI_GL_DEVICE_SOURCES}
    LINK_LIBRARIES
        Core
)

sw_configure_module_dll_exports(RHI_GL MODULE)
sw_set_log_tag(RHI_GL RHI)
sw_set_module_bin_output(RHI_GL)

set_target_properties(RHI_GL PROPERTIES FOLDER "Source/Core/Graphics/RHI/Modules")
