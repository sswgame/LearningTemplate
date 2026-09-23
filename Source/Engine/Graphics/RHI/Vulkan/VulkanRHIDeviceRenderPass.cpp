/**
 * @file VulkanRHIDeviceRenderPass.cpp
 * @brief VulkanRHIDevice 의 렌더패스 · 프레임버퍼 캐시와 이미지 레이아웃 전이입니다.
 * @details Vulkan 은 렌더패스와 프레임버퍼를 명시적으로 만들어야 해서, PSO 가 요구하는 조합마다
 *          캐시가 필요합니다. 이 캐시들이 파이프라인이 선언한 포맷과 어긋나면 GPU 가 죽습니다.
 */
#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

namespace sw
{
    namespace
    {
        VkAttachmentLoadOp toVkLoadOp( RHIRenderPassLoadOp loadOp )
        {
            switch ( loadOp )
            {
                case RHIRenderPassLoadOp::Load:
                    return VK_ATTACHMENT_LOAD_OP_LOAD;
                case RHIRenderPassLoadOp::DontCare:
                    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                case RHIRenderPassLoadOp::Clear:
                default:
                    return VK_ATTACHMENT_LOAD_OP_CLEAR;
            }
        }
    } // namespace

    void VulkanRHIDevice::transitionTextureLayout( VkCommandBuffer cmd, VulkanTextureRecord& record,
                                                   uint32 targetLayout, uint32 aspect )
    {
        if ( cmd == VK_NULL_HANDLE || record._image == VK_NULL_HANDLE )
            return;

        std::scoped_lock<mutex> lock{ _imageLayoutMutex };
        if ( record._layout == targetLayout )
            return;

        noteBarrierDuringRecording( "transitionTextureLayout" );

        transitionImageLayout( cmd, record._image, record._layout, targetLayout, aspect );
        record._layout = targetLayout;
    }

    SW_LOG_CALLER( "Vulkan" );

