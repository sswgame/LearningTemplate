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
        RHIBufferHandle                 createConstantBufferNative( uint32 size ) override;
        RHIBufferHandle                 createConstantBufferPageNative( uint32 size ) override;
        void                            writeConstantBufferSliceNative( const RHIBufferSlice& slice, const void* pData, uint32 size ) override;
        RHIBufferHandle                 createStructuredBufferNative( uint32 elementSize, uint32 elementCount ) override;
        RHIBufferHandle                 createBufferNative( const RHIBufferDesc& desc ) override;
        void                            updateStructuredBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource,
                                                                             const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle                 createVertexBufferNative( const void* pData, uint32 sizeBytes ) override;
        RHIBufferHandle                 createGPUVertexBufferNative( uint32 sizeBytes ) override;
        void                            updateGeometryBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle                 createIndexBufferNative( const void* pData, uint32 sizeBytes ) override;
        [[nodiscard]] bool              destroyBufferNative( RHIBufferHandle buffer ) override;
        RHIBufferHandle                 createReadbackBufferNative( uint32 elementSize, uint32 elementCount ) override;
        [[nodiscard]] bool              readReadbackBufferNative( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination ) override;
        RHITextureHandle                createTexture2DNative( const RHITextureDesc& desc ) override;
        void                            setTextureDebugNameNative( RHITextureHandle texture, const utf8* pName ) override;
        [[nodiscard]] bool              destroyTextureNative( RHITextureHandle texture ) override;
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
        /** @brief 내용을 채워 바인드 용도 하나(정점 또는 인덱스)의 기본 버퍼를 만듭니다. 정점 버퍼와 인덱스 버퍼가 같은 경로를 씁니다. */
        RHIBufferHandle createFilledBuffer( const void* pData, uint32 sizeBytes, uint32 bindFlags );
        /** @brief UAV 를 레지스트리에 넣고 인덱스를 반환합니다. 반납된 슬롯을 먼저 씁니다. 버퍼 UAV 와 텍스처 UAV 가 같은 경로를 씁니다. @p owner 는 원본 자원입니다(부술 때 이 번호를 놓는다). */
        RHIUnorderedAccessIndex registerUAVView( ID3D11UnorderedAccessView* pUAV, const RHIBindlessOwner& owner );

    private:
        D3D11RHIDevice* _pDevice;
    };
} // namespace sw
#endif
