# ==============================================================================
# @file Source/Core/Graphics/RHI/Modules/CMakeLists.txt
# @brief RHI backend MODULE DLL plugins (SW_RHI_AS_MODULES)
# ==============================================================================

# DX is Windows-only; GL/Vulkan build on all platforms that have Modules enabled.
if(WIN32)
    add_subdirectory(DX11)
    add_subdirectory(DX12)
endif()

add_subdirectory(GL)
add_subdirectory(Vulkan)
