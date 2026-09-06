/**
 * @file VulkanRHIDeviceRenderPass.cpp
 * @brief VulkanRHIDevice 의 렌더패스·프레임버퍼 캐시와 이미지 레이아웃 전이
 * @details Vulkan 은 렌더패스와 프레임버퍼를 명시적으로 만들어야 해서, PSO 가 요구하는 조합마다
 *          캐시가 필요하다. 이 캐시들이 파이프라인이 선언한 포맷과 어긋나면 GPU 가 죽는다.
 */
#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

namespace sw
{
    void VulkanRHIDevice::transitionTextureLayout( VkCommandBuffer cmd, VulkanTextureRecord& record,
                                                   uint32 targetLayout, uint32 aspect )
    {
        if ( cmd == VK_NULL_HANDLE || record._image == VK_NULL_HANDLE )
            return;

        std::scoped_lock<mutex> lock{ _imageLayoutMutex };
        if ( record._layout == targetLayout )
            return;

        transitionImageLayout( cmd, record._image, record._layout, targetLayout, aspect );
        record._layout = targetLayout;
    }

    SW_LOG_CALLER( "Vulkan" );

    static VkAttachmentLoadOp toVkLoadOp( RHIRenderPassLoadOp loadOp )
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

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = static_cast<VkFormat>( sharedFormat );
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        // Match swapchain-compatible dependency style so PSO / active RP stay consistent.
        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments    = &colorAttachment;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dependency;

