#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIRenderPassCache.h"

#include <vulkan/vulkan.h>

namespace sw
{
    void VulkanRHIRenderPassCache::RenderPassSpec::addColor( uint32 vkFormat, uint32 loadOp, uint32 initialLayout, uint32 finalLayout )
    {
        if ( _colorCount >= kMaxColorAttachments )
            return;
        _arrColorFormat[_colorCount]        = vkFormat;
        _arrColorLoadOp[_colorCount]        = loadOp;
        _arrColorStoreOp[_colorCount]       = VK_ATTACHMENT_STORE_OP_STORE;
        _arrColorInitialLayout[_colorCount] = initialLayout;
        _arrColorFinalLayout[_colorCount]   = finalLayout;
        ++_colorCount;
    }

    void VulkanRHIRenderPassCache::RenderPassSpec::setDepth( uint32 vkFormat, uint32 loadOp, uint32 initialLayout, uint32 finalLayout )
    {
        _depthFormat        = vkFormat;
        _depthLoadOp        = loadOp;
        _depthInitialLayout = initialLayout;
        _depthFinalLayout   = finalLayout;
    }

    bool VulkanRHIRenderPassCache::CompositeKey::operator==( const CompositeKey& other ) const
    {
        if ( _colorCount != other._colorCount || _depth != other._depth || _depthLoadOp != other._depthLoadOp )
            return false;
        for ( uint32 colorIndex = 0; colorIndex < _colorCount; ++colorIndex )
        {
            if ( _arrColor[colorIndex] != other._arrColor[colorIndex] || _arrColorLoadOp[colorIndex] != other._arrColorLoadOp[colorIndex] )
                return false;
        }
        return true;
    }