    bool VulkanRHIDevice::transitionImageLayout( VkCommandBuffer cmd, VkImage image, uint32 oldLayoutU32, uint32 newLayoutU32, uint32 aspectU32 )
    {
        const VkImageLayout      oldLayout = static_cast<VkImageLayout>( oldLayoutU32 );
        const VkImageLayout      newLayout = static_cast<VkImageLayout>( newLayoutU32 );
        const VkImageAspectFlags aspect    = aspectU32;
        if ( cmd == VK_NULL_HANDLE || image == VK_NULL_HANDLE || oldLayout == newLayout )
            return true;

        VkImageMemoryBarrier barrier{};
        barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                   = oldLayout;
        barrier.newLayout                   = newLayout;
        barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                       = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL )
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage              = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage              = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if ( oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
        {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage              = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if ( oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL )
        {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage              = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage              = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if ( oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL )
        {
            // 그림자맵: 깊이 쓰기(late fragment tests) 가 끝난 뒤 프래그먼트 셰이더가 샘플한다.
            barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage              = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if ( oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL )
        {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            srcStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage              = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else
        {
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            srcStage              = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstStage              = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        vkCmdPipelineBarrier( cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier );
        return true;
    }

    VkRenderPass VulkanRHIDevice::createRenderPassFromSpec( const VulkanRHIRenderPassCache::RenderPassSpec& spec ) const
    {
        return VulkanRHIRenderPassCache::createFromSpec( _device, spec );
    }

    bool VulkanRHIDevice::ensureOffscreenRenderPass( uint32 vkFormat )
    {
        if ( _offscreenRenderPass != VK_NULL_HANDLE )
            return true;
        if ( _device == nullptr )
            return false;

        // 공용 오프스크린 렌더패스는 계약 포맷일 때만 재사용한다. 다른 포맷은 전용 RP 를 만든다.
        const uint32 sharedFormat = static_cast<uint32>( VulkanRHIDeviceInternal::toVulkanTextureFormat( constant::kOffscreenColorFormat ) );
        if ( vkFormat != 0 && vkFormat != sharedFormat )
            return false;

        VulkanRHIRenderPassCache::RenderPassSpec spec{};
        spec.addColor( sharedFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        _offscreenRenderPass = createRenderPassFromSpec( spec );
        return _offscreenRenderPass != VK_NULL_HANDLE;
    }

    bool VulkanRHIDevice::createOffscreenFramebuffer( VulkanTextureRecord& record )
    {
        if ( record._imageView == VK_NULL_HANDLE || record._bRenderTarget == SW_FALSE )
            return false;

        const bool bUseSharedPass = ( record._format == static_cast<uint32>( VK_FORMAT_R8G8B8A8_UNORM ) );
        if ( bUseSharedPass )
        {
            if ( ensureOffscreenRenderPass( record._format ) == false )
                return false;
            record._renderPass = _offscreenRenderPass;
        }
        else
        {
            // 포맷별 전용 RP. beginRenderPass 가 전이를 먼저 걸어 두므로 COLOR_ATTACHMENT_OPTIMAL 에서 시작한다.
            VulkanRHIRenderPassCache::RenderPassSpec spec{};
            spec.addColor( record._format, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            record._renderPass = createRenderPassFromSpec( spec );
            if ( record._renderPass == VK_NULL_HANDLE )
                return false;
        }

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = record._renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &record._imageView;
        fbInfo.width           = record._width;
        fbInfo.height          = record._height;
        fbInfo.layers          = 1;

        if ( vkCreateFramebuffer( _device, &fbInfo, nullptr, &record._framebuffer ) != VK_SUCCESS )
        {
            if ( record._renderPass != _offscreenRenderPass )
                vkDestroyRenderPass( _device, record._renderPass, nullptr );
            record._renderPass = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    void VulkanRHIDevice::destroyOffscreenFramebuffer( VulkanTextureRecord& record )
    {
        // 즉시 파괴하면 안 된다. 아직 실행 중인 프레임의 커맨드버퍼가 이 프레임버퍼를 참조할 수
        // 있다(게임뷰 리사이즈가 대표적인 경로다). 예전에는 오프스크린 경로가 매 프레임 블로킹
        // 제출을 해서 우연히 안전했을 뿐이고, 그 스톨을 걷어내자 곧바로 in-use 위반이 드러났다.
        enqueueFramebufferRelease( record._framebuffer,
                                   ( record._renderPass != _offscreenRenderPass ) ? record._renderPass : VK_NULL_HANDLE );
        record._framebuffer = VK_NULL_HANDLE;
        record._renderPass  = VK_NULL_HANDLE;
    }

    void VulkanRHIDevice::enqueueFramebufferRelease( VkFramebuffer framebuffer, VkRenderPass ownedRenderPass )
    {
        if ( _device == nullptr || ( framebuffer == VK_NULL_HANDLE && ownedRenderPass == VK_NULL_HANDLE ) )
            return;

        VkDevice dev = _device;
        _releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, framebuffer, ownedRenderPass]()
        {
            if ( framebuffer != VK_NULL_HANDLE )
                vkDestroyFramebuffer( dev, framebuffer, nullptr );
            if ( ownedRenderPass != VK_NULL_HANDLE )
                vkDestroyRenderPass( dev, ownedRenderPass, nullptr );
        } ),
                                         _frameFenceCounter + 1 );
    }

    VkRenderPass VulkanRHIDevice::ensurePipelineRenderPass( const RHIPipelineStateDesc& desc )
    {
        VulkanRHIRenderPassCache::PipelineKey key{};
        const bool                            bDepthOnly = ( desc._numRenderTargets == 0 && desc._bEnableDepthTest != 0 );
        key._colorCount                                  = bDepthOnly ? 0u : ( desc._numRenderTargets > 0 ? desc._numRenderTargets : 1u );
        if ( key._colorCount > kMaxColorAttachments )
            key._colorCount = kMaxColorAttachments;
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            VkFormat colorFmt = VulkanRHIDeviceInternal::toVulkanTextureFormat( desc._arrRtvFormat[colorIndex] );
            if ( colorFmt == VK_FORMAT_UNDEFINED )
                colorFmt = VK_FORMAT_R8G8B8A8_UNORM;
            key._arrColorFormat[colorIndex] = static_cast<uint32>( colorFmt );
        }
        if ( desc._bEnableDepthTest != 0 )
        {
            // FrameRenderer 는 RHI D24 를 요청하지만 GPU 가 지원하지 않을 수 있다 → 디바이스가 고른 포맷을 쓴다.
            VkFormat depthFmt = VulkanRHIDeviceInternal::toVulkanTextureFormat( desc._depthStencilFormat );
            if ( desc._depthStencilFormat == sw::RHIFormat::D24_UNORM_S8_UINT ||
                 depthFmt == VK_FORMAT_D24_UNORM_S8_UINT || depthFmt == VK_FORMAT_UNDEFINED )
                depthFmt = static_cast<VkFormat>( _depthFormat );
            key._depthFormat = static_cast<uint32>( depthFmt );
        }

        // PSO 호환용. 파이프라인은 이 RP 와 "호환되는" RP 어디에서든 쓰인다(포맷 · 개수 · 샘플 수만 같으면 된다).
        VulkanRHIRenderPassCache::RenderPassSpec spec{};
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
            spec.addColor( key._arrColorFormat[colorIndex], VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        if ( key._depthFormat != 0 )
            spec.setDepth( key._depthFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );

        return _renderPassCache.ensurePipelineRenderPass( _device, key, spec );
    }

    bool VulkanRHIDevice::ensureCompositeFramebuffer( const VulkanRHIRenderPassCache::CompositeKey& key,
                                                      VulkanRHIRenderPassCache::CompositeRecord&    outRecord )
    {
        // 첨부 뷰와 서술은 여기서 풀고, 조회 · 생성은 캐시가 한 임계구역에서 한다.
        VkImageView arrFbAttachment[kMaxColorAttachments + 1]{};
        uint32      width{ 0 };
        uint32      height{ 0 };
        // beginRenderPass 가 첨부를 미리 COLOR_ATTACHMENT / DEPTH_STENCIL_ATTACHMENT 로 전이해 두므로 그 레이아웃에서 시작·종료한다.
        VulkanRHIRenderPassCache::RenderPassSpec spec{};
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            VulkanTextureRecord* pTex = resolveTexture( key._arrColor[colorIndex] );
            if ( pTex == nullptr || pTex->_imageView == VK_NULL_HANDLE || pTex->_bDepthStencil != SW_FALSE )
                return false;
            arrFbAttachment[colorIndex] = pTex->_imageView;
            width                       = pTex->_width;
            height                      = pTex->_height;
            spec.addColor( pTex->_format, toVkLoadOp( static_cast<RHIRenderPassLoadOp>( key._arrColorLoadOp[colorIndex] ) ),
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        }

        uint32 attachCount = key._colorCount;
        if ( key._depth != 0 )
        {
            VulkanTextureRecord* pTex = resolveTexture( key._depth );
            if ( pTex == nullptr || pTex->_imageView == VK_NULL_HANDLE || pTex->_bDepthStencil == SW_FALSE )
                return false;
            arrFbAttachment[attachCount] = pTex->_imageView;
            ++attachCount;
            if ( width == 0 )
            {
                width  = pTex->_width;
                height = pTex->_height;
            }
            spec.setDepth( pTex->_format, toVkLoadOp( static_cast<RHIRenderPassLoadOp>( key._depthLoadOp ) ),
                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
        }
        if ( attachCount == 0 )
            return false;

        return _renderPassCache.ensureComposite( _device, key, spec, arrFbAttachment, attachCount, width, height, outRecord );
    }

    void VulkanRHIDevice::destroyCompositeFramebuffersUsing( RHITextureHandle texture )
    {
        if ( texture == 0 || _device == nullptr )
            return;
        vector<VulkanRHIRenderPassCache::CompositeRecord> listDetached;
        _renderPassCache.detachCompositesUsing( texture, listDetached );
        for ( const VulkanRHIRenderPassCache::CompositeRecord& record : listDetached )
        {
            // 실행 중인 커맨드버퍼가 아직 참조할 수 있으므로 펜스 통과 후에 파괴한다.
            enqueueFramebufferRelease( record._framebuffer, record._renderPass );
        }
    }

} // namespace sw
