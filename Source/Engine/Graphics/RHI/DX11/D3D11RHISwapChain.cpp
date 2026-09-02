#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHISwapChain.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    void  D3D11RHISwapChain::resize( uint32 width, uint32 height ) { _pDevice->resize( width, height ); }
    void  D3D11RHISwapChain::beginFrame( const float4& clearColor ) { _pDevice->beginFrame( clearColor ); }
    void  D3D11RHISwapChain::endFrame( bool vsync, bool bPresent ) { _pDevice->endFrame( vsync, bPresent ); }
    void* D3D11RHISwapChain::getNativeSwapChain() const { return _pDevice->getNativeSwapChain(); }
} // namespace sw
#endif
