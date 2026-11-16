#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

namespace sw
{
    class VulkanRHIDevice;

    class VulkanRHIResourceFactory : public IRHIResourceFactory
    {
    public:
        explicit VulkanRHIResourceFactory( VulkanRHIDevice* pDevice )
            : _pDevice{ pDevice } {}
        RHIPipelineStateHandle   createPipelineStateNative( const RHIPipelineStateDesc& desc ) override;
        RHIPipelineStateHandle   createComputePipelineStateNative( string_view shaderPath, string_view entryPoint, const vector<string>& listDefine ) override;
        void                     destroyPipelineState( RHIPipelineStateHandle pso ) override;
        RHIRenderPassHandle      createRenderPass( const RHIRenderPassDesc& desc ) override;
        void                     destroyRenderPass( RHIRenderPassHandle pass ) override;
        RHIBufferHandle          createConstantBuffer( uint32 size ) override;
        void                     updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        RHIBufferHandle          createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;
        void                     updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                                const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle          createVertexBuffer( const void* pData, uint32 sizeBytes ) override;
        void                     updateGeometryBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle          createIndexBuffer( const void* pData, uint32 sizeBytes ) override;
        void                     destroyBuffer( RHIBufferHandle buffer ) override;
        RHIBufferHandle          createReadbackBuffer( uint32 elementSize, uint32 elementCount ) override;
        [[nodiscard]] bool       readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination ) override;
        RHITextureHandle         createTexture2D( const RHITextureDesc& desc ) override;
        RHITextureAllocationInfo getTextureAllocationInfo( const RHITextureDesc& desc ) override;
        RHITransientHeapHandle   createTransientHeap( uint64 sizeBytes ) override;
        void                     destroyTransientHeap( RHITransientHeapHandle heap ) override;
        RHITextureHandle         createPlacedTexture2D( const RHITextureDesc& desc, RHITransientHeapHandle heap, uint64 offsetBytes ) override;
        void                     setTextureDebugName( RHITextureHandle texture, const utf8* pName ) override;
        void                     destroyTexture( RHITextureHandle texture ) override;
        // 전송(RHIAsyncUploads U6): 업로드 · 되읽기 링에 놓고 전송 작업(커맨드 버퍼 + 펜스)으로 내며 기다리지 않는다. 완료는 pollTransfer 가 펜스 줄의 연속 구간 워터마크로 본다.
        [[nodiscard]] RHITransferSubmit submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout ) override;
        [[nodiscard]] RHITransferStatus pollTransfer( RHITransferTicket ticket ) override;
        [[nodiscard]] bool              takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes ) override;
        void                            discardTextureReadback( RHITransferTicket ticket ) override;
        RHIFormat                       getTextureFormat( RHITextureHandle texture ) const override;
        RHIDescriptorIndex              registerBindlessTexture( RHITextureHandle texture ) override;
        bool                            isTextureReadyToSample( RHITextureHandle texture ) override;
        void                            unregisterBindlessTexture( RHIDescriptorIndex index ) override;
        RHIDescriptorIndex              registerBindlessBufferSRV( RHIBufferHandle buffer ) override;
        void                            unregisterBindlessResource( RHIDescriptorIndex index ) override;
        RHIDescriptorIndex              registerBindlessBufferUAV( RHIBufferHandle buffer ) override;
        RHIDescriptorIndex              registerBindlessTextureUAV( RHITextureHandle texture ) override;
        void                            unregisterBindlessUAV( RHIDescriptorIndex index ) override;

    private:
        /** @brief @p desc 의 이미지 생성 서술을 채웁니다. 형태 · 형식 · 깊이 형식이 맞지 않으면 이유를 남기고 false 입니다(`createTexture2D` 와 할당 크기 묻기가 같은 서술을 쓴다). */
        [[nodiscard]] bool buildImageCreateInfo( const RHITextureDesc& desc, VkImageCreateInfo& outInfo );
        /**
         * @brief 이미지 · 메모리 · 뷰 · 레코드를 만듭니다. @p pHeap 이 있으면 그 힙 메모리의 @p offsetBytes 에 묶고(placed — 레코드가 힙 참조를 든다), 없으면 자기 메모리를 잡는다.
         * @details `createTexture2D` 와 `createPlacedTexture2D` 가 같은 몸을 쓴다(DX12 `createTextureResource` 와 같다) — 서술 · 뷰 · 레이아웃 규칙이 두 벌로 갈리지 않게.
         */
        RHITextureHandle createTextureResource( const RHITextureDesc& desc, const shared_ptr<VulkanRHIDevice::VulkanHeapMemory>& pHeap, uint64 offsetBytes );
        /** @brief 등록표에서 비운 텍스처 번호를 GPU 펜스 뒤에 더미로 되돌리고 빈 목록에 넣습니다(`_bindlessMutex` 를 쥔 채로 부릅니다). */
        void scheduleTextureBindlessIndexFree( RHIDescriptorIndex index );
        /** @brief 버퍼 인덱스(SRV/CB 또는 UAV)를 GPU 펜스 뒤에 프리리스트로 돌려보냅니다. 부르기 전에 원본 테이블은 비워 둡니다. */
        void deferFreeBufferIndex( RHIDescriptorIndex index, bool bUAV );
        /** @brief 현재 프레임 슬롯의 스테이징에서 sizeBytes 를 bump 할당합니다(부족하면 키우고 이전 버퍼는 펜스 뒤 해제). */
        bool acquireStructuredUploadStaging( uint64 sizeBytes, uint64& outOffset, VkBuffer& outBuffer );

    private:
        VulkanRHIDevice* _pDevice;
    };
} // namespace sw
