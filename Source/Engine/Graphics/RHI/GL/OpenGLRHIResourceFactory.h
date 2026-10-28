#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    class OpenGLRHIDevice;

    class OpenGLRHIResourceFactory : public IRHIResourceFactory
    {
    public:
        explicit OpenGLRHIResourceFactory( OpenGLRHIDevice* pDevice )
            : _pDevice{ pDevice } {}
        RHIPipelineStateHandle          createPipelineState( const RHIPipelineStateDesc& desc ) override;
        RHIPipelineStateHandle          createComputePipelineState( string_view shaderPath, string_view entryPoint = {}, const vector<string>& listDefine = {} ) override;
        void                            destroyPipelineState( RHIPipelineStateHandle pso ) override;
        RHIRenderPassHandle             createRenderPass( const RHIRenderPassDesc& desc ) override;
        void                            destroyRenderPass( RHIRenderPassHandle pass ) override;
        RHIBufferHandle                 createConstantBuffer( uint32 size ) override;
        void                            updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        RHIBufferHandle                 createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;
        void                            updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                                       const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle                 createBuffer( const RHIBufferDesc& desc ) override;
        RHIBufferHandle                 createVertexBuffer( const void* pData, uint32 sizeBytes ) override;
        void                            updateVertexBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle                 createIndexBuffer( const void* pData, uint32 sizeBytes ) override;
        void                            destroyBuffer( RHIBufferHandle buffer ) override;
        RHIBufferHandle                 createReadbackBuffer( uint32 elementSize, uint32 elementCount ) override;
        [[nodiscard]] bool              readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination ) override;
        RHITextureHandle                createTexture2D( const RHITextureDesc& desc ) override;
        void                            destroyTexture( RHITextureHandle texture ) override;
        [[nodiscard]] RHITransferSubmit submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout ) override;
        [[nodiscard]] RHITransferStatus pollTransfer( RHITransferTicket ticket ) override;
        [[nodiscard]] bool              takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes ) override;
        void                            discardTextureReadback( RHITransferTicket ticket ) override;
        RHIFormat                       getTextureFormat( RHITextureHandle texture ) const override;
        RHIDescriptorIndex              registerBindlessTexture( RHITextureHandle texture ) override;
        void                            unregisterBindlessTexture( RHIDescriptorIndex index ) override;
        RHIDescriptorIndex              registerBindlessBufferSRV( RHIBufferHandle buffer ) override;
        void                            unregisterBindlessResource( RHIDescriptorIndex index ) override;
        RHIDescriptorIndex              registerBindlessBufferUAV( RHIBufferHandle buffer ) override;
        RHIDescriptorIndex              registerBindlessTextureUAV( RHITextureHandle texture ) override;
        void                            unregisterBindlessUAV( RHIDescriptorIndex index ) override;

    private:
        /** @brief 프로그램을 링크하고, 실패하면 로그와 함께 지웁니다. 그래픽스와 컴퓨트가 같은 경로를 씁니다. */
        static bool linkProgram( uint32 program, const utf8* pLabel );

        OpenGLRHIDevice* _pDevice;
    };
} // namespace sw
