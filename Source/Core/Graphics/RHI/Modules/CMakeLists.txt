# ==============================================================================
# @file Source/Core/Graphics/RHI/Modules/CMakeLists.txt
# @brief RHI backend MODULE DLL plugins (SW_RHI_AS_MODULES)
# ==============================================================================

if(NOT WIN32)
    message(STATUS "[RHI Modules] Non-Windows: DX MODULE backends skipped")
    return()
endif()

add_subdirectory(DX11)
add_subdirectory(DX12)

# App is discovered before Core; wire runtime DLL build order here.
if(TARGET App)
    if(TARGET RHI_DX11)
        add_dependencies(App RHI_DX11)
    endif()
    if(TARGET RHI_DX12)
        add_dependencies(App RHI_DX12)
    endif()
endif()
