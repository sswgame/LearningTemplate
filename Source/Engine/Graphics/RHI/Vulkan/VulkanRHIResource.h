#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

namespace sw
{
    class VulkanRHIDevice;

    class VulkanRHIResource : public IRHIResource
    {
    public:
        explicit VulkanRHIResource( VulkanRHIDevice* pDevice )
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
        /** @brief 텍스처 레코드가 쥔 bindless 슬롯을 반납하고 레코드의 인덱스를 지웁니다(destroyTexture/unregisterBindlessTexture 공용). */
        void releaseTextureBindlessSlot( VulkanRHIDevice::VulkanTextureRecord& record );
        /** @brief 현재 프레임 슬롯의 스테이징에서 sizeBytes 를 bump 할당합니다(부족하면 키우고 옛 버퍼는 펜스 뒤 해제). */
        bool acquireStructuredUploadStaging( uint64 sizeBytes, uint64& outOffset, VkBuffer& outBuffer );

        VulkanRHIDevice* _pDevice;
    };
} // namespace sw
