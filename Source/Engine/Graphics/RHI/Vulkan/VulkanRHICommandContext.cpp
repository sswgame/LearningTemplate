#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

#include <vulkan/vulkan.h>

namespace sw
{
    VulkanRHICommandContext::VulkanRHICommandContext( VulkanRHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _targetBuffer{ nullptr }
        , _pState{ pDevice != nullptr ? &pDevice->_recordingState : nullptr }
    {
    }

    VulkanRHICommandContext::VulkanRHICommandContext( VulkanRHIDevice* pDevice, VkCommandBuffer targetBuffer,
                                                      VulkanRecordingState* pState )
        : _pDevice{ pDevice }
        , _targetBuffer{ targetBuffer }
        , _pState{ pState }
    {
    }

    VkCommandBuffer VulkanRHICommandContext::commandBuffer() const
    {
        if ( _targetBuffer != nullptr )
            return _targetBuffer;
        return _pDevice != nullptr ? _pDevice->currentCommandBuffer() : nullptr;
    }

    SW_LOG_CALLER( "Vulkan" );

    static void setVkClearColor( VkClearValue& dst, const float32* pClear )
    {
        dst.color.float32[0] = pClear[0];
        dst.color.float32[1] = pClear[1];
        dst.color.float32[2] = pClear[2];
        dst.color.float32[3] = pClear[3];
    }

    static void mapStateVal( RHIBufferState state, VkAccessFlags& access, VkPipelineStageFlags& stage )
    {
        switch ( state )
        {
            case RHIBufferState::UnorderedAccess:
                access = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
                stage  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                break;
            case RHIBufferState::ShaderResource:
                access = VK_ACCESS_SHADER_READ_BIT;
                stage  = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                break;
            case RHIBufferState::IndirectArgument:
                access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                stage  = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
                break;
            case RHIBufferState::CopyDest:
                access = VK_ACCESS_TRANSFER_WRITE_BIT;
                stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
                break;
            case RHIBufferState::VertexOrConstant:
                access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
                stage  = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                break;
            case RHIBufferState::Index:
                access = VK_ACCESS_INDEX_READ_BIT;
                stage  = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                break;
            case RHIBufferState::Common:
            default:
                access = 0;
                stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                break;
        }
    }

    void VulkanRHICommandContext::beginOffscreenPass( RHITextureHandle colorTarget, const float4& clearColor )
    {
        if ( colorTarget == 0 )
        {
            _pDevice->beginFrame( clearColor );
            return;
        }

        VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( colorTarget );
        if ( pResolved == nullptr || _pDevice->_offscreenCommandBuffer == VK_NULL_HANDLE )
            return;
        VulkanRHIDevice::VulkanTextureRecord& record = *pResolved;
        if ( record._framebuffer == VK_NULL_HANDLE || record._renderPass == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR( "beginOffscreenPass: texture has no framebuffer." );
            return;
        }

        // beginFrame 이 이미 스왑체인 렌더패스를 열어둔 상태다(S1 이후). 오프스크린 전용 버퍼로
        // 갈아타기 전에 프레임 버퍼의 렌더패스를 실제로 닫아야 한다 — 예전엔 플래그만 내렸는데,
        // 그때는 beginFrame 이 아직 안 불려 열린 렌더패스가 없어서 무해했을 뿐이다.
        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            VkCommandBuffer frameCmd = commandBuffer();
            if ( frameCmd != VK_NULL_HANDLE )
                vkCmdEndRenderPass( frameCmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        vkWaitForFences( _pDevice->_device, 1, &_pDevice->_offscreenFence, VK_TRUE, UINT64_MAX );
        vkResetFences( _pDevice->_device, 1, &_pDevice->_offscreenFence );
        vkResetCommandBuffer( _pDevice->_offscreenCommandBuffer, 0 );

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer( _pDevice->_offscreenCommandBuffer, &beginInfo );

        // 새 커맨드버퍼엔 아직 아무 디스크립터셋도 안 걸림 — bindGraphicsMaterialSets 캐시 무효화.
        _pState->_lastBoundGraphicsSet0    = nullptr;
        _pState->_bStaticGraphicsSetsBound = false;

        _pDevice->transitionImageLayout( _pDevice->_offscreenCommandBuffer, record._image, record._layout,
                                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                         VK_IMAGE_ASPECT_COLOR_BIT );
        record._layout = static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );

        _pState->_bOffscreenPassActive = 1;
        // _bFrameStarted 는 프레임 수명주기 상태라 beginFrame/endFrame 만 소유한다. 예전엔 여기서
        // 강제로 세웠는데, beginFrame 이 항상 먼저 오는 지금은 불필요하고 오히려 위험하다
        // (docs/05_RHI_FrameContract.md R5 참고).
        _pState->_bRenderPassActive     = SW_FALSE;
        _pState->_activeOffscreenTarget = colorTarget;

        // Default clear pass (FrameRenderer may restart passes on this same buffer).
        RHIRenderPassBeginInfo rpBegin{};
        rpBegin.setColorTarget( colorTarget, clearColor, RHIRenderPassLoadOp::Clear );
        rpBegin._width  = record._width;
        rpBegin._height = record._height;
        beginRenderPass( rpBegin );
    }

    void VulkanRHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || src == 0 )
            return;

        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        VulkanRHIDevice::VulkanTextureRecord* pSrcResolved = _pDevice->resolveTexture( src );
        if ( pSrcResolved == nullptr || pSrcResolved->_image == VK_NULL_HANDLE || pSrcResolved->_bDepthStencil != 0 )
            return;

