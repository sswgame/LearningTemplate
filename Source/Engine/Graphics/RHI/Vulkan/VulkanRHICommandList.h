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
     * @details 이 리스트는 **자기 커맨드 풀 + 커맨드 버퍼 + 기록 상태**를 소유한다. `VkCommandPool` 은
     *          외부 동기화 대상이라, 여러 리스트가 서로 다른 스레드에서 동시에 기록하려면 풀이
     *          리스트마다 따로여야 한다(DX12 의 얼로케이터와 같은 제약). 쌍은 디바이스 풀에서 빌리고
     *          다 쓰면 GPU 펜스 통과 후 돌려준다.
     *          `endCommandList` 로 버퍼를 닫고 `IRHIDevice::executeCommandList` 로 넘기면, 디바이스가
     *          프레임 스트림을 그 지점에서 잘라 [지금까지의 세그먼트][이 리스트][새 세그먼트] 순서로
     *          잇고 `endFrame` 에서 한 번에 제출한다 — 같은 큐의 제출 순서가 곧 실행 순서다.
     */
    class VulkanRHICommandList : public IRHICommandList
    {
    public:
        /** @brief 디바이스 풀에서 커맨드 풀 + 버퍼 쌍을 빌립니다. */
        explicit VulkanRHICommandList( VulkanRHIDevice* pDevice );
        /** @brief 빌린 쌍을 GPU 펜스 통과 후 풀로 돌려보냅니다. */
        ~VulkanRHICommandList() override;

        VulkanRHICommandList( const VulkanRHICommandList& )            = delete;
        VulkanRHICommandList& operator=( const VulkanRHICommandList& ) = delete;

        /** @brief `IRHIDevice::executeCommandList` 가 프레임 스트림에 끼워 넣는 네이티브 버퍼. */
        VkCommandBuffer nativeCommandBuffer() const { return _entry._buffer; }

        void beginCommandList() override;
        void endCommandList() override;

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
        VulkanRHIDevice*        _pDevice{ nullptr };
        VulkanCommandListEntry  _entry{};          ///< 이 리스트 전용 커맨드 풀 + 커맨드 버퍼
        uint8                   _bEntryDirty{ 0 }; ///< 현재 쌍에 이미 기록했는가(있으면 다음 begin 때 교체)
        VulkanRecordingState    _state{};
        VulkanRHICommandContext _context;
    };
} // namespace sw
