#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    class OpenGLRHIDevice;

    class OpenGLRHIResourceFactory : public IRHIResourceFactory
    {
    public:
        explicit OpenGLRHIResourceFactory( OpenGLRHIDevice* pDevice )
            : _pDevice{ pDevice } {}
        RHIPipelineStateHandle createPipelineStateNative( const RHIPipelineStateDesc& desc, const RHICompiledGraphicsStages& stages ) override;
        RHIPipelineStateHandle createComputePipelineStateNative( const RHICompiledShaderStage& computeStage, string_view label ) override;
        [[nodiscard]] bool     destroyPipelineStateNative( RHIPipelineStateHandle pso ) override;
        RHIBufferHandle        createConstantBufferNative( uint32 size ) override;
        /** @brief 기록 경로의 상수 버퍼 쓰기입니다(커맨드 컨텍스트만 부른다 — 두 번째 쓰기 진단은 `IRHICommandContext::updateConstantBuffer` 가 했다). */
        void                            writeConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size );
        RHIBufferHandle                 createConstantBufferPageNative( uint32 size ) override;
        void                            writeConstantBufferSliceNative( const RHIBufferSlice& slice, const void* pData, uint32 size ) override;
        RHIBufferHandle                 createStructuredBufferNative( uint32 elementSize, uint32 elementCount ) override;
        void                            updateStructuredBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource,
                                                                             const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle                 createBufferNative( const RHIBufferDesc& desc ) override;
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
        /** @brief 프로그램을 링크하고, 실패하면 로그와 함께 지웁니다. 그래픽스와 컴퓨트가 같은 경로를 씁니다. */
        static bool linkProgram( uint32 program, const utf8* pLabel );

        OpenGLRHIDevice* _pDevice;
    };
} // namespace sw
