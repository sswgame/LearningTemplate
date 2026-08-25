#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHISwapChain.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"

namespace sw
{
	void  OpenGLRHISwapChain::resize( uint32 width, uint32 height ) { _pDevice->resize( width, height ); }
	void  OpenGLRHISwapChain::beginFrame( float32 clearColor[4] ) { _pDevice->beginFrame( clearColor ); }
	void  OpenGLRHISwapChain::endFrame( bool vsync, bool bPresent ) { _pDevice->endFrame( vsync, bPresent ); }
	void* OpenGLRHISwapChain::getNativeSwapChain() const { return _pDevice->getNativeSwapChain(); }
} // namespace sw
