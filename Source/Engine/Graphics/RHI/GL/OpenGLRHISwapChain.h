#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHISwapChain.h"

namespace sw
{
	class OpenGLRHIDevice;

	class OpenGLRHISwapChain : public IRHISwapChain
	{
	public:
		explicit OpenGLRHISwapChain( OpenGLRHIDevice* pDevice )
			: _pDevice{ pDevice } {}
		void  resize( uint32 width, uint32 height ) override;
		void  beginFrame( const float4& clearColor ) override;
		void  endFrame( bool vsync = true, bool bPresent = true ) override;
		void* getNativeSwapChain() const override;

	private:
		OpenGLRHIDevice* _pDevice;
	};
} // namespace sw
