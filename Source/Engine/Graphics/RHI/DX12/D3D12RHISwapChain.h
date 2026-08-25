#pragma once
#include "Core/Common/Types.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
	class D3D12RHIDevice;

	class D3D12RHISwapChain : public IRHISwapChain
	{
	public:
		explicit D3D12RHISwapChain( D3D12RHIDevice* pDevice )
			: _pDevice{ pDevice } {}
		void  resize( uint32 width, uint32 height ) override;
		void  beginFrame( float32 clearColor[4] ) override;
		void  endFrame( bool vsync = true, bool bPresent = true ) override;
		void* getNativeSwapChain() const override;

	private:
		D3D12RHIDevice* _pDevice;
	};
} // namespace sw
#endif
