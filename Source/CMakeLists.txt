# ==============================================================================
# @file Source/CMakeLists.txt
# @brief Source 제품 그래프 (Game은 ReflectionParser 이후 루트에서 등록)
# ==============================================================================

add_subdirectory(Runtime)
add_subdirectory(Core)
add_subdirectory(Editor)
add_subdirectory(App)

# App is registered after Core; wire RHI_* MODULE build order here.
if(TARGET App)
    foreach(_rhi_mod IN ITEMS RHI_DX11 RHI_DX12 RHI_GL RHI_Vulkan)
        if(TARGET ${_rhi_mod})
            add_dependencies(App ${_rhi_mod})
        endif()
    endforeach()
endif()
