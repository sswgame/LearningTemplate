# ==============================================================================
# @file RHI_DX11 MODULE — Direct3D 11 backend plugin
# ==============================================================================

include("${CMAKE_CURRENT_SOURCE_DIR}/../../RhiSources.cmake")

sw_add_library(RHI_DX11
    TYPE MODULE
    SOURCES
        ${SW_RHI_DX11_DEVICE_SOURCES}
    LINK_LIBRARIES
        Core
)

sw_configure_module_dll_exports(RHI_DX11 MODULE)
sw_set_log_tag(RHI_DX11 RHI)
sw_set_module_bin_output(RHI_DX11)

set_target_properties(RHI_DX11 PROPERTIES FOLDER "Source/Core/Graphics/RHI/Modules")
