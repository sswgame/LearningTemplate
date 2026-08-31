#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"

namespace sw
{
	class VulkanRHIDevice;

	class VulkanRHICommandContext : public IRHICommandContext
	{
	public:
		explicit VulkanRHICommandContext( VulkanRHIDevice* pDevice )
			: _pDevice{ pDevice } {}
		~VulkanRHICommandContext() override = default;

		void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
		void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
		void prepareTextureForShaderRead( RHITextureHandle texture ) override;
		void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;
		void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
		void draw( uint32 vertexCount, uint32 startVertex = 0, RHIDescriptorIndex materialDescriptorIndex = kInvalidDescriptorIndex ) override;
		void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
		void setViewport( const RHIViewport& viewport ) override;
		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, RHIDescriptorIndex materialDescriptorIndex = kInvalidDescriptorIndex ) override;
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
		/**
		 * @brief 그래픽스 draw 직전 set 0(머티리얼/Pass UBO)·set 1(bindless 텍스처)을 바인딩합니다.
		 * @details 셰이더가 set 0 을 정적으로 참조하므로 모든 draw 경로(인다이렉트 포함)에서 필요합니다.
		 *          유효한 머티리얼 디스크립터가 없으면 디바이스의 기본 셋(_descriptorSet)으로 폴백합니다.
		 */
		void bindGraphicsMaterialSets( RHIDescriptorIndex materialDescriptorIndex );

		/**
		 * @brief 현재 활성 그래픽스 PSO(없으면 오프스크린/기본 파이프라인)를 바인딩합니다.
		 * @details 모든 draw 계열이 직전 상태에 의존하지 않고 스스로 파이프라인을 세우도록 합니다.
		 * @return 바인딩할 파이프라인이 있으면 true, 없으면 false(드로우를 건너뛰어야 함).
		 */
		bool bindActiveGraphicsPipeline();

		VulkanRHIDevice* _pDevice;
	};
} // namespace sw
