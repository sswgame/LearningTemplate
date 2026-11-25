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
        RHIPipelineStateHandle createPipelineStateNative( const RHIPipelineStateDesc& desc, const RHICompiledGraphicsStages& stages ) override;
        RHIPipelineStateHandle createComputePipelineStateNative( const RHICompiledShaderStage& computeStage, string_view label ) override;
        [[nodiscard]] bool     destroyPipelineStateNative( RHIPipelineStateHandle pso ) override;
        RHIBufferHandle        createConstantBufferNative( uint32 size ) override;
        /** @brief 기록 경로의 상수 버퍼 쓰기입니다(커맨드 컨텍스트만 부른다 — 두 번째 쓰기 진단은 `IRHICommandContext::updateConstantBuffer` 가 했다). */
        void            writeConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size );
        RHIBufferHandle createConstantBufferPageNative( uint32 size ) override;
        void            writeConstantBufferSliceNative( const RHIBufferSlice& slice, const void* pData, uint32 size ) override;
        RHIBufferHandle createStructuredBufferNative( uint32 elementSize, uint32 elementCount ) override;
        void            updateStructuredBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource,
                                                             const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle createVertexBufferNative( const void* pData, uint32 sizeBytes ) override;
        /** @brief 기기 로컬 버퍼 @p handle 에 첫 내용 @p pData 를 스테이징 복사로 올립니다(기다리지 않는다 — 렌더 스레드가 그 버퍼를 읽는 리스트보다 앞에 기록한다). 실패하면 버퍼를 놓고 0 입니다. */
        RHIBufferHandle          uploadDeviceLocal( RHIBufferHandle handle, const void* pData, uint32 sizeBytes );
        RHIBufferHandle          createGPUVertexBufferNative( uint32 sizeBytes ) override;
        void                     updateGeometryBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle          createIndexBufferNative( const void* pData, uint32 sizeBytes ) override;
        [[nodiscard]] bool       destroyBufferNative( RHIBufferHandle buffer ) override;
        RHIBufferHandle          createReadbackBufferNative( uint32 elementSize, uint32 elementCount ) override;
        [[nodiscard]] bool       readReadbackBufferNative( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination ) override;
        RHITextureHandle         createTexture2DNative( const RHITextureDesc& desc ) override;
        RHITextureAllocationInfo getTextureAllocationInfoNative( const RHITextureDesc& desc ) override;
        RHITransientHeapHandle   createTransientHeapNative( uint64 sizeBytes ) override;
        [[nodiscard]] bool       destroyTransientHeapNative( RHITransientHeapHandle heap ) override;
        RHITextureHandle         createPlacedTexture2DNative( const RHITextureDesc& desc, RHITransientHeapHandle heap, uint64 offsetBytes ) override;
        void                     setTextureDebugNameNative( RHITextureHandle texture, const utf8* pName ) override;
        [[nodiscard]] bool       destroyTextureNative( RHITextureHandle texture ) override;
        // 전송(RHIAsyncUploads U6): 업로드 · 되읽기 링에 놓고 전송 작업(커맨드 버퍼 + 펜스)으로 내며 기다리지 않는다. 완료는 pollTransfer 가 펜스 줄의 연속 구간 워터마크로 본다.
        [[nodiscard]] RHITransferSubmit submitTextureUploadNative( RHITextureHandle texture, const RHITextureUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureRegionUploadNative( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureReadbackNative( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout ) override;
        [[nodiscard]] RHITransferStatus pollTransfer( RHITransferTicket ticket ) override;
        [[nodiscard]] bool              takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes ) override;
        void                            discardTextureReadback( RHITransferTicket ticket ) override;
        RHIFormat                       getTextureFormat( RHITextureHandle texture ) const override;
        RHITextureSRVIndex              registerBindlessTextureNative( RHITextureHandle texture ) override;
        [[nodiscard]] bool              rebindBindlessTextureNative( RHITextureSRVIndex index, RHITextureHandle texture ) override;
        RHITextureSRVIndex              reserveBindlessTextureIndexNative() override;
        [[nodiscard]] bool              activateReservedBindlessTextureNative( RHITextureSRVIndex index, RHITextureHandle texture ) override;
        [[nodiscard]] bool              describeTextureNative( RHITextureHandle texture, RHITextureBindingInfo& outInfo ) override;
        [[nodiscard]] uint32            findConstantPageBytesNative( RHIBufferHandle buffer ) override;
        [[nodiscard]] bool              unregisterBindlessTextureNative( RHITextureSRVIndex index ) override;
        RHIBufferSRVIndex               registerBindlessBufferSRVNative( RHIBufferHandle buffer ) override;
        RHIBufferSRVIndex               registerBindlessBufferSRVNative( const RHIBufferSlice& slice ) override;
        [[nodiscard]] bool              unregisterBindlessResourceNative( RHIBufferSRVIndex index ) override;
        RHIUnorderedAccessIndex         registerBindlessBufferUAVNative( RHIBufferHandle buffer ) override;
        RHIUnorderedAccessIndex         registerBindlessTextureUAVNative( RHITextureHandle texture ) override;
        [[nodiscard]] bool              unregisterBindlessUAVNative( RHIUnorderedAccessIndex index ) override;

    private:
        /** @brief @p desc 의 이미지 생성 서술을 채웁니다. 형태 · 형식 · 깊이 형식이 맞지 않으면 이유를 남기고 false 입니다(`createTexture2D` 와 할당 크기 묻기가 같은 서술을 쓴다). */
        [[nodiscard]] bool populateImageCreateInfo( const RHITextureDesc& desc, VkImageCreateInfo& outInfo );
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
