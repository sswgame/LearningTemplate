/**
 * @file WebGPURHIResourceFactory.h
 * @brief WebGPU 자원(버퍼 · 텍스처 · 파이프라인)과 전송 · bindless 등록입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

#include <webgpu/webgpu.h>

namespace sw
{
    class WebGPURHIDevice;

    /**
     * @class WebGPURHIResourceFactory
     * @brief `IRHIResourceFactory` 의 WebGPU 구현입니다. 장치를 만든 스레드에서만 부른다(`_bThreadSafeResourceCreation` 거짓 — `GPUUploadQueue` 가 렌더 스레드에서 만든다).
     */
    class WebGPURHIResourceFactory final : public IRHIResourceFactory
    {
    public:
        explicit WebGPURHIResourceFactory( WebGPURHIDevice* pDevice );

        RHIPipelineStateHandle createPipelineStateNative( const RHIPipelineStateDesc& desc ) override;
        RHIPipelineStateHandle createComputePipelineStateNative( string_view shaderPath, string_view entryPoint, const vector<string>& listDefine ) override;
        void                   destroyPipelineState( RHIPipelineStateHandle pso ) override;
        RHIRenderPassHandle    createRenderPass( const RHIRenderPassDesc& desc ) override;
        void                   destroyRenderPass( RHIRenderPassHandle pass ) override;

        RHIBufferHandle               createConstantBuffer( uint32 size ) override;
        void                          updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        RHIBufferHandle               createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;
        void                          updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle               createVertexBuffer( const void* pData, uint32 sizeBytes ) override;
        void                          updateGeometryBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle               createIndexBuffer( const void* pData, uint32 sizeBytes ) override;
        void                          destroyBuffer( RHIBufferHandle buffer ) override;
        [[nodiscard]] RHIBufferHandle createReadbackBuffer( uint32 elementSize, uint32 elementCount ) override;
        [[nodiscard]] bool            readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination ) override;

        RHITextureHandle createTexture2D( const RHITextureDesc& desc ) override;
        void             destroyTexture( RHITextureHandle texture ) override;
        RHIFormat        getTextureFormat( RHITextureHandle texture ) const override;

        [[nodiscard]] RHITransferSubmit submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout ) override;
        [[nodiscard]] RHITransferStatus pollTransfer( RHITransferTicket ticket ) override;
        [[nodiscard]] bool              takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes ) override;
        void                            discardTextureReadback( RHITransferTicket ticket ) override;

        RHIDescriptorIndex registerBindlessTexture( RHITextureHandle texture ) override;
        [[nodiscard]] bool isTextureReadyToSample( RHITextureHandle texture ) override;
        void               unregisterBindlessTexture( RHIDescriptorIndex index ) override;
        RHIDescriptorIndex registerBindlessBufferSRV( RHIBufferHandle buffer ) override;
        void               unregisterBindlessResource( RHIDescriptorIndex index ) override;
        RHIDescriptorIndex registerBindlessBufferUAV( RHIBufferHandle buffer ) override;
        RHIDescriptorIndex registerBindlessTextureUAV( RHITextureHandle texture ) override;
        void               unregisterBindlessUAV( RHIDescriptorIndex index ) override;

    private:
        /** @brief 용도 @p usage 의 버퍼를 만들고 @p pData 가 있으면 담습니다(크기는 4 의 배수로 올린다 — 명세의 쓰기 정렬). */
        RHIBufferHandle createBufferWithUsage( uint64 sizeBytes, uint32 elementSize, WGPUBufferUsage usage, const void* pData, const utf8* pLabel );
        /** @brief 버퍼 구간들을 큐 쓰기로 올립니다. 구간은 4 바이트 정렬로 넓힌다 — 앞뒤의 넓힌 바이트는 원본 블롭에서 읽는다(원본 밖이면 0). */
        void writeBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount );
        /** @brief 셰이더 꾸러미로 셰이더 모듈을 만들고 그 WGSL 의 바인딩 번호를 @p ioListBinding 에 더합니다. 꾸러미가 아니면 오류를 남기고 nullptr 입니다. */
        WGPUShaderModule createShaderModule( const vector<uint8>& bytecode, const utf8* pLabel, vector<uint32>& ioListBinding );
        /** @brief 전송 하나의 완료 표지를 큐에 넣고 시리얼을 받습니다. 표지는 지금까지 큐에 낸 일이 끝나면 켜진다(`wgpuQueueOnSubmittedWorkDone`). */
        [[nodiscard]] uint64 enqueueQueueFence();
        /** @brief 읽기 버퍼 매핑이 끝나면 부르는 콜백입니다. userdata1 은 전송 표지(장치 · 시리얼을 든다)입니다. 패딩을 벗긴 바이트를 결과에 담는다. */
        static void onReadbackMapped( WGPUMapAsyncStatus status, WGPUStringView message, void* pUserdata1, void* pUserdata2 );

    private:
        WebGPURHIDevice* _pDevice;
    };
} // namespace sw