        VulkanRHIDevice::VulkanTextureRecord& srcRec = *pSrcResolved;
        _pDevice->transitionImageLayout( cmd, srcRec._image, srcRec._layout,
                                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                         VK_IMAGE_ASPECT_COLOR_BIT );
        srcRec._layout = static_cast<uint32>( VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );

        VkImage dstImage = VK_NULL_HANDLE;
        uint32  dstW     = _pDevice->_swapChainExtentWidth;
        uint32  dstH     = _pDevice->_swapChainExtentHeight;

        if ( dst == 0 )
        {
            if ( _pDevice->_imageIndex >= _pDevice->_listSwapChainImage.size() )
                return;
            dstImage = _pDevice->_listSwapChainImage[_pDevice->_imageIndex];
            _pDevice->transitionImageLayout( cmd, dstImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_IMAGE_ASPECT_COLOR_BIT );
        }
        else
        {
            VulkanRHIDevice::VulkanTextureRecord* pDstResolved = _pDevice->resolveTexture( dst );
            if ( pDstResolved == nullptr || pDstResolved->_image == VK_NULL_HANDLE )
                return;
            dstImage = pDstResolved->_image;
            dstW     = pDstResolved->_width;
            dstH     = pDstResolved->_height;
            _pDevice->transitionImageLayout( cmd, dstImage, pDstResolved->_layout,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_IMAGE_ASPECT_COLOR_BIT );
            pDstResolved->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
        }

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1]             = { static_cast<int32>( srcRec._width ), static_cast<int32>( srcRec._height ), 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[1]             = { static_cast<int32>( dstW ), static_cast<int32>( dstH ), 1 };

        vkCmdBlitImage( cmd, srcRec._image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR );

        if ( dst == 0 )
        {
            _pDevice->transitionImageLayout( cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                             VK_IMAGE_ASPECT_COLOR_BIT );
        }
        else
        {
            _pDevice->transitionImageLayout( cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                             VK_IMAGE_ASPECT_COLOR_BIT );
            VulkanRHIDevice::VulkanTextureRecord* pDstResolved = _pDevice->resolveTexture( dst );
            if ( pDstResolved != nullptr )
                pDstResolved->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        }
    }

    void VulkanRHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( _pDevice->supportsNativeBindlessSampling() )
            return;

        VkDescriptorSet       descSet    = VK_NULL_HANDLE;
        const VkDescriptorSet textureSet = _pDevice->registeredTextureSetAt( index );
        if ( textureSet != VK_NULL_HANDLE && textureSet != _pDevice->_bindlessTextureSet )
            descSet = textureSet;

        if ( descSet != VK_NULL_HANDLE )
        {
            vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 2 + slot, 1, &descSet, 0, nullptr );
            vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, 2 + slot, 1, &descSet, 0, nullptr );
        }
    }

    void VulkanRHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || texture == 0 )
            return;
        VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
        if ( pResolved == nullptr || pResolved->_image == VK_NULL_HANDLE )
            return;

        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        VulkanRHIDevice::VulkanTextureRecord& record       = *pResolved;
        const uint32                          targetLayout = ( record._bDepthStencil != 0 )
                                                               ? static_cast<uint32>( VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL )
                                                               : static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        if ( record._layout == targetLayout )
            return;

        const uint32 aspect = ( record._bDepthStencil != 0 )
                                ? _pDevice->depthAspectMask()
                                : static_cast<uint32>( VK_IMAGE_ASPECT_COLOR_BIT );
        _pDevice->transitionImageLayout( cmd, record._image, record._layout, targetLayout, aspect );
        record._layout = targetLayout;
    }

    void VulkanRHICommandContext::endOffscreenPass( RHITextureHandle colorTarget )
    {
        if ( colorTarget == 0 || _pState->_bOffscreenPassActive == 0 || _pDevice->_offscreenCommandBuffer == VK_NULL_HANDLE )
            return;

        VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( colorTarget );
        if ( pResolved == nullptr )
            return;

        VulkanRHIDevice::VulkanTextureRecord& record = *pResolved;
        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( _pDevice->_offscreenCommandBuffer );
            _pState->_bRenderPassActive = SW_FALSE;
        }
        _pDevice->transitionImageLayout( _pDevice->_offscreenCommandBuffer, record._image,
                                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         VK_IMAGE_ASPECT_COLOR_BIT );
        record._layout = static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

        vkEndCommandBuffer( _pDevice->_offscreenCommandBuffer );

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &_pDevice->_offscreenCommandBuffer;
        vkQueueSubmit( _pDevice->_graphicsQueue, 1, &submitInfo, _pDevice->_offscreenFence );
        vkWaitForFences( _pDevice->_device, 1, &_pDevice->_offscreenFence, VK_TRUE, UINT64_MAX );

        _pState->_bOffscreenPassActive = 0;
        // _bFrameStarted 를 여기서 내리면 뒤따르는 endFrame 이 조기 반환해 프레임 제출이 통째로
        // 사라진다(=acquire 세마포어가 신호된 채 남아 다음 프레임 acquire 가 실패). 프레임 수명주기는
        // beginFrame/endFrame 소유다.
        _pState->_activeOffscreenTarget = 0;
    }

    void VulkanRHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;

        _pState->_activeGraphicsPso                               = pso;
        VkPipeline                                        pipe    = _pDevice->_pipeline;
        const VulkanRHIDevice::VulkanPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord != nullptr )
        {
            if ( pRecord->_pipeline != VK_NULL_HANDLE )
                pipe = pRecord->_pipeline;
        }

        if ( pipe != VK_NULL_HANDLE )
            vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe );
    }

    void VulkanRHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;

        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        const VulkanRHIDevice::VulkanPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord != nullptr )
        {
            if ( pRecord->_pipeline != VK_NULL_HANDLE )
                vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pRecord->_pipeline );

            VkDescriptorSet set0 = _pDevice->_descriptorSet;
            if ( set0 == VK_NULL_HANDLE )
                set0 = _pDevice->registeredDescriptorSetAt( 0 );
            if ( set0 != VK_NULL_HANDLE && _pDevice->_pipelineLayout != VK_NULL_HANDLE )
                vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, 0, 1, &set0, 0, nullptr );

            // set 1: bindless 텍스처 배열 / set 4: 정적 샘플러 — binding.hlsli 를 포함하는 컴퓨트 셰이더가
            // (SW_BINDLESS 활성화로) 정적으로 참조할 수 있으므로 그래픽스와 동일하게 매 디스패치 바인딩한다.
            if ( _pDevice->_pipelineLayout != VK_NULL_HANDLE )
            {
                if ( _pDevice->_bindlessTextureSet != VK_NULL_HANDLE )
                    vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, 1, 1, &_pDevice->_bindlessTextureSet, 0, nullptr );
                if ( _pDevice->_staticSamplerSet != VK_NULL_HANDLE )
                    vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, 4, 1, &_pDevice->_staticSamplerSet, 0, nullptr );
            }
        }
    }

    void VulkanRHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;

        const bool   bBindColor = beginInfo._bBindColor != 0;
        const uint32 colorCount = bBindColor ? ( beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u ) : 0u;
        const bool   bHasDepth  = beginInfo._depthTarget != 0;

        // Depth-only still uses composite path when a depth target is provided.
        if ( bBindColor == 0 && bHasDepth == false )
            return;

        RHITextureHandle colorHandles[kMaxColorAttachments]{};
        for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
        {
            colorHandles[attachmentIndex] = beginInfo._arrColorTarget[attachmentIndex];
        }

        if ( colorCount == 1 && colorHandles[0] == 0 && _pState->_activeOffscreenTarget != 0 )
            colorHandles[0] = _pState->_activeOffscreenTarget;

        // Composite FB for MRT, color+depth, or depth-only. Keep plain single-RT / swapchain path otherwise.
        const bool bUseComposite = ( colorCount > 1 ) || ( colorCount == 1 && colorHandles[0] != 0 && bHasDepth ) ||
                                   ( colorCount == 0 && bHasDepth );

        VkRenderPass  renderPass  = _pDevice->_renderPass;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D    extent{ _pDevice->_swapChainExtentWidth, _pDevice->_swapChainExtentHeight };
        VkClearValue  clearValues[kMaxColorAttachments + 1]{};
        uint32        clearCount{ 0 };

        if ( bUseComposite )
        {
            VulkanRHIDevice::CompositeFbKey key{};
            key._colorCount = ( colorCount > kMaxColorAttachments ) ? kMaxColorAttachments : colorCount;
            for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
            {
                key._arrColor[colorIndex]       = colorHandles[colorIndex];
                key._arrColorLoadOp[colorIndex] = static_cast<uint8>( beginInfo._arrLoadOp[colorIndex] );
            }
            key._depth       = beginInfo._depthTarget;
            key._depthLoadOp = static_cast<uint8>( beginInfo._depthLoadOp );

            if ( _pState->_bRenderPassActive == SW_TRUE )
            {
                vkCmdEndRenderPass( cmd );
                _pState->_bRenderPassActive = SW_FALSE;
            }

            for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
            {
                VulkanRHIDevice::VulkanTextureRecord* pTex = _pDevice->resolveTexture( key._arrColor[colorIndex] );
                if ( pTex == nullptr )
                    return;
                constexpr uint32 aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                _pDevice->transitionImageLayout( cmd, pTex->_image, pTex->_layout,
                                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, aspect );
                pTex->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            }
            if ( key._depth != 0 )
            {
                VulkanRHIDevice::VulkanTextureRecord* pTex = _pDevice->resolveTexture( key._depth );
                if ( pTex == nullptr )
                    return;
                const uint32 aspect = _pDevice->depthAspectMask();
                _pDevice->transitionImageLayout( cmd, pTex->_image, pTex->_layout,
                                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect );
                pTex->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
            }

            VulkanRHIDevice::CompositeFbRecord composite{};
            if ( _pDevice->ensureCompositeFramebuffer( key, composite ) == false )
                return;

            renderPass  = composite._renderPass;
            framebuffer = composite._framebuffer;
            extent      = { composite._width, composite._height };
            // Track RT only — do NOT set _bOffscreenPassActive (that routes to a separate CB).
            if ( key._colorCount > 0 )
                _pState->_activeOffscreenTarget = key._arrColor[0];

            for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
            {
                const float32* pClear = &beginInfo._arrClearColor[colorIndex]._x;
                setVkClearColor( clearValues[clearCount++], pClear );
            }
            if ( key._depth != 0 )
            {
                clearValues[clearCount].depthStencil.depth   = beginInfo._clearDepth;
                clearValues[clearCount].depthStencil.stencil = 0;
                ++clearCount;
            }
        }
        else
        {
            // Existing single-RT path (swapchain or per-texture offscreen FB).
            RHITextureHandle colorTarget = ( colorCount > 0 ) ? colorHandles[0] : 0;
            if ( colorTarget != 0 )
            {
                VulkanRHIDevice::VulkanTextureRecord* pTex = _pDevice->resolveTexture( colorTarget );
                if ( pTex == nullptr || pTex->_framebuffer == VK_NULL_HANDLE || pTex->_renderPass == VK_NULL_HANDLE )
                    return;

                constexpr uint32 aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                _pDevice->transitionImageLayout( cmd, pTex->_image, pTex->_layout,
                                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, aspect );
                pTex->_layout                   = static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
                renderPass                      = pTex->_renderPass;
                framebuffer                     = pTex->_framebuffer;
                extent                          = { pTex->_width, pTex->_height };
                _pState->_activeOffscreenTarget = colorTarget;
            }
            else
            {
                if ( _pDevice->_renderPass == VK_NULL_HANDLE || _pDevice->_listSwapChainFramebuffer.empty() || _pDevice->_imageIndex >= _pDevice->_listSwapChainFramebuffer.size() )
                    return;
                framebuffer = _pDevice->_listSwapChainFramebuffer[_pDevice->_imageIndex];
                extent      = { _pDevice->_swapChainExtentWidth, _pDevice->_swapChainExtentHeight };
            }

            if ( _pState->_bRenderPassActive == SW_TRUE )
            {
                vkCmdEndRenderPass( cmd );
                _pState->_bRenderPassActive = SW_FALSE;
            }

            const float32* pClear = &beginInfo._arrClearColor[0]._x;
            setVkClearColor( clearValues[0], pClear );
            clearCount = 1;
        }

        if ( beginInfo._width > 0 && beginInfo._height > 0 )
        {
            extent.width  = MathUtil::min( extent.width, beginInfo._width );
            extent.height = MathUtil::min( extent.height, beginInfo._height );
        }

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass        = renderPass;
        renderPassInfo.framebuffer       = framebuffer;
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = extent;
        renderPassInfo.clearValueCount   = clearCount;
        renderPassInfo.pClearValues      = clearValues;

        vkCmdBeginRenderPass( cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );
        _pState->_bRenderPassActive = SW_TRUE;

        // Match DX12 beginRenderPass: viewport = pass extent, DX Y orientation.
        RHIViewport vp{};
        vp._width    = static_cast<float32>( extent.width );
        vp._height   = static_cast<float32>( extent.height );
        vp._minDepth = 0.0f;
        vp._maxDepth = 1.0f;
        setViewport( vp );
    }

    void VulkanRHICommandContext::endRenderPass()
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pState->_bRenderPassActive == SW_FALSE )
            return;
        vkCmdEndRenderPass( cmd );
        _pState->_bRenderPassActive = SW_FALSE;
    }

    void VulkanRHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
    {
        VkCommandBuffer                      cmd     = commandBuffer();
        VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
        if ( cmd == VK_NULL_HANDLE || pRecord == nullptr )
            return;

        if ( pRecord->_buffer == VK_NULL_HANDLE || pRecord->_state == newState )
            return;

        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        VkAccessFlags        srcAccess{ 0 };
        VkAccessFlags        dstAccess{ 0 };
        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        mapStateVal( pRecord->_state, srcAccess, srcStage );
        mapStateVal( newState, dstAccess, dstStage );

        VkBufferMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask       = srcAccess;
        barrier.dstAccessMask       = dstAccess;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = pRecord->_buffer;
        barrier.offset              = 0;
        barrier.size                = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier( cmd, srcStage, dstStage, 0, 0, nullptr, 1, &barrier, 0, nullptr );
        pRecord->_state = newState;
    }

    void VulkanRHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
    {
        // u<slot> (RW 구조버퍼) 전용 — set 7..9. t<slot> (읽기전용) 은 bindComputeShaderResource(set 6..9) 를 쓴다.
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pDevice->_pipelineLayout == VK_NULL_HANDLE || slot >= 3 )
            return;

        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        VkDescriptorSet       descSet = VK_NULL_HANDLE;
        const VkDescriptorSet uavSet  = _pDevice->registeredUavSetAt( index );
        if ( uavSet != VK_NULL_HANDLE )
            descSet = uavSet;
        else
            descSet = _pDevice->registeredDescriptorSetAt( index );

        if ( descSet != VK_NULL_HANDLE )
        {
            vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, 7 + slot, 1, &descSet, 0, nullptr );
        }
    }

    void VulkanRHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        // t<slot> (읽기전용 구조버퍼) 전용 — set 6..9.
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pDevice->_pipelineLayout == VK_NULL_HANDLE || slot >= 4 )
            return;

        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        VkDescriptorSet       descSet = VK_NULL_HANDLE;
        const VkDescriptorSet uavSet  = _pDevice->registeredUavSetAt( index );
        if ( uavSet != VK_NULL_HANDLE )
            descSet = uavSet;
        else
            descSet = _pDevice->registeredDescriptorSetAt( index );

        if ( descSet != VK_NULL_HANDLE )
        {
            vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, 6 + slot, 1, &descSet, 0, nullptr );
        }
    }

    void VulkanRHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // set 0 binding 0 = b<slot>. gpucull 등 컴퓨트 셰이더의 cbuffer(register(b0)) 전용.
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pDevice->_pipelineLayout == VK_NULL_HANDLE || slot != 0 )
            return;
        if ( index == kInvalidDescriptorIndex ||
             index >= static_cast<RHIDescriptorIndex>( _pDevice->registeredDescriptorSetCount() ) )
            return;

        const VkDescriptorSet descSet = _pDevice->registeredDescriptorSetAt( index );
        if ( descSet == VK_NULL_HANDLE )
            return;

        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, 0, 1, &descSet, 0, nullptr );
    }

    void VulkanRHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        (void)slot;
        _pState->_boundMeshVb     = buffer;
        _pState->_boundMeshStride = stride > 0 ? stride : static_cast<uint32>( sizeof( RHIVertex ) );
        _pState->_boundMeshOffset = offset;
    }

    void VulkanRHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
    {
        _pState->_boundIndexBuffer = buffer;
        _pState->_boundIndexStride = ( indexStride == 2 ) ? 2u : 4u;
        _pState->_boundIndexOffset = offset;
    }

    void VulkanRHICommandContext::bindGraphicsMaterialSets( RHIDescriptorIndex cbDescriptorIndex )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pDevice->_pipelineLayout == VK_NULL_HANDLE )
            return;

        const bool bValidDescriptor =
            ( cbDescriptorIndex != kInvalidDescriptorIndex &&
              _pDevice->registeredDescriptorSetAt( cbDescriptorIndex ) != VK_NULL_HANDLE );

        // set 0 binding 0 = PassCB. 유효한 인덱스가 있을 때만 (재)바인딩한다.
        // ShaderBindingBinder::bindGraphics 가 draw/drawIndirect 호출 전에 이미 bindConstantBuffer(slot0)
        // 로 실제 PassCB 를 set 0 에 바인딩해 두므로, 여기서 유효하지 않은 인덱스로 더미 UBO 폴백을
        // 강제로 덮어쓰면 방금 바인딩한 값이 지워진다(과거 draw()가 CB 바인딩을 직접 겸하던 구조의 잔재).
        // 인덱스가 없을 땐 현재 바인딩을 그대로 두어 이 문제를 피한다.
        // 같은 세트를 이미 바인딩해 뒀으면(연속 드로우가 흔히 그렇다) 재호출을 스킵한다 — 캐시는
        // beginFrame()/beginOffscreenPass()가 새 커맨드버퍼를 열 때 초기화한다.
        if ( bValidDescriptor )
        {
            const VkDescriptorSet set0 = _pDevice->registeredDescriptorSetAt( cbDescriptorIndex );
            if ( set0 != _pState->_lastBoundGraphicsSet0 )
            {
                vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 0, 1, &set0, 0, nullptr );
                _pState->_lastBoundGraphicsSet0 = set0;
            }
        }

        // set 1(bindless 텍스처)·set 4(정적 샘플러)는 이 커맨드버퍼가 살아있는 동안 안 바뀌므로 한 번만.
        if ( _pState->_bStaticGraphicsSetsBound == false )
        {
            if ( _pDevice->_bindlessTextureSet != VK_NULL_HANDLE )
                vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 1, 1, &_pDevice->_bindlessTextureSet, 0, nullptr );

            // binding.hlsli 가 모든 셰이더에서 정적으로 참조하므로(없으면 vkCmdDraw 검증 오류) 필요.
            if ( _pDevice->_staticSamplerSet != VK_NULL_HANDLE )
                vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 4, 1, &_pDevice->_staticSamplerSet, 0, nullptr );

            _pState->_bStaticGraphicsSetsBound = true;
        }
    }

    void VulkanRHICommandContext::bindMeshVertexBufferOrFallback()
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;

        if ( _pState->_boundMeshVb != 0 )
        {
            const VulkanRHIDevice::VulkanBufferRecord* pVb = _pDevice->resolveAllocatedBuffer( _pState->_boundMeshVb );
            if ( pVb != nullptr && pVb->_buffer != VK_NULL_HANDLE )
            {
                VkBuffer     arrVertexBuffer[] = { pVb->_buffer };
                VkDeviceSize arrOffset[]       = { static_cast<VkDeviceSize>( _pState->_boundMeshOffset ) };
                vkCmdBindVertexBuffers( cmd, 0, 1, arrVertexBuffer, arrOffset );
            }
        }
        else if ( _pDevice->_vertexBuffer != VK_NULL_HANDLE )
        {
            VkBuffer     arrVertexBuffer[] = { _pDevice->_vertexBuffer };
            VkDeviceSize arrOffset[]       = { 0 };
            vkCmdBindVertexBuffers( cmd, 0, 1, arrVertexBuffer, arrOffset );
        }
    }

    bool VulkanRHICommandContext::bindActiveGraphicsPipeline()
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return false;

        VkPipeline                                        pipeline = _pDevice->_pipeline;
        const VulkanRHIDevice::VulkanPipelineStateRecord* pRecord  = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPso );
        if ( pRecord != nullptr )
        {
            if ( pRecord->_pipeline != VK_NULL_HANDLE )
                pipeline = pRecord->_pipeline;
        }
        else if ( _pState->_activeOffscreenTarget != 0 && _pDevice->_offscreenPipeline != VK_NULL_HANDLE )
            pipeline = _pDevice->_offscreenPipeline;

        if ( pipeline == VK_NULL_HANDLE )
            return false;

        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
        return true;
    }

    void VulkanRHICommandContext::draw( uint32 vertexCount, uint32 startVertex,
                                        RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex )
    {
        VkCommandBuffer cmd = commandBuffer();
        const bool      bCanDraw =
            ( cmd != VK_NULL_HANDLE && _pDevice->_pipelineLayout != VK_NULL_HANDLE && vertexCount > 0 );
        if ( bCanDraw == false )
            return;

        if ( bindActiveGraphicsPipeline() == false )
            return;

        bindGraphicsMaterialSets( passCbDescriptorIndex );

        if ( materialCbDescriptorIndex != kInvalidDescriptorIndex )
        {
            constexpr VkShaderStageFlags kPushStages =
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
            uint32 matIndex = materialCbDescriptorIndex;
            vkCmdPushConstants( cmd, _pDevice->_pipelineLayout, kPushStages, 0, sizeof( uint32 ), &matIndex );
        }

        bindMeshVertexBufferOrFallback();

        vkCmdDraw( cmd, vertexCount, 1, startVertex, 0 );
    }

    void VulkanRHICommandContext::drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        VkCommandBuffer cmd = commandBuffer();
        const bool      bCanDraw =
            ( cmd != VK_NULL_HANDLE && _pDevice->_pipelineLayout != VK_NULL_HANDLE && vertexCount > 0 && instanceCount > 0 );
        if ( bCanDraw == false )
            return;

        if ( bindActiveGraphicsPipeline() == false )
            return;

        bindMeshVertexBufferOrFallback();

        vkCmdDraw( cmd, vertexCount, instanceCount, startVertex, startInstance );
    }

    void VulkanRHICommandContext::bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot )
    {
        // 현재 파이프라인 레이아웃: b0(PassCB) = set 0, b1(MaterialCB) = push constant. b2+ 미지원.
        if ( slot == 0 )
        {
            bindGraphicsMaterialSets( cb );
            return;
        }
        if ( slot == 1 )
        {
            VkCommandBuffer cmd = commandBuffer();
            if ( cmd == VK_NULL_HANDLE || _pDevice->_pipelineLayout == VK_NULL_HANDLE || cb == kInvalidDescriptorIndex )
                return;
            constexpr VkShaderStageFlags kPushStages =
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
            uint32 matIndex = cb;
            vkCmdPushConstants( cmd, _pDevice->_pipelineLayout, kPushStages, 0, sizeof( uint32 ), &matIndex );
            return;
        }
        SW_LOG_TRACE( "bindConstantBuffer: 슬롯 b%# 는 현재 파이프라인 레이아웃에서 지원하지 않습니다.", slot );
    }

    void VulkanRHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 VS/PS 가 읽는 구조버퍼(SwInstanceData 등). registerBindlessResource 가 만든
        // STORAGE_BUFFER 디스크립터셋을 파이프라인 레이아웃 set 6+slot 에 바인딩한다
        // (set 6..9 = _uavDescriptorSetLayout). HLSL: register(t#, space6).
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pDevice->_pipelineLayout == VK_NULL_HANDLE || slot >= 4 )
            return;
        if ( index == kInvalidDescriptorIndex ||
             index >= static_cast<RHIDescriptorIndex>( _pDevice->registeredDescriptorSetCount() ) )
            return;
        const VkDescriptorSet descSet = _pDevice->registeredDescriptorSetAt( index );
        if ( descSet == VK_NULL_HANDLE )
            return;
        vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout,
                                 6 + slot, 1, &descSet, 0, nullptr );
    }

    void VulkanRHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;

        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        vkCmdDispatch( cmd, threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void VulkanRHICommandContext::setViewport( const RHIViewport& viewport )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;

        // RHIViewport is DirectX-style (top-left origin, +Y down in pixel space, +NDC Y = up).
        // Negative VkViewport.height maps Vulkan NDC to the same orientation as DX/GL_UPPER_LEFT.
        VkViewport vkViewport{};
        vkViewport.x        = viewport._x;
        vkViewport.y        = viewport._y + viewport._height;
        vkViewport.width    = viewport._width;
        vkViewport.height   = -viewport._height;
        vkViewport.minDepth = viewport._minDepth;
        vkViewport.maxDepth = viewport._maxDepth;
        vkCmdSetViewport( cmd, 0, 1, &vkViewport );

        VkRect2D scissor{};
        scissor.offset.x      = static_cast<int32>( viewport._x );
        scissor.offset.y      = static_cast<int32>( viewport._y );
        scissor.extent.width  = viewport._width > 0.0f ? static_cast<uint32>( viewport._width ) : 0u;
        scissor.extent.height = viewport._height > 0.0f ? static_cast<uint32>( viewport._height ) : 0u;
        vkCmdSetScissor( cmd, 0, 1, &scissor );
    }

    void VulkanRHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        (void)rootParameterIndex;
        VkCommandBuffer cmd = commandBuffer();
        const bool      bCanPush =
            ( cmd != VK_NULL_HANDLE && _pDevice->_pipelineLayout != VK_NULL_HANDLE && pData != nullptr && num32BitValues > 0 );
        if ( bCanPush == false )
            return;

        constexpr VkShaderStageFlags kPushStages =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        vkCmdPushConstants( cmd, _pDevice->_pipelineLayout, kPushStages, destOffsetIn32BitValues * 4, num32BitValues * 4, pData );
    }

    void VulkanRHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
                                                RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex )
    {
        VkCommandBuffer                            cmd     = commandBuffer();
        const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( argumentBuffer );
        if ( cmd == VK_NULL_HANDLE || pRecord == nullptr )
            return;

        if ( bindActiveGraphicsPipeline() == false )
            return;

        bindGraphicsMaterialSets( passCbDescriptorIndex );

        if ( materialCbDescriptorIndex != kInvalidDescriptorIndex && _pDevice->_pipelineLayout != VK_NULL_HANDLE )
        {
            uint32                       matIndex = materialCbDescriptorIndex;
            constexpr VkShaderStageFlags kPushStages =
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
            vkCmdPushConstants( cmd, _pDevice->_pipelineLayout, kPushStages, 0, sizeof( uint32 ), &matIndex );
        }

        if ( pRecord->_buffer != VK_NULL_HANDLE )
        {
            bindMeshVertexBufferOrFallback();
            vkCmdDrawIndirect( cmd, pRecord->_buffer, argumentBufferOffset, 1, sizeof( VkDrawIndirectCommand ) );
        }
    }

    void VulkanRHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        VkCommandBuffer                            cmd   = commandBuffer();
        const VulkanRHIDevice::VulkanBufferRecord* pArgs = _pDevice->resolveAllocatedBuffer( argumentBuffer );
        const VulkanRHIDevice::VulkanBufferRecord* pIb   = _pDevice->resolveAllocatedBuffer( _pState->_boundIndexBuffer );
        const bool                                 bValidArgs =
            ( cmd != VK_NULL_HANDLE && pArgs != nullptr && pIb != nullptr && pArgs->_buffer != VK_NULL_HANDLE && pIb->_buffer != VK_NULL_HANDLE );
        if ( bValidArgs == false )
            return;

        if ( bindActiveGraphicsPipeline() == false )
            return;

        // 셰이더가 set 0 을 정적으로 참조하므로 인다이렉트 드로우에서도 바인딩해야 한다(머티리얼 인덱스는 인스턴스 버퍼에서 온다).
        bindGraphicsMaterialSets( kInvalidDescriptorIndex );

        const VulkanRHIDevice::VulkanBufferRecord* pVb = _pDevice->resolveAllocatedBuffer( _pState->_boundMeshVb );
        if ( pVb != nullptr )
        {
            VkBuffer     arrVertexBuffer[] = { pVb->_buffer };
            VkDeviceSize arrOffset[]       = { static_cast<VkDeviceSize>( _pState->_boundMeshOffset ) };
            vkCmdBindVertexBuffers( cmd, 0, 1, arrVertexBuffer, arrOffset );
        }

        const VkIndexType indexType = ( _pState->_boundIndexStride == 2 ) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer( cmd, pIb->_buffer, _pState->_boundIndexOffset, indexType );
        vkCmdDrawIndexedIndirect( cmd, pArgs->_buffer, argumentBufferOffset, 1, sizeof( VkDrawIndexedIndirectCommand ) );
    }

    void VulkanRHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        VkCommandBuffer                            cmd     = commandBuffer();
        const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( argumentBuffer );
        if ( cmd == VK_NULL_HANDLE || pRecord == nullptr )
            return;

        if ( pRecord->_buffer != VK_NULL_HANDLE )
            vkCmdDispatchIndirect( cmd, pRecord->_buffer, argumentBufferOffset );
    }

    void VulkanRHICommandContext::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                                     RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        VkCommandBuffer                            cmd   = commandBuffer();
        const VulkanRHIDevice::VulkanBufferRecord* pArgs = _pDevice->resolveAllocatedBuffer( argumentBuffer );
        const bool                                 bValidMultiArgs =
            ( cmd != VK_NULL_HANDLE && pArgs != nullptr && pArgs->_buffer != VK_NULL_HANDLE && maxCommandCount > 0 );
        if ( bValidMultiArgs == false )
            return;

        if ( bindActiveGraphicsPipeline() == false )
            return;

        // 셰이더의 set 0 정적 참조를 만족시킨다(머티리얼은 GPU 인스턴스 데이터에서 인덱싱).
        bindGraphicsMaterialSets( kInvalidDescriptorIndex );

        constexpr uint32 stride = sizeof( RHIDrawIndirectCommand );

        if ( countBuffer != 0 && _pDevice->_bDrawIndirectCount != 0 )
        {
            const VulkanRHIDevice::VulkanBufferRecord* pCountRec = _pDevice->resolveAllocatedBuffer( countBuffer );
            if ( pCountRec != nullptr )
            {
                if ( pCountRec->_buffer != VK_NULL_HANDLE )
                {
                    vkCmdDrawIndirectCount( cmd, pArgs->_buffer, argumentBufferOffset, pCountRec->_buffer, countBufferOffset,
                                            maxCommandCount, stride );
                    return;
                }
            }
        }

        if ( _pDevice->_bMultiDrawIndirect != 0 && maxCommandCount > 1 )
        {
            vkCmdDrawIndirect( cmd, pArgs->_buffer, argumentBufferOffset, maxCommandCount, stride );
            return;
        }

        for ( uint32 commandIndex = 0; commandIndex < maxCommandCount; ++commandIndex )
        {
            vkCmdDrawIndirect( cmd, pArgs->_buffer, argumentBufferOffset + commandIndex * stride, 1, stride );
        }
    }

    void VulkanRHICommandContext::beginEventMarker( const utf8* pName )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || pName == nullptr || _pDevice->_instance == VK_NULL_HANDLE )
            return;

        PFN_vkCmdBeginDebugUtilsLabelEXT pFn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr( _pDevice->_instance, "vkCmdBeginDebugUtilsLabelEXT" ) );
        if ( pFn == nullptr )
            return;

        VkDebugUtilsLabelEXT label{};
        label.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = pName;
        pFn( cmd, &label );
    }

    void VulkanRHICommandContext::endEventMarker()
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pDevice->_instance == VK_NULL_HANDLE )
            return;

        PFN_vkCmdEndDebugUtilsLabelEXT pFn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr( _pDevice->_instance, "vkCmdEndDebugUtilsLabelEXT" ) );
        if ( pFn != nullptr )
            pFn( cmd );
    }
} // namespace sw
