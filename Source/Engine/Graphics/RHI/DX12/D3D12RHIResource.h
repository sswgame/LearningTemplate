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
        void                   destroyPipelineState( RHIPipelineStateHandle pso ) override;
        RHIRenderPassHandle    createRenderPass( const RHIRenderPassDesc& desc ) override;
        void                   destroyRenderPass( RHIRenderPassHandle pass ) override;
        RHIBufferHandle        createConstantBuffer( uint32 size ) override;
        void                   updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        RHIBufferHandle        createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;
        void                   updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        RHIBufferHandle        createVertexBuffer( const void* pData, uint32 sizeBytes ) override;
        void                   destroyBuffer( RHIBufferHandle buffer ) override;
        RHITextureHandle       createTexture2D( const RHITextureDesc& desc ) override;
        void                   destroyTexture( RHITextureHandle texture ) override;
        bool                   uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc ) override;
        bool                   readbackTexture2D( RHITextureHandle texture, uint32 mip, vector<uint8>& outBytes, RHITextureMipSpan& outLayout ) override;
        RHIDescriptorIndex     registerBindlessTexture( RHITextureHandle texture ) override;
        void                   unregisterBindlessTexture( RHIDescriptorIndex index ) override;
        RHIDescriptorIndex     registerBindlessResource( RHIBufferHandle buffer ) override;
        void                   unregisterBindlessResource( RHIDescriptorIndex index ) override;
        RHIDescriptorIndex     registerBindlessUAV( RHIBufferHandle buffer ) override;
        void                   unregisterBindlessUAV( RHIDescriptorIndex index ) override;

    private:
        /** @brief 텍스처/버퍼 공용 힙 슬롯을 비우고 프리리스트에 돌려줍니다(빈 슬롯은 무시). */
        void releaseBindlessSlot( RHIDescriptorIndex index );

        /**
         * @brief 현재 프레임 링 슬롯의 스테이징 힙에서 sizeBytes 를 bump 할당하고 복사 리스트를 열어 둡니다.
         * @details 얼로케이터는 펜스 구간이 바뀔 때만 Reset 한다 — 같은 구간의 앞선 복사가 GPU 에서 도는 중일 수
         *          있다. 성공하면 `_pDevice->_arrStructuredUploadSlot[outSlotIndex]._copyCommandList` 에 기록하고
         *          submitUploadSlot 으로 닫는다. updateStructuredBuffer / uploadTexture2D 공용.
         */
        bool acquireUploadStaging( uint64 sizeBytes, uint64 alignment, uint32& outSlotIndex, uint64& outOffset, void*& pOutMapped );
        /** @brief 현재 프레임 링 슬롯의 복사 리스트만 엽니다(스테이징 없이 — readback 처럼 소스가 다른 곳일 때). */
        bool openUploadSlot( uint32& outSlotIndex );
        /** @brief acquireUploadStaging / openUploadSlot 으로 연 복사 리스트를 닫고 그래픽스 큐에 제출합니다. */
        void submitUploadSlot( uint32 slotIndex );
        /** @brief 지금까지 큐에 넣은 작업이 끝날 때까지 CPU 를 세웁니다(readback 전용 — 프레임 경로에서 부르지 말 것). */
        bool waitForQueueDrain();

    public:
    private:
        D3D12RHIDevice* _pDevice;
    };
} // namespace sw
#endif
