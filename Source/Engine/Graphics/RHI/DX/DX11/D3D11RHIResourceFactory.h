#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/Backend/RHIBindlessRegistry.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

struct ID3D11UnorderedAccessView;

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    class D3D11RHIDevice;

    class D3D11RHIResourceFactory : public IRHIResourceFactory
    {
    public:
        explicit D3D11RHIResourceFactory( D3D11RHIDevice* pDevice )
            : _pDevice{ pDevice } {}
        RHIPipelineStateHandle          createPipelineStateNative( const RHIPipelineStateDesc& desc, const RHICompiledGraphicsStages& stages ) override;
        RHIPipelineStateHandle          createComputePipelineStateNative( const RHICompiledShaderStage& computeStage, string_view label ) override;
        [[nodiscard]] bool              destroyPipelineStateNative( RHIPipelineStateHandle pso ) override;
        RHIBufferHandle                 createConstantBuffer( uint32 size ) override;
        RHIBufferHandle                 createConstantBufferPage( uint32 size ) override;
        void                            writeConstantBufferSlice( const RHIBufferSlice& slice, const void* pData, uint32 size ) override;
        RHIBufferHandle                 createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;
        RHIBufferHandle                 createBuffer( const RHIBufferDesc& desc ) override;
        void                            updateStructuredBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource,
                                                                             const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle                 createVertexBuffer( const void* pData, uint32 sizeBytes ) override;
        RHIBufferHandle                 createGPUVertexBuffer( uint32 sizeBytes ) override;
        void                            updateGeometryBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle                 createIndexBuffer( const void* pData, uint32 sizeBytes ) override;
        [[nodiscard]] bool              destroyBufferNative( RHIBufferHandle buffer ) override;
        RHIBufferHandle                 createReadbackBuffer( uint32 elementSize, uint32 elementCount ) override;
        [[nodiscard]] bool              readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination ) override;
        RHITextureHandle                createTexture2D( const RHITextureDesc& desc ) override;
        void                            setTextureDebugName( RHITextureHandle texture, const utf8* pName ) override;
        [[nodiscard]] bool              destroyTextureNative( RHITextureHandle texture ) override;
        [[nodiscard]] RHITransferSubmit submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout ) override;
        [[nodiscard]] RHITransferStatus pollTransfer( RHITransferTicket ticket ) override;
        [[nodiscard]] bool              takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes ) override;
        void                            discardTextureReadback( RHITransferTicket ticket ) override;
        RHIFormat                       getTextureFormat( RHITextureHandle texture ) const override;
        RHITextureSRVIndex              registerBindlessTexture( RHITextureHandle texture ) override;
        [[nodiscard]] bool              rebindBindlessTexture( RHITextureSRVIndex index, RHITextureHandle texture ) override;
        RHITextureSRVIndex              reserveBindlessTextureIndex() override;
        [[nodiscard]] bool              activateReservedBindlessTexture( RHITextureSRVIndex index, RHITextureHandle texture ) override;
        bool                            isTextureReadyToSample( RHITextureHandle texture ) override;
        void                            unregisterBindlessTexture( RHITextureSRVIndex index ) override;
        RHIBufferSRVIndex               registerBindlessBufferSRV( RHIBufferHandle buffer ) override;
        RHIBufferSRVIndex               registerBindlessBufferSRV( const RHIBufferSlice& slice ) override;
        void                            unregisterBindlessResource( RHIBufferSRVIndex index ) override;
        RHIUnorderedAccessIndex         registerBindlessBufferUAV( RHIBufferHandle buffer ) override;
        RHIUnorderedAccessIndex         registerBindlessTextureUAV( RHITextureHandle texture ) override;
        void                            unregisterBindlessUAV( RHIUnorderedAccessIndex index ) override;

    private:
        /** @brief 내용을 채워 바인드 용도 하나(정점 또는 인덱스)의 기본 버퍼를 만듭니다. 정점 버퍼와 인덱스 버퍼가 같은 경로를 씁니다. */
        RHIBufferHandle createFilledBuffer( const void* pData, uint32 sizeBytes, uint32 bindFlags );
        /** @brief UAV 를 레지스트리에 넣고 인덱스를 반환합니다. 반납된 슬롯을 먼저 씁니다. 버퍼 UAV 와 텍스처 UAV 가 같은 경로를 씁니다. @p owner 는 원본 자원입니다(부술 때 이 번호를 놓는다). */
        RHIUnorderedAccessIndex registerUAVView( ID3D11UnorderedAccessView* pUAV, const RHIBindlessOwner& owner );

    private:
        D3D11RHIDevice* _pDevice;
    };
} // namespace sw
#endif
