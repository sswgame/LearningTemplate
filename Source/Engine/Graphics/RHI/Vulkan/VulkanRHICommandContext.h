#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

namespace sw
{

    class VulkanRHICommandContext : public IRHICommandContext
    {
    public:
        /**
         * @brief 디바이스의 기록 상태/버퍼를 쓰는 즉시 컨텍스트를 만듭니다.
         * @details 스왑체인 begin/end 처럼 "디바이스가 직접 여는 버퍼"에 기록하는 경로용입니다.
         */
        explicit VulkanRHICommandContext( VulkanRHIDevice* pDevice );
        /**
         * @brief 지정한 커맨드 버퍼와 기록 상태에 기록하는 컨텍스트를 만듭니다.
         * @details `VulkanRHICommandList` 처럼 자기 버퍼/상태를 소유하는 쪽이 씁니다 — 여러 리스트가
         *          동시에 기록해도 서로의 바인딩 캐시를 건드리지 않습니다.
         */
        VulkanRHICommandContext( VulkanRHIDevice* pDevice, VkCommandBuffer targetBuffer, VulkanRecordingState* pState,
                                 VulkanDescriptorPoolSet* pDescriptorPoolSet );
        ~VulkanRHICommandContext() override = default;

        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderRead( RHITextureHandle texture ) override;
        void prepareTextureForRenderTarget( RHITextureHandle texture ) override;
        void prepareTextureForUnorderedAccess( RHITextureHandle texture ) override;
        void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
        void draw( uint32 vertexCount, uint32 startVertex = 0 ) override;
        void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override;
        void bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override;
        void bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
        void setViewport( const RHIViewport& viewport ) override;
        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1,
                           RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override;
        void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
        void setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
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
        void uavBarrier( RHIBufferHandle buffer ) override;

    private:
        /**
         * @brief 바뀐 슬롯 상태를 슬롯 세트(set 0)로 굳혀 두 바인드 포인트에 걸고, 텍스처 세트(set 1)는 커맨드버퍼마다 한 번 겁니다.
         * @details 언리얼 Vulkan RHI 처럼 드로우/디스패치 직전에 부른다. 상태가 그대로면 아무것도 안 한다.
         */
        void flushSlotSet( bool bCompute );
        /**
         * @brief 슬롯 하나에 bindless 인덱스의 버퍼를 겁니다 (세트는 flushSlotSet 이 굳힌다).
         * @param bCompute true 면 컴퓨트 바인드 포인트의 상태, false 면 그래픽스 — 둘은 독립이다.
         * @param bindingIndex 세트 0 의 binding (종류별 시프트 + 레지스터 번호).
         * @param bUav true 면 UAV 레지스트리, false 면 SRV/CB 레지스트리.
         * @param bConstantBuffer true 면 링 상수버퍼의 이번 프레임 슬롯 오프셋/크기를 쓴다.
         */
        void setSlot( bool bCompute, uint32 bindingIndex, RHIDescriptorIndex index, bool bUav, bool bConstantBuffer );

        /**
         * @brief 현재 활성 그래픽스 PSO(없으면 오프스크린/기본 파이프라인)를 바인딩합니다.
         * @details 모든 draw 계열이 직전 상태에 의존하지 않고 스스로 파이프라인을 세우도록 합니다.
         * @return 바인딩할 파이프라인이 있으면 true, 없으면 false(드로우를 건너뛰어야 함).
         */
        bool bindActiveGraphicsPipeline();

        /** @brief 현재 바인딩된 메시 VB(없으면 풀스크린 정점버퍼)를 슬롯 0에 건다. draw류 3곳 복붙 통합. */
        void bindMeshVertexBufferOrFallback();

        VulkanRHIDevice* _pDevice;
        /// @brief 이 컨텍스트가 기록할 버퍼. nullptr 이면 디바이스가 지금 연 버퍼를 따라간다.
        VkCommandBuffer _targetBuffer{ nullptr };
        /// @brief 이 컨텍스트가 갱신할 기록 상태. 리스트가 자기 것을 넘기면 서로 간섭하지 않는다.
        VulkanRecordingState* _pState{ nullptr };
        /// @brief 슬롯 세트를 할당받는 풀 묶음. nullptr 이면 디바이스 프레임 스트림의 이번 링 슬롯 묶음을 쓴다.
        VulkanDescriptorPoolSet* _pDescriptorPoolSet{ nullptr };

        /** @brief 실제로 기록할 커맨드 버퍼입니다(지정된 게 있으면 그것, 없으면 디바이스의 현재 버퍼). */
        VkCommandBuffer commandBuffer() const;

    public:
        /** @brief 기록 대상 버퍼와 풀 묶음을 교체합니다(소유자가 버퍼+풀 쌍을 바꿔 낄 때). */
        void rebindCommandBuffer( VkCommandBuffer targetBuffer, VulkanDescriptorPoolSet* pDescriptorPoolSet )
        {
            _targetBuffer       = targetBuffer;
            _pDescriptorPoolSet = pDescriptorPoolSet;
        }
    };
} // namespace sw