    size_t VulkanRHIRenderPassCache::CompositeKeyHash::operator()( const CompositeKey& key ) const
    {
        size_t hash = static_cast<size_t>( key._depth ) * 1315423911u;
        hash ^= static_cast<size_t>( key._colorCount ) + 0x9e3779b9u;
        hash ^= static_cast<size_t>( key._depthLoadOp ) + 0x9e3779b9u;
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            hash ^= static_cast<size_t>( key._arrColor[colorIndex] ) + 0x9e3779b9u + ( hash << 6 ) + ( hash >> 2 );
            hash ^= static_cast<size_t>( key._arrColorLoadOp[colorIndex] ) + 0x9e3779b9u;
        }
        return hash;
    }

    bool VulkanRHIRenderPassCache::PipelineKey::operator==( const PipelineKey& other ) const
    {
        if ( _colorCount != other._colorCount || _depthFormat != other._depthFormat )
            return false;
        for ( uint32 colorIndex = 0; colorIndex < _colorCount; ++colorIndex )
        {
            if ( _arrColorFormat[colorIndex] != other._arrColorFormat[colorIndex] )
                return false;
        }
        return true;
    }

    size_t VulkanRHIRenderPassCache::PipelineKeyHash::operator()( const PipelineKey& key ) const
    {
        size_t hash = static_cast<size_t>( key._depthFormat ) * 1315423911u;
        hash ^= static_cast<size_t>( key._colorCount ) + 0x9e3779b9u;
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            hash ^= static_cast<size_t>( key._arrColorFormat[colorIndex] ) + 0x9e3779b9u + ( hash << 6 ) + ( hash >> 2 );
        }
        return hash;
    }

    VulkanRHIRenderPassCache::VulkanRHIRenderPassCache()
        : _compositeMutex{}
        , _mapComposite{}
        , _mapPipelineRenderPass{}
        , _listRenderPassRecord{}
    {
    }

    VkRenderPass VulkanRHIRenderPassCache::createFromSpec( VkDevice device, const RenderPassSpec& spec )
    {
        const bool bHasDepth = spec._depthFormat != 0;
        if ( device == nullptr || ( spec._colorCount == 0 && bHasDepth == false ) )
            return VK_NULL_HANDLE;

        VkAttachmentDescription arrAttachment[kMaxColorAttachments + 1]{};
        VkAttachmentReference   arrColorRef[kMaxColorAttachments]{};
        for ( uint32 colorIndex = 0; colorIndex < spec._colorCount; ++colorIndex )
        {
            arrAttachment[colorIndex].format         = static_cast<VkFormat>( spec._arrColorFormat[colorIndex] );
            arrAttachment[colorIndex].samples        = VK_SAMPLE_COUNT_1_BIT;
            arrAttachment[colorIndex].loadOp         = static_cast<VkAttachmentLoadOp>( spec._arrColorLoadOp[colorIndex] );
            arrAttachment[colorIndex].storeOp        = static_cast<VkAttachmentStoreOp>( spec._arrColorStoreOp[colorIndex] );
            arrAttachment[colorIndex].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            arrAttachment[colorIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            arrAttachment[colorIndex].initialLayout  = static_cast<VkImageLayout>( spec._arrColorInitialLayout[colorIndex] );
            arrAttachment[colorIndex].finalLayout    = static_cast<VkImageLayout>( spec._arrColorFinalLayout[colorIndex] );
            arrColorRef[colorIndex].attachment       = colorIndex;
            arrColorRef[colorIndex].layout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkAttachmentReference depthRef{};
        uint32                attachCount = spec._colorCount;
        if ( bHasDepth )
        {
            arrAttachment[attachCount].format         = static_cast<VkFormat>( spec._depthFormat );
            arrAttachment[attachCount].samples        = VK_SAMPLE_COUNT_1_BIT;
            arrAttachment[attachCount].loadOp         = static_cast<VkAttachmentLoadOp>( spec._depthLoadOp );
            arrAttachment[attachCount].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            arrAttachment[attachCount].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            arrAttachment[attachCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            arrAttachment[attachCount].initialLayout  = static_cast<VkImageLayout>( spec._depthInitialLayout );
            arrAttachment[attachCount].finalLayout    = static_cast<VkImageLayout>( spec._depthFinalLayout );
            depthRef.attachment                       = attachCount;
            depthRef.layout                           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            ++attachCount;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = spec._colorCount;
        subpass.pColorAttachments       = spec._colorCount > 0 ? arrColorRef : nullptr;
        subpass.pDepthStencilAttachment = bHasDepth ? &depthRef : nullptr;

        // 바깥(앞선 제출)의 컬러 쓰기 → 이 패스의 컬러 쓰기. 깊이가 있으면 초기 프래그먼트 테스트 단계도 같이 건다.
        // 예전엔 자리마다 이 마스크가 달랐다(깊이 없는 패스에도 깊이 단계를 걸거나, 깊이 패스에 빼먹거나).
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
        rpInfo.pAttachments    = arrAttachment;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dependency;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        if ( vkCreateRenderPass( device, &rpInfo, nullptr, &renderPass ) != VK_SUCCESS )
            return VK_NULL_HANDLE;
        return renderPass;
    }

    VkRenderPass VulkanRHIRenderPassCache::ensurePipelineRenderPass( VkDevice device, const PipelineKey& key, const RenderPassSpec& spec )
    {
        const auto existing = _mapPipelineRenderPass.find( key );
        if ( existing != _mapPipelineRenderPass.end() )
            return existing->second;

        VkRenderPass renderPass = createFromSpec( device, spec );
        if ( renderPass == VK_NULL_HANDLE )
            return VK_NULL_HANDLE;
        _mapPipelineRenderPass.emplace( key, renderPass );
        return renderPass;
    }

    bool VulkanRHIRenderPassCache::ensureComposite( VkDevice device, const CompositeKey& key, const RenderPassSpec& spec,
                                                    const VkImageView* pAttachmentView, uint32 attachmentCount, uint32 width, uint32 height,
                                                    CompositeRecord& outRecord )
    {
        std::scoped_lock<mutex> lock{ _compositeMutex };
        const auto              existing = _mapComposite.find( key );
        if ( existing != _mapComposite.end() )
        {
            outRecord = existing->second;
            return outRecord._framebuffer != VK_NULL_HANDLE && outRecord._renderPass != VK_NULL_HANDLE;
        }
        if ( device == nullptr || attachmentCount == 0 || pAttachmentView == nullptr )
            return false;

        CompositeRecord record{};
        record._renderPass = createFromSpec( device, spec );
        if ( record._renderPass == VK_NULL_HANDLE )
            return false;

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = record._renderPass;
        fbInfo.attachmentCount = attachmentCount;
        fbInfo.pAttachments    = pAttachmentView;
        fbInfo.width           = width;
        fbInfo.height          = height;
        fbInfo.layers          = 1;

        if ( vkCreateFramebuffer( device, &fbInfo, nullptr, &record._framebuffer ) != VK_SUCCESS )
        {
            vkDestroyRenderPass( device, record._renderPass, nullptr );
            return false;
        }
        record._width  = width;
        record._height = height;
        _mapComposite.emplace( key, record );
        outRecord = record;
        return true;
    }

    void VulkanRHIRenderPassCache::detachCompositesUsing( RHITextureHandle texture, vector<CompositeRecord>& outListDetached )
    {
        if ( texture == 0 )
            return;
        std::scoped_lock<mutex> lock{ _compositeMutex };
        for ( auto it = _mapComposite.begin(); it != _mapComposite.end(); )
        {
            bool bUses = ( it->first._depth == texture );
            for ( uint32 colorIndex = 0; colorIndex < it->first._colorCount && bUses == false; ++colorIndex )
            {
                bUses = ( it->first._arrColor[colorIndex] == texture );
            }
            if ( bUses )
            {
                outListDetached.push_back( it->second );
                it = _mapComposite.erase( it );
            }
            else
                ++it;
        }
    }

    RHIRenderPassHandle VulkanRHIRenderPassCache::addRenderPassRecord( VkRenderPass renderPass, bool bOwned )
    {
        RenderPassRecord record{};
        record._renderPass = renderPass;
        record._bOwned     = bOwned ? SW_TRUE : SW_FALSE;
        _listRenderPassRecord.push_back( record );
        return _listRenderPassRecord.size();
    }

    VulkanRHIRenderPassCache::RenderPassRecord* VulkanRHIRenderPassCache::resolveRenderPassRecord( RHIRenderPassHandle handle )
    {
        if ( handle == 0 || handle > _listRenderPassRecord.size() )
            return nullptr;
        return &_listRenderPassRecord[handle - 1];
    }

    void VulkanRHIRenderPassCache::destroyRenderPassRecord( VkDevice device, RHIRenderPassHandle handle, VkRenderPass swapchainRenderPass )
    {
        RenderPassRecord* pRecord = resolveRenderPassRecord( handle );
        if ( pRecord == nullptr )
            return;
        if ( pRecord->_bOwned != SW_FALSE && pRecord->_renderPass != VK_NULL_HANDLE && pRecord->_renderPass != swapchainRenderPass )
            vkDestroyRenderPass( device, pRecord->_renderPass, nullptr );
        *pRecord = RenderPassRecord{};
    }

    void VulkanRHIRenderPassCache::destroyAll( VkDevice device, VkRenderPass swapchainRenderPass )
    {
        {
            std::scoped_lock<mutex> lock{ _compositeMutex };
            for ( auto& pair : _mapComposite )
            {
                if ( pair.second._framebuffer != VK_NULL_HANDLE )
                    vkDestroyFramebuffer( device, pair.second._framebuffer, nullptr );
                if ( pair.second._renderPass != VK_NULL_HANDLE )
                    vkDestroyRenderPass( device, pair.second._renderPass, nullptr );
            }
            _mapComposite.clear();
        }
        for ( auto& pair : _mapPipelineRenderPass )
        {
            if ( pair.second != VK_NULL_HANDLE )
                vkDestroyRenderPass( device, pair.second, nullptr );
        }
        _mapPipelineRenderPass.clear();
        for ( RenderPassRecord& record : _listRenderPassRecord )
        {
            if ( record._bOwned != SW_FALSE && record._renderPass != VK_NULL_HANDLE && record._renderPass != swapchainRenderPass )
                vkDestroyRenderPass( device, record._renderPass, nullptr );
        }
        _listRenderPassRecord.clear();
    }
} // namespace sw
