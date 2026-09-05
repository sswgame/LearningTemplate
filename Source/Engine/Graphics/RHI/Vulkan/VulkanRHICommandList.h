/**
 * @file VulkanRHICommandList.h
 * @brief 소프트웨어 Cmd-vector 기록 없이 즉시 VulkanRHICommandContext를 호출하는 IRHICommandList
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"

namespace sw
{
    class VulkanRHIDevice;

    /**
     * @class VulkanRHICommandList
     * @brief `RHIDeferredCommandList`(CPU `Cmd` 벡터에 기록 후 나중에 replay)를 대체하는 IRHICommandList.
     * @details `VulkanRHICommandContext`는 상태 없이 매 호출마다 `_pDevice->currentCommandBuffer()`로
     *          "지금 활성인 버퍼"(스왑체인 프레임 버퍼 또는 오프스크린 버퍼)를 동적으로 찾아간다 —
     *          DX11/DX12와 달리 이 리스트는 자신만의 네이티브 `VkCommandBuffer`를 새로 소유하지 않고,
     *          그 컨텍스트를 그대로 감싸 호출을 즉시 전달한다. 버퍼의 open/close(`vkBeginCommandBuffer`/
     *          `vkEndCommandBuffer`)와 제출(`vkQueueSubmit`)은 지금처럼 `VulkanRHIDevice::beginFrame`/
     *          `endFrame`과 `beginOffscreenPass`/`endOffscreenPass`가 그대로 소유한다 — 이 클래스가
     *          없애는 건 순수하게 "CPU 쪽 Cmd 구조체 벡터에 쌓았다가 나중에 switch문으로 재생하는"
     *          중간 계층뿐이다.
     */
    class VulkanRHICommandList : public IRHICommandList
    {
    public:
        explicit VulkanRHICommandList( VulkanRHIDevice* pDevice )
            : _context{ pDevice } {}
        ~VulkanRHICommandList() override = default;

        VulkanRHICommandList( const VulkanRHICommandList& )            = delete;
        VulkanRHICommandList& operator=( const VulkanRHICommandList& ) = delete;

        /** @brief 버퍼 open/close는 beginFrame/endFrame/beginOffscreenPass가 소유하므로 아무 것도 하지 않습니다. */
        void beginCommandList() override {}
        void endCommandList() override {}

        void setViewport( const RHIViewport& viewport ) override { _context.setViewport( viewport ); }
        void setPipelineState( RHIPipelineStateHandle pso ) override { _context.setPipelineState( pso ); }
        void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override { _context.beginRenderPass( beginInfo ); }
        void endRenderPass() override { _context.endRenderPass(); }
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override
        {
            _context.setVertexBuffer( slot, buffer, stride, offset );
        }
        void draw( uint32 vertexCount, uint32 startVertex = 0,
                   RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,
                   RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override
        {
            _context.draw( vertexCount, startVertex, passCbDescriptorIndex, materialCbDescriptorIndex );
        }
        void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override
        {
            _context.drawInstanced( vertexCount, instanceCount, startVertex, startInstance );
        }
        void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) override
        {
            _context.setIndexBuffer( buffer, indexStride, offset );
        }
        void setComputePipelineState( RHIPipelineStateHandle pso ) override { _context.setComputePipelineState( pso ); }
        void dispatchCompute( uint32 x, uint32 y, uint32 z ) override { _context.dispatchCompute( x, y, z ); }
        void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                      uint32 destOffsetIn32BitValues = 0 ) override
        {
            _context.setComputeRootConstants( rootParameterIndex, num32BitValues, pData, destOffsetIn32BitValues );
        }
        void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override { _context.bindComputeUAV( index, slot ); }
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override { _context.bindShaderResource( index, slot ); }
        void bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override { _context.bindConstantBuffer( cb, slot ); }
        void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override { _context.bindStructuredBuffer( index, slot ); }
        void bindComputeConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override { _context.bindComputeConstantBuffer( cb, slot ); }
        void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override
        {
            _context.bindComputeShaderResource( index, slot );
        }
        void prepareTextureForShaderRead( RHITextureHandle texture ) override { _context.prepareTextureForShaderRead( texture ); }
        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override { _context.blitTexture( src, dst ); }
        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
                           RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,
                           RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override
        {
            _context.drawIndirect( argumentBuffer, argumentBufferOffset, passCbDescriptorIndex, materialCbDescriptorIndex );
        }
        void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
        {
            _context.dispatchIndirect( argumentBuffer, argumentBufferOffset );
        }
        void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState ) override { _context.transitionBuffer( buffer, newState ); }
        void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
        {
            _context.drawIndexedIndirect( argumentBuffer, argumentBufferOffset );
        }
        void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override
        {
            _context.multiDrawIndirect( argumentBuffer, argumentBufferOffset, maxCommandCount, countBuffer, countBufferOffset );
        }
        void beginEventMarker( const utf8* pName ) override { _context.beginEventMarker( pName ); }
        void endEventMarker() override { _context.endEventMarker(); }

    private:
        VulkanRHICommandContext _context;
    };
} // namespace sw
