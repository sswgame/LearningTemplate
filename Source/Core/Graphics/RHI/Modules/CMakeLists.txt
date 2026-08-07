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

# App is discovered before Core; wire runtime DLL/so build order here.
if(TARGET App)
    foreach(_rhi_mod IN ITEMS RHI_DX11 RHI_DX12 RHI_GL RHI_Vulkan)
        if(TARGET ${_rhi_mod})
            add_dependencies(App ${_rhi_mod})
        endif()
    endforeach()
endif()
