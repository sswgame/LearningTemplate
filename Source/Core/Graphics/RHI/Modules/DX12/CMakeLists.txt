# ==============================================================================
# @file RHI_DX12 MODULE — Direct3D 12 backend plugin
# ==============================================================================

sw_add_library(RHI_DX12 MODULE
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../../DX12/D3D12RHIDevice.cpp"
    LINK_LIBRARIES
        Core
        sw_graphics_libs
)

sw_configure_module_dll_exports(RHI_DX12 MODULE)
sw_set_log_tag(RHI_DX12 RHI)
sw_set_module_bin_output(RHI_DX12)

set_target_properties(RHI_DX12 PROPERTIES FOLDER "Source/Core/Graphics/RHI/Modules")