        return vkCreateRenderPass( _device, &rpInfo, nullptr, &_offscreenRenderPass ) == VK_SUCCESS;
    }

    bool VulkanRHIDevice::createOffscreenFramebuffer( VulkanTextureRecord& record )
    {
        if ( record._imageView == VK_NULL_HANDLE || record._bRenderTarget == 0 )
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
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format         = static_cast<VkFormat>( record._format );
            colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorRef{};
            colorRef.attachment = 0;
            colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments    = &colorRef;

            VkSubpassDependency dependency{};
            dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass    = 0;
            dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo rpInfo{};
            rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpInfo.attachmentCount = 1;
            rpInfo.pAttachments    = &colorAttachment;
            rpInfo.subpassCount    = 1;
            rpInfo.pSubpasses      = &subpass;
            rpInfo.dependencyCount = 1;
            rpInfo.pDependencies   = &dependency;

            if ( vkCreateRenderPass( _device, &rpInfo, nullptr, &record._renderPass ) != VK_SUCCESS )
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
        // 즉시 파괴하면 안 된다 - 아직 실행 중인 프레임의 커맨드버퍼가 이 프레임버퍼를 참조할 수
        // 있다(게임뷰 리사이즈가 대표적인 경로다). 예전엔 오프스크린 경로가 매 프레임 블로킹
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
        PipelineRpKey key{};
        const bool    bDepthOnly = ( desc._numRenderTargets == 0 && desc._bEnableDepthTest != 0 );
        key._colorCount          = bDepthOnly ? 0u : ( desc._numRenderTargets > 0 ? desc._numRenderTargets : 1u );
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
            // FrameRenderer는 RHI D24를 요청하지만 GPU가 미지원일 수 있음 → 디바이스 선택 포맷 사용
            VkFormat depthFmt = VulkanRHIDeviceInternal::toVulkanTextureFormat( desc._depthStencilFormat );
            if ( desc._depthStencilFormat == sw::RHIFormat::D24_UNORM_S8_UINT ||
                 depthFmt == VK_FORMAT_D24_UNORM_S8_UINT || depthFmt == VK_FORMAT_UNDEFINED )
                depthFmt = static_cast<VkFormat>( _depthFormat );
            key._depthFormat = static_cast<uint32>( depthFmt );
        }

        auto existing = _mapPipelineRenderPass.find( key );
        if ( existing != _mapPipelineRenderPass.end() )
            return existing->second;

        VkAttachmentDescription attachments[kMaxColorAttachments + 1]{};
        VkAttachmentReference   colorRefs[kMaxColorAttachments]{};
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            attachments[colorIndex].format         = static_cast<VkFormat>( key._arrColorFormat[colorIndex] );
            attachments[colorIndex].samples        = VK_SAMPLE_COUNT_1_BIT;
            attachments[colorIndex].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[colorIndex].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[colorIndex].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[colorIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[colorIndex].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[colorIndex].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorRefs[colorIndex].attachment       = colorIndex;
            colorRefs[colorIndex].layout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkAttachmentReference depthRef{};
        const bool            bHasDepth   = key._depthFormat != 0;
        uint32                attachCount = key._colorCount;
        if ( bHasDepth )
        {
            attachments[attachCount].format         = static_cast<VkFormat>( key._depthFormat );
            attachments[attachCount].samples        = VK_SAMPLE_COUNT_1_BIT;
            attachments[attachCount].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[attachCount].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[attachCount].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[attachCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[attachCount].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[attachCount].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthRef.attachment                     = attachCount;
            depthRef.layout                         = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            ++attachCount;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = key._colorCount;
        subpass.pColorAttachments       = key._colorCount > 0 ? colorRefs : nullptr;
        subpass.pDepthStencilAttachment = bHasDepth ? &depthRef : nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        if ( bHasDepth )
        {
            dependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = attachCount;
        rpInfo.pAttachments    = attachments;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dependency;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        if ( vkCreateRenderPass( _device, &rpInfo, nullptr, &renderPass ) != VK_SUCCESS )
            return VK_NULL_HANDLE;

        _mapPipelineRenderPass.emplace( key, renderPass );
        return renderPass;
    }

    bool VulkanRHIDevice::ensureCompositeFramebuffer( const CompositeFbKey& key, CompositeFbRecord& outRecord )
    {
        std::scoped_lock<mutex> lock{ _compositeFbMutex };
        auto                    existing = _mapCompositeFramebuffer.find( key );
        if ( existing != _mapCompositeFramebuffer.end() )
        {
            outRecord = existing->second;
            return outRecord._framebuffer != VK_NULL_HANDLE && outRecord._renderPass != VK_NULL_HANDLE;
        }

        VkImageView arrColorView[kMaxColorAttachments]{};
        uint32      arrColorFormat[kMaxColorAttachments]{};
        uint32      width{ 0 };
        uint32      height{ 0 };
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            VulkanTextureRecord* pTex = resolveTexture( key._arrColor[colorIndex] );
            if ( pTex == nullptr || pTex->_imageView == VK_NULL_HANDLE || pTex->_bDepthStencil != 0 )
                return false;
            arrColorView[colorIndex]   = pTex->_imageView;
            arrColorFormat[colorIndex] = pTex->_format;
            width                      = pTex->_width;
            height                     = pTex->_height;
        }

        VkImageView depthView = VK_NULL_HANDLE;
        uint32      depthFormat{ 0 };
        if ( key._depth != 0 )
        {
            VulkanTextureRecord* pTex = resolveTexture( key._depth );
            if ( pTex == nullptr || pTex->_imageView == VK_NULL_HANDLE || pTex->_bDepthStencil == 0 )
                return false;
            depthView   = pTex->_imageView;
            depthFormat = pTex->_format;
            if ( width == 0 )
            {
                width  = pTex->_width;
                height = pTex->_height;
            }
        }
        if ( key._colorCount == 0 && depthView == VK_NULL_HANDLE )
            return false;

        VkAttachmentDescription arrAttachment[kMaxColorAttachments + 1]{};
        VkAttachmentReference   arrColorRef[kMaxColorAttachments]{};
        VkImageView             arrFbAttachment[kMaxColorAttachments + 1]{};
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            arrAttachment[colorIndex].format         = static_cast<VkFormat>( arrColorFormat[colorIndex] );
            arrAttachment[colorIndex].samples        = VK_SAMPLE_COUNT_1_BIT;
            arrAttachment[colorIndex].loadOp         = toVkLoadOp( static_cast<RHIRenderPassLoadOp>( key._arrColorLoadOp[colorIndex] ) );
            arrAttachment[colorIndex].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            arrAttachment[colorIndex].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            arrAttachment[colorIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            // Match pre-begin transitionImageLayout to COLOR_ATTACHMENT_OPTIMAL.
            arrAttachment[colorIndex].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            arrAttachment[colorIndex].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            arrColorRef[colorIndex].attachment      = colorIndex;
            arrColorRef[colorIndex].layout          = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            arrFbAttachment[colorIndex]             = arrColorView[colorIndex];
        }

        VkAttachmentReference depthRef{};
        uint32                attachCount = key._colorCount;
        const bool            bHasDepth   = depthView != VK_NULL_HANDLE;
        if ( bHasDepth )
        {
            arrAttachment[attachCount].format         = static_cast<VkFormat>( depthFormat );
            arrAttachment[attachCount].samples        = VK_SAMPLE_COUNT_1_BIT;
            arrAttachment[attachCount].loadOp         = toVkLoadOp( static_cast<RHIRenderPassLoadOp>( key._depthLoadOp ) );
            arrAttachment[attachCount].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            arrAttachment[attachCount].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            arrAttachment[attachCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            arrAttachment[attachCount].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            arrAttachment[attachCount].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthRef.attachment                       = attachCount;
            depthRef.layout                           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            arrFbAttachment[attachCount]              = depthView;
            ++attachCount;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = key._colorCount;
        subpass.pColorAttachments       = key._colorCount > 0 ? arrColorRef : nullptr;
        subpass.pDepthStencilAttachment = bHasDepth ? &depthRef : nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = attachCount;
        rpInfo.pAttachments    = arrAttachment;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dependency;

        CompositeFbRecord record{};
        if ( vkCreateRenderPass( _device, &rpInfo, nullptr, &record._renderPass ) != VK_SUCCESS )
            return false;

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = record._renderPass;
        fbInfo.attachmentCount = attachCount;
        fbInfo.pAttachments    = arrFbAttachment;
        fbInfo.width           = width;
        fbInfo.height          = height;
        fbInfo.layers          = 1;

        if ( vkCreateFramebuffer( _device, &fbInfo, nullptr, &record._framebuffer ) != VK_SUCCESS )
        {
            vkDestroyRenderPass( _device, record._renderPass, nullptr );
            return false;
        }
        record._width  = width;
        record._height = height;
        _mapCompositeFramebuffer.emplace( key, record );
        outRecord = record;
        return true;
    }

    void VulkanRHIDevice::destroyCompositeFramebuffersUsing( RHITextureHandle texture )
    {
        if ( texture == 0 || _device == nullptr )
            return;
        std::scoped_lock<mutex> lock{ _compositeFbMutex };
        for ( auto it = _mapCompositeFramebuffer.begin(); it != _mapCompositeFramebuffer.end(); )
        {
            bool bUses = ( it->first._depth == texture );
            for ( uint32 colorIndex = 0; colorIndex < it->first._colorCount && bUses == false; ++colorIndex )
            {
                bUses = ( it->first._arrColor[colorIndex] == texture );
            }
            if ( bUses )
            {
                // 실행 중인 커맨드버퍼가 아직 참조할 수 있으므로 펜스 통과 후에 파괴한다.
                enqueueFramebufferRelease( it->second._framebuffer, it->second._renderPass );
                it = _mapCompositeFramebuffer.erase( it );
            }
            else
                ++it;
        }
    }

} // namespace sw
