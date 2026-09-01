#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
	class D3D11RHIDevice;

	class D3D11RHICommandContext : public IRHICommandContext
	{
	public:
		explicit D3D11RHICommandContext( D3D11RHIDevice* pDevice )
			: _pDevice{ pDevice } {}
		~D3D11RHICommandContext() override = default;

		void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
		void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
		void prepareTextureForShaderRead( RHITextureHandle texture ) override;
		void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;
		void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
		void draw( uint32 vertexCount, uint32 startVertex = 0, RHIDescriptorIndex passCbDescriptorIndex = kInvalidDescriptorIndex,
				   RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override;
		void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
		void setViewport( const RHIViewport& viewport ) override;
		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, RHIDescriptorIndex passCbDescriptorIndex = kInvalidDescriptorIndex,
						   RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override;
		void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount, RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override;
		void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
		void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
		void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
		void beginEventMarker( const utf8* pName ) override;
		void endEventMarker() override;
		void setPipelineState( RHIPipelineStateHandle pso ) override;
		void setComputePipelineState( RHIPipelineStateHandle pso ) override;
		void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override;
		void endRenderPass() override;
		void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) override;
		void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState ) override;
		void beginOffscreenPass( RHITextureHandle colorTarget, const float4& clearColor ) override;
		void endOffscreenPass( RHITextureHandle colorTarget ) override;

	private:
		void			   bindPassAndMaterialCb( RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex );
		D3D11RHIDevice*	   _pDevice;
		RHIDescriptorIndex _lastBoundMaterialDescriptor{ kInvalidDescriptorIndex };
	};
} // namespace sw
#endif
