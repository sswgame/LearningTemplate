# ==============================================================================
# @file RHI_DX11 MODULE — Direct3D 11 backend plugin
# ==============================================================================

sw_add_library(RHI_DX11 MODULE
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../../DX11/D3D11RHIDevice.cpp"
    LINK_LIBRARIES
        Core
        sw_graphics_libs
)

sw_configure_module_dll_exports(RHI_DX11 MODULE)
sw_set_log_tag(RHI_DX11 RHI)
sw_set_module_bin_output(RHI_DX11)

set_target_properties(RHI_DX11 PROPERTIES FOLDER "Source/Core/Graphics/RHI/Modules")
