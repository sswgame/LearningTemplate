# ==============================================================================
# @file Source/Core/Graphics/RHI/RhiSources.cmake
# @brief RHI backend device .cpp lists — Core (modules OFF) / RHI_* MODULE (modules ON) 공유
# ==============================================================================

set(_sw_rhi_root "${CMAKE_CURRENT_LIST_DIR}")

set(SW_RHI_DX11_DEVICE_SOURCES
    "${_sw_rhi_root}/DX11/D3D11RHIDevice.cpp"
)
set(SW_RHI_DX12_DEVICE_SOURCES
    "${_sw_rhi_root}/DX12/D3D12RHIDevice.cpp"
)
set(SW_RHI_GL_DEVICE_SOURCES
    "${_sw_rhi_root}/GL/OpenGLRHIDevice.cpp"
)
set(SW_RHI_VULKAN_DEVICE_SOURCES
    "${_sw_rhi_root}/Vulkan/VulkanRHIDevice.cpp"
)

set(SW_RHI_ALL_DEVICE_SOURCES
    ${SW_RHI_DX11_DEVICE_SOURCES}
    ${SW_RHI_DX12_DEVICE_SOURCES}
    ${SW_RHI_GL_DEVICE_SOURCES}
    ${SW_RHI_VULKAN_DEVICE_SOURCES}
)

unset(_sw_rhi_root)
