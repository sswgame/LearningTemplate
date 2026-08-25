#pragma once
#include "Core/Common/Types.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"

namespace sw
{
	class VulkanRHIDevice;

	class VulkanRHISwapChain : public IRHISwapChain
	{
	public:
		explicit VulkanRHISwapChain( VulkanRHIDevice* pDevice )
			: _pDevice{ pDevice } {}
		void  resize( uint32 width, uint32 height ) override;
		void  beginFrame( float32 clearColor[4] ) override;
		void  endFrame( bool vsync = true, bool bPresent = true ) override;
		void* getNativeSwapChain() const override;

	private:
		VulkanRHIDevice* _pDevice;
	};
} // namespace sw
