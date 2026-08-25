#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHIResource.h"

namespace sw
{
	class OpenGLRHIDevice;

	class OpenGLRHIResource : public IRHIResource
	{
	public:
		explicit OpenGLRHIResource( OpenGLRHIDevice* pDevice )
			: _pDevice{ pDevice } {}
		RHIPipelineStateHandle createPipelineState( const RHIPipelineStateDesc& desc ) override;
		RHIPipelineStateHandle createComputePipelineState( string_view shaderPath, string_view entryPoint = "CSMain" ) override;
		void				   destroyPipelineState( RHIPipelineStateHandle pso ) override;
		RHIRenderPassHandle	   createRenderPass( const RHIRenderPassDesc& desc ) override;
		void				   destroyRenderPass( RHIRenderPassHandle pass ) override;
		RHIBufferHandle		   createConstantBuffer( uint32 size ) override;
		void				   updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
		RHIBufferHandle		   createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;
		void				   updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
		RHIBufferHandle		   createBuffer( const RHIBufferDesc& desc ) override;
		RHIBufferHandle		   createVertexBuffer( const void* pData, uint32 sizeBytes ) override;
		void				   destroyBuffer( RHIBufferHandle buffer ) override;
		RHITextureHandle	   createTexture2D( const RHITextureDesc& desc ) override;
		void				   destroyTexture( RHITextureHandle texture ) override;
		RHIDescriptorIndex	   registerBindlessTexture( RHITextureHandle texture ) override;
		RHIDescriptorIndex	   registerBindlessResource( RHIBufferHandle buffer ) override;
		void				   unregisterBindlessResource( RHIDescriptorIndex index ) override;
		RHIDescriptorIndex	   registerBindlessUAV( RHIBufferHandle buffer ) override;
		void				   unregisterBindlessUAV( RHIDescriptorIndex index ) override;

	private:
		OpenGLRHIDevice* _pDevice;
	};
} // namespace sw
