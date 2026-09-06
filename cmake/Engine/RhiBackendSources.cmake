# ==============================================================================
# @file cmake/Engine/RhiBackendSources.cmake
# @brief RHI 백엔드 device .cpp 목록 — Engine(모듈 OFF) / RHI_* MODULE(모듈 ON) 공유
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 백엔드별 device .cpp — 알파벳 순 정렬
# ------------------------------------------------------------------------------
set(swRhiRoot "${CMAKE_SOURCE_DIR}/Source/Engine/Graphics/RHI")

set(SW_RHI_DX11_DEVICE_SOURCES
    "${swRhiRoot}/DX11/D3D11RHICommandContext.cpp"
    "${swRhiRoot}/DX11/D3D11RHICommandList.cpp"
    "${swRhiRoot}/DX11/D3D11RHIDevice.cpp"
    "${swRhiRoot}/DX11/D3D11RHIResource.cpp"
    "${swRhiRoot}/DX11/D3D11RHIResourceBindless.cpp"
    "${swRhiRoot}/DX11/D3D11RHIResourcePipeline.cpp"
    "${swRhiRoot}/DX11/D3D11RHISwapChain.cpp"
)
set(SW_RHI_DX12_DEVICE_SOURCES
    "${swRhiRoot}/DX12/D3D12RHICommandContext.cpp"
    "${swRhiRoot}/DX12/D3D12RHICommandList.cpp"
    "${swRhiRoot}/DX12/D3D12RHIDevice.cpp"
    "${swRhiRoot}/DX12/D3D12RHIResource.cpp"
    "${swRhiRoot}/DX12/D3D12RHIResourceBindless.cpp"
    "${swRhiRoot}/DX12/D3D12RHIResourcePipeline.cpp"
    "${swRhiRoot}/DX12/D3D12RHISwapChain.cpp"
)
set(SW_RHI_GL_DEVICE_SOURCES
    "${swRhiRoot}/GL/OpenGLRHICommandContext.cpp"
    "${swRhiRoot}/GL/OpenGLRHIDevice.cpp"
    "${swRhiRoot}/GL/OpenGLRHIResource.cpp"
    "${swRhiRoot}/GL/OpenGLRHIResourceBindless.cpp"
    "${swRhiRoot}/GL/OpenGLRHIResourcePipeline.cpp"
    "${swRhiRoot}/GL/OpenGLRHISwapChain.cpp"
)
set(SW_RHI_VULKAN_DEVICE_SOURCES
    "${swRhiRoot}/Vulkan/VulkanRHICommandContext.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHICommandList.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIDevice.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIDeviceDescriptor.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIDeviceInit.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIDeviceRenderPass.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIResource.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIResourceBindless.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIResourcePipeline.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHISwapChain.cpp"
)

set(SW_RHI_ALL_DEVICE_SOURCES
    ${SW_RHI_DX11_DEVICE_SOURCES}
    ${SW_RHI_DX12_DEVICE_SOURCES}
    ${SW_RHI_GL_DEVICE_SOURCES}
    ${SW_RHI_VULKAN_DEVICE_SOURCES}
)

unset(swRhiRoot)
