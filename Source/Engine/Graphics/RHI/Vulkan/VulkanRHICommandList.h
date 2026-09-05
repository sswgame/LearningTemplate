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
     * @details 이 리스트는 **자기 primary `VkCommandBuffer` 와 전용 커맨드 풀**을 디바이스 풀에서
     *          빌려 소유하고, 자기 `VulkanRecordingState` 를 갖는다. 그래서 여러 리스트가 서로 다른
     *          스레드에서 동시에 기록해도 버퍼도 바인딩 캐시도 겹치지 않는다.
     *          `vkCmdBeginRenderPass` 는 primary 에서만 가능하므로(VUID-vkCmdBeginRenderPass-bufferlevel)
     *          secondary + `vkCmdExecuteCommands` 로는 패스마다 자기 렌더패스를 열 수 없다. 그래서
     *          `VulkanRHIDevice::executeCommandList` 는 프레임 커맨드 버퍼를 세그먼트로 끊고 이 리스트의
     *          버퍼를 그 사이에 끼워, 프레임당 단일 `vkQueueSubmit` 에 순서대로 함께 제출한다.
     */
    class VulkanRHICommandList : public IRHICommandList
    {
    public:
        /** @brief 디바이스 풀에서 secondary 버퍼 + 전용 커맨드 풀을 빌립니다. */
        explicit VulkanRHICommandList( VulkanRHIDevice* pDevice );
        /** @brief 빌린 엔트리를 GPU 펜스 통과 후 풀로 돌려보냅니다. */
        ~VulkanRHICommandList() override;

        VulkanRHICommandList( const VulkanRHICommandList& )            = delete;
        VulkanRHICommandList& operator=( const VulkanRHICommandList& ) = delete;

        /** @brief 자기 커맨드 풀을 리셋하고 secondary 버퍼 기록을 시작합니다. */
        void beginCommandList() override;
        /** @brief secondary 버퍼 기록을 끝냅니다. */
        void endCommandList() override;

        /** @brief 이 리스트가 유효한 버퍼를 확보했으면 true. */
        bool isValid() const { return _entry._buffer != nullptr; }
        /** @brief `VulkanRHIDevice::executeCommandList` 가 vkCmdExecuteCommands 에 쓰는 네이티브 버퍼. */
        VkCommandBuffer getNativeCommandBuffer() const { return _entry._buffer; }

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
        VulkanRHIDevice*        _pDevice;
        VulkanCommandListEntry  _entry; ///< 이 리스트 전용 primary 버퍼 + 커맨드 풀
        VulkanRecordingState    _state; ///< 이 리스트 전용 기록 상태
        VulkanRHICommandContext _context;
    };
} // namespace sw
