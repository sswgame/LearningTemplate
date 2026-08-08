# ==============================================================================
# @file RHI_Vulkan MODULE — Vulkan backend plugin
# ==============================================================================

include("${CMAKE_CURRENT_SOURCE_DIR}/../../RhiSources.cmake")

sw_add_library(RHI_Vulkan MODULE
    SOURCES
        ${SW_RHI_VULKAN_DEVICE_SOURCES}
    LINK_LIBRARIES
        Core
        sw_graphics_libs
)

sw_configure_module_dll_exports(RHI_Vulkan MODULE)
sw_set_log_tag(RHI_Vulkan RHI)
sw_set_module_bin_output(RHI_Vulkan)

set_target_properties(RHI_Vulkan PROPERTIES FOLDER "Source/Core/Graphics/RHI/Modules")
