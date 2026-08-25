#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.h"

namespace sw
{
	void  VulkanRHISwapChain::resize( uint32 width, uint32 height ) { _pDevice->resize( width, height ); }
	void  VulkanRHISwapChain::beginFrame( float32 clearColor[4] ) { _pDevice->beginFrame( clearColor ); }
	void  VulkanRHISwapChain::endFrame( bool vsync, bool bPresent ) { _pDevice->endFrame( vsync, bPresent ); }
	void* VulkanRHISwapChain::getNativeSwapChain() const { return _pDevice->getNativeSwapChain(); }
} // namespace sw
