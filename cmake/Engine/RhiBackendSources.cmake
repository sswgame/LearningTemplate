# ==============================================================================
# @file cmake/Engine/RhiBackendSources.cmake
# @brief RHI 백엔드 device .cpp 목록 — RHI_* MODULE(Dev) / Engine 정적 링크(Shipping) 공유
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 백엔드별 device .cpp — 알파벳 순 정렬
# ------------------------------------------------------------------------------
set(swRhiRoot "${CMAKE_SOURCE_DIR}/Source/Engine/Graphics/RHI")

set(SW_RHI_DX11_DEVICE_SOURCES
    "${swRhiRoot}/DX11/D3D11RHICommandContext.cpp"
    "${swRhiRoot}/DX11/D3D11RHICommandList.cpp"
    "${swRhiRoot}/DX11/D3D11RHIDevice.cpp"
    "${swRhiRoot}/DX11/D3D11RHIDeviceInit.cpp"
    "${swRhiRoot}/DX11/D3D11RHIDeviceSubmission.cpp"
    "${swRhiRoot}/DX11/D3D11RHIResourceFactory.cpp"
    "${swRhiRoot}/DX11/D3D11RHIResourceFactoryBindless.cpp"
    "${swRhiRoot}/DX11/D3D11RHIResourceFactoryPipeline.cpp"
    "${swRhiRoot}/DX11/D3D11RHISwapChain.cpp"
)
set(SW_RHI_DX12_DEVICE_SOURCES
    "${swRhiRoot}/DX12/D3D12RHICommandContext.cpp"
    "${swRhiRoot}/DX12/D3D12RHICommandList.cpp"
    "${swRhiRoot}/DX12/D3D12RHIDevice.cpp"
    "${swRhiRoot}/DX12/D3D12RHIDeviceDescriptor.cpp"
    "${swRhiRoot}/DX12/D3D12RHIDeviceInit.cpp"
    "${swRhiRoot}/DX12/D3D12RHIDeviceSubmission.cpp"
    "${swRhiRoot}/DX12/D3D12RHIResourceFactory.cpp"
    "${swRhiRoot}/DX12/D3D12RHIResourceFactoryBindless.cpp"
    "${swRhiRoot}/DX12/D3D12RHIResourceFactoryPipeline.cpp"
    "${swRhiRoot}/DX12/D3D12RHISwapChain.cpp"
)
set(SW_RHI_GL_DEVICE_SOURCES
    "${swRhiRoot}/GL/OpenGLRHICommandContext.cpp"
    "${swRhiRoot}/GL/OpenGLRHIDevice.cpp"
    "${swRhiRoot}/GL/OpenGLRHIDeviceInit.cpp"
    "${swRhiRoot}/GL/OpenGLRHIDeviceSubmission.cpp"
    "${swRhiRoot}/GL/OpenGLRHIResourceFactory.cpp"
    "${swRhiRoot}/GL/OpenGLRHIResourceFactoryBindless.cpp"
    "${swRhiRoot}/GL/OpenGLRHIResourceFactoryPipeline.cpp"
    "${swRhiRoot}/GL/Platform/IOpenGLPlatformContext.cpp"
    "${swRhiRoot}/GL/Platform/WglPlatformContext.cpp"
    "${swRhiRoot}/GL/Platform/GlxPlatformContext.cpp"
)
set(SW_RHI_VULKAN_DEVICE_SOURCES
    "${swRhiRoot}/Vulkan/VulkanRHICommandContext.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHICommandList.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIDevice.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIDeviceDescriptor.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIDeviceInit.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIDeviceRenderPass.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIDeviceSubmission.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIRenderPassCache.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIResourceFactory.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIResourceFactoryBindless.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIResourceFactoryPipeline.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHISwapChain.cpp"
)

set(SW_RHI_ALL_DEVICE_SOURCES
    ${SW_RHI_DX11_DEVICE_SOURCES}
    ${SW_RHI_DX12_DEVICE_SOURCES}
    ${SW_RHI_GL_DEVICE_SOURCES}
    ${SW_RHI_VULKAN_DEVICE_SOURCES}
)

unset(swRhiRoot)
