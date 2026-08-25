# ==============================================================================
# @file Source/Engine/Graphics/RHI/RhiSources.cmake
# @brief RHI 백엔드 device .cpp 목록 — Engine(모듈 OFF) / RHI_* MODULE(모듈 ON) 공유
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 백엔드별 device .cpp — Engine GLOB 제외 목록과 RHI_* SOURCES가 동일 리스트를 씀
# ------------------------------------------------------------------------------
set(swRhiRoot "${CMAKE_SOURCE_DIR}/Source/Engine/Graphics/RHI")

set(SW_RHI_DX11_DEVICE_SOURCES
    "${swRhiRoot}/DX11/D3D11RHIDevice.cpp"
    "${swRhiRoot}/DX11/D3D11RHICommandContext.cpp"
    "${swRhiRoot}/DX11/D3D11RHISwapChain.cpp"
    "${swRhiRoot}/DX11/D3D11RHIResource.cpp"
)
set(SW_RHI_DX12_DEVICE_SOURCES
    "${swRhiRoot}/DX12/D3D12RHIDevice.cpp"
    "${swRhiRoot}/DX12/D3D12RHICommandContext.cpp"
    "${swRhiRoot}/DX12/D3D12RHISwapChain.cpp"
    "${swRhiRoot}/DX12/D3D12RHIResource.cpp"
)
set(SW_RHI_GL_DEVICE_SOURCES
    "${swRhiRoot}/GL/OpenGLRHIDevice.cpp"
    "${swRhiRoot}/GL/OpenGLRHICommandContext.cpp"
    "${swRhiRoot}/GL/OpenGLRHISwapChain.cpp"
    "${swRhiRoot}/GL/OpenGLRHIResource.cpp"
)
set(SW_RHI_VULKAN_DEVICE_SOURCES
    "${swRhiRoot}/Vulkan/VulkanRHIDevice.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHICommandContext.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHISwapChain.cpp"
    "${swRhiRoot}/Vulkan/VulkanRHIResource.cpp"
)

set(SW_RHI_ALL_DEVICE_SOURCES
    ${SW_RHI_DX11_DEVICE_SOURCES}
    ${SW_RHI_DX12_DEVICE_SOURCES}
    ${SW_RHI_GL_DEVICE_SOURCES}
    ${SW_RHI_VULKAN_DEVICE_SOURCES}
)

unset(swRhiRoot)
