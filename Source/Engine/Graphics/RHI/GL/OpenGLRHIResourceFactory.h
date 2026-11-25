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
        RHIBufferHandle        createConstantBuffer( uint32 size ) override;
        /** @brief 기록 경로의 상수 버퍼 쓰기입니다(커맨드 컨텍스트만 부른다 — 두 번째 쓰기 진단은 `IRHICommandContext::updateConstantBuffer` 가 했다). */
        void                            writeConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size );
        RHIBufferHandle                 createConstantBufferPage( uint32 size ) override;
        void                            writeConstantBufferSlice( const RHIBufferSlice& slice, const void* pData, uint32 size ) override;
        RHIBufferHandle                 createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;
        void                            updateStructuredBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource,
                                                                             const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle                 createBuffer( const RHIBufferDesc& desc ) override;
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
        [[nodiscard]] RHITransferSubmit submitTextureUploadNative( RHITextureHandle texture, const RHITextureUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureRegionUploadNative( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureReadbackNative( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout ) override;
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
        /** @brief 프로그램을 링크하고, 실패하면 로그와 함께 지웁니다. 그래픽스와 컴퓨트가 같은 경로를 씁니다. */
        static bool linkProgram( uint32 program, const utf8* pLabel );

        OpenGLRHIDevice* _pDevice;
    };
} // namespace sw
