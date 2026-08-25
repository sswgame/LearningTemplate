#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHIResource.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
	class D3D12RHIDevice;

	class D3D12RHIResource : public IRHIResource
	{
	public:
		explicit D3D12RHIResource( D3D12RHIDevice* pDevice )
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
		D3D12RHIDevice* _pDevice;
	};
} // namespace sw
#endif
