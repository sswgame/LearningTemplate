#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHISwapChain.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
	class D3D11RHIDevice;

	class D3D11RHISwapChain : public IRHISwapChain
	{
	public:
		explicit D3D11RHISwapChain( D3D11RHIDevice* pDevice )
			: _pDevice{ pDevice } {}
		void  resize( uint32 width, uint32 height ) override;
		void  beginFrame( float32 clearColor[4] ) override;
		void  endFrame( bool vsync = true, bool bPresent = true ) override;
		void* getNativeSwapChain() const override;

	private:
		D3D11RHIDevice* _pDevice;
	};
} // namespace sw
#endif
