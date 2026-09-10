#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#include <vulkan/vulkan.h>

namespace sw
{
    namespace
    {
        /**
         * @brief 호출자가 준 슬롯을 레지스터 번호로 정규화합니다.
         * @details 엔진 바인더는 리플렉션의 `_registerIndex` 를 그대로 넘기는데, Vulkan 리플렉션에서 그 값은 세트 0 의 **binding**
         *          (레지스터 + 종류별 시프트, bindingslots.hlsli 6)이다. 명시 호출(bindComputeUAV( idx, 0 ) 등)은 레지스터를 준다.
         *          t 밴드(16..31)·u 밴드(32..47)는 레지스터 범위와 겹치지 않으므로 둘 다 받아 레지스터로 되돌린다.
         */
        uint32 toRegister( uint32 slot, uint32 bandShift )
        {
            if ( bandShift > 0 && slot >= bandShift && slot < bandShift + shaderslot::vk::kBandWidth )
                return slot - bandShift;
            return slot;
        }
    } // namespace

    VulkanRHICommandContext::VulkanRHICommandContext( VulkanRHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _targetBuffer{ nullptr }
        , _pState{ pDevice != nullptr ? &pDevice->_recordingState : nullptr }
    {
    }

    VulkanRHICommandContext::VulkanRHICommandContext( VulkanRHIDevice* pDevice, VkCommandBuffer targetBuffer,
                                                      VulkanRecordingState* pState, VulkanDescriptorPoolSet* pDescriptorPoolSet )
        : _pDevice{ pDevice }
        , _targetBuffer{ targetBuffer }
        , _pState{ pState }
        , _pDescriptorPoolSet{ pDescriptorPoolSet }
    {
    }

    VkCommandBuffer VulkanRHICommandContext::commandBuffer() const
    {
        if ( _targetBuffer != nullptr )
            return _targetBuffer;
        return _pDevice != nullptr ? _pDevice->currentCommandBuffer() : nullptr;
    }

    SW_LOG_CALLER( "Vulkan" );

    static void setVkClearColor( VkClearValue& dst, const float4& clear )
    {
        dst.color.float32[0] = clear._x;
        dst.color.float32[1] = clear._y;
        dst.color.float32[2] = clear._z;
        dst.color.float32[3] = clear._w;
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
        _pDevice->transitionTextureLayout( cmd, srcRec, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           VK_IMAGE_ASPECT_COLOR_BIT );

        VkImage dstImage = VK_NULL_HANDLE;
        uint32  dstW     = _pDevice->_swapChain.getExtentWidth();
        uint32  dstH     = _pDevice->_swapChain.getExtentHeight();

        if ( dst == 0 )
        {
            if ( _pDevice->_swapChain.getCurrentImage() == VK_NULL_HANDLE )
                return;
            dstImage = _pDevice->_swapChain.getCurrentImage();
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
            _pDevice->transitionTextureLayout( cmd, *pDstResolved, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                               VK_IMAGE_ASPECT_COLOR_BIT );
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
            VulkanRHIDevice::VulkanTextureRecord* pDstResolved = _pDevice->resolveTexture( dst );
            if ( pDstResolved != nullptr )
                _pDevice->transitionTextureLayout( cmd, *pDstResolved, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                   VK_IMAGE_ASPECT_COLOR_BIT );
        }
    }

    void VulkanRHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 t# → 슬롯 세트의 t 밴드. 텍스처 슬롯(t0..t3, 에뮬 전용)은 여기로 오지 않는다 —
        // FrameRenderer 가 supportsNativeBindlessSampling() 이면 건너뛴다.
        slot = toRegister( slot, shaderslot::vk::kTShift );
        if ( slot >= shaderslot::kSrvSlotCount )
            return;
        setSlot( false, shaderslot::vk::kTShift + slot, index, false, false );
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
        const uint32                          aspect       = ( record._bDepthStencil != 0 )
                                                               ? _pDevice->depthAspectMask()
                                                               : static_cast<uint32>( VK_IMAGE_ASPECT_COLOR_BIT );
        _pDevice->transitionTextureLayout( cmd, record, targetLayout, aspect );
    }

    void VulkanRHICommandContext::prepareTextureForUnorderedAccess( RHITextureHandle texture )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || texture == 0 )
            return;
        VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
        if ( pResolved == nullptr || pResolved->_image == VK_NULL_HANDLE || pResolved->_bDepthStencil != 0 )
            return;

        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }
        // 스토리지 이미지 디스크립터(writeBindlessStorageImageSlot)가 GENERAL 을 적어 두므로 같은 레이아웃으로 옮긴다.
        _pDevice->transitionTextureLayout( cmd, *pResolved, static_cast<uint32>( VK_IMAGE_LAYOUT_GENERAL ), static_cast<uint32>( VK_IMAGE_ASPECT_COLOR_BIT ) );
    }

    void VulkanRHICommandContext::prepareTextureForRenderTarget( RHITextureHandle texture )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || texture == 0 )
            return; // 스왑체인(0)은 렌더패스의 initialLayout 이 담당한다.

        VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
        if ( pResolved == nullptr || pResolved->_image == VK_NULL_HANDLE )
            return;

        // 렌더패스를 열어 둔 채로 레이아웃을 바꿀 수 없다.
        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        const bool   bDepth       = pResolved->_bDepthStencil != 0;
        const uint32 targetLayout = bDepth ? static_cast<uint32>( VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL )
                                           : static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        const uint32 aspect       = bDepth ? _pDevice->depthAspectMask()
                                           : static_cast<uint32>( VK_IMAGE_ASPECT_COLOR_BIT );
        _pDevice->transitionTextureLayout( cmd, *pResolved, targetLayout, aspect );
    }

    void VulkanRHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;

        // PSO 가 바뀌면 그래픽스 슬롯 상태를 비운다 — 이전 패스가 건 t/u 슬롯이 다음 세트로 새지 않게(언리얼의 파이프라인별 상태).
        _pState->_arrSlotState[0]                                 = VulkanSlotState{};
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

        _pState->_arrSlotState[1]                                 = VulkanSlotState{}; // 컴퓨트 슬롯 상태도 PSO 단위
        const VulkanRHIDevice::VulkanPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord != nullptr && pRecord->_pipeline != VK_NULL_HANDLE )
            vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pRecord->_pipeline );
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

        // Composite FB for MRT, color+depth, or depth-only. Keep plain single-RT / swapchain path otherwise.
        const bool bUseComposite = ( colorCount > 1 ) || ( colorCount == 1 && colorHandles[0] != 0 && bHasDepth ) ||
                                   ( colorCount == 0 && bHasDepth );

        VkRenderPass  renderPass  = _pDevice->_renderPass;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D    extent{ _pDevice->_swapChain.getExtentWidth(), _pDevice->_swapChain.getExtentHeight() };
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
                _pDevice->transitionTextureLayout( cmd, *pTex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, aspect );
            }
            if ( key._depth != 0 )
            {
                VulkanRHIDevice::VulkanTextureRecord* pTex = _pDevice->resolveTexture( key._depth );
                if ( pTex == nullptr )
                    return;
                const uint32 aspect = _pDevice->depthAspectMask();
                _pDevice->transitionTextureLayout( cmd, *pTex, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect );
            }

            VulkanRHIDevice::CompositeFbRecord composite{};
            if ( _pDevice->ensureCompositeFramebuffer( key, composite ) == false )
                return;

            renderPass                   = composite._renderPass;
            framebuffer                  = composite._framebuffer;
            extent                       = { composite._width, composite._height };
            _pState->_bActiveSwapchainRT = 0;

            for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
            {
                setVkClearColor( clearValues[clearCount++], beginInfo._arrClearColor[colorIndex] );
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
                _pDevice->transitionTextureLayout( cmd, *pTex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, aspect );
                renderPass                   = pTex->_renderPass;
                framebuffer                  = pTex->_framebuffer;
                extent                       = { pTex->_width, pTex->_height };
                _pState->_bActiveSwapchainRT = 0;
            }
            else
            {
                if ( _pDevice->_renderPass == VK_NULL_HANDLE || _pDevice->_swapChain.getCurrentFramebuffer() == VK_NULL_HANDLE )
                    return;
                framebuffer                  = _pDevice->_swapChain.getCurrentFramebuffer();
                extent                       = { _pDevice->_swapChain.getExtentWidth(), _pDevice->_swapChain.getExtentHeight() };
                _pState->_bActiveSwapchainRT = 1;
                // 스왑체인 렌더패스는 loadOp 이 렌더패스 객체에 박혀 있어 begin 시점에 못 고른다 —
                // 요청된 loadOp 에 맞는 변종을 고른다. Load 인데 CLEAR 변종을 쓰면 앞 패스가 백버퍼에
                // 그린 내용이 지워진다.
                if ( beginInfo._arrLoadOp[0] == RHIRenderPassLoadOp::Load && _pDevice->_renderPassLoad != VK_NULL_HANDLE )
                    renderPass = _pDevice->_renderPassLoad;
            }

            if ( _pState->_bRenderPassActive == SW_TRUE )
            {
                vkCmdEndRenderPass( cmd );
                _pState->_bRenderPassActive = SW_FALSE;
            }

            setVkClearColor( clearValues[0], beginInfo._arrClearColor[0] );
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
        RHIViewport viewport{};
        viewport._width    = static_cast<float32>( extent.width );
        viewport._height   = static_cast<float32>( extent.height );
        viewport._minDepth = 0.0f;
        viewport._maxDepth = 1.0f;
        setViewport( viewport );
    }

    void VulkanRHICommandContext::endRenderPass()
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pState->_bRenderPassActive == SW_FALSE )
            return;
        vkCmdEndRenderPass( cmd );
        _pState->_bRenderPassActive = SW_FALSE;
    }

    void VulkanRHICommandContext::uavBarrier( RHIBufferHandle buffer )
    {
        VkCommandBuffer                      cmd     = commandBuffer();
        VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
        if ( cmd == VK_NULL_HANDLE || pRecord == nullptr || pRecord->_buffer == VK_NULL_HANDLE )
            return;

        if ( _pState->_bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pState->_bRenderPassActive = SW_FALSE;
        }

        // 컴퓨트 쓰기 → 컴퓨트 읽기·쓰기. 상태는 그대로라 레이아웃 전이가 아니라 **가시성**만 맞춘다.
        VkBufferMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = pRecord->_buffer;
        barrier.offset              = 0;
        barrier.size                = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                              &barrier, 0, nullptr );
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
        // 컴퓨트 u# → 슬롯 세트의 u 밴드. 인덱스는 UAV 레지스트리(registerBindlessUAV)의 것.
        slot = toRegister( slot, shaderslot::vk::kUShift );
        if ( slot >= shaderslot::kComputeUavSlotCount )
            return;
        setSlot( true, shaderslot::vk::kUShift + slot, index, true, false );
    }

    void VulkanRHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        slot = toRegister( slot, shaderslot::vk::kTShift );
        if ( slot >= shaderslot::kSrvSlotCount )
            return;
        setSlot( true, shaderslot::vk::kTShift + slot, index, false, false );
    }

    void VulkanRHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot )
    {
        if ( slot >= shaderslot::kConstantBufferSlotCount )
            return;
        setSlot( true, shaderslot::vk::kBShift + slot, constantBufferIndex, false, true );
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

    void VulkanRHICommandContext::setSlot( bool bCompute, uint32 bindingIndex, RHIDescriptorIndex index, bool bUav, bool bConstantBuffer )
    {
        if ( bindingIndex >= shaderslot::vk::kSlotBindingCount || index == kInvalidDescriptorIndex )
            return;
        const RHIBufferHandle                      handle  = bUav ? _pDevice->uavSourceBufferAt( index ) : _pDevice->bindlessSourceBufferAt( index );
        const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( handle );
        if ( pRecord == nullptr || pRecord->_buffer == VK_NULL_HANDLE )
            return;

        VulkanSlotState&   state = _pState->_arrSlotState[bCompute ? 1 : 0];
        VulkanSlotBinding& slot  = state._arrSlot[bindingIndex];

        // 값이 그대로면 세트를 새로 할당하지 않는다 — 바인더는 드로우마다 같은 버퍼를 다시 건다.
        VulkanSlotBinding candidate{};
        candidate._buffer = pRecord->_buffer;
        candidate._offset = 0;
        candidate._range  = pRecord->_size;
        if ( bConstantBuffer )
        {
            // 링 상수버퍼(createConstantBuffer)는 프레임 슬롯마다 slotSize 만큼 떨어진 자리에 쓴다 — updateConstantBuffer 가
            // 이번 프레임 슬롯에 썼으므로 같은 구간을 건다.
            const auto slotIt = _pDevice->_mapCbSlotSize.find( handle );
            if ( slotIt != _pDevice->_mapCbSlotSize.end() )
            {
                candidate._range  = slotIt->second;
                candidate._offset = static_cast<uint64>( _pDevice->_currentFrame % constant::kMaxFrameCountInFlight ) * slotIt->second;
            }
        }

        const uint64 slotBit     = ( uint64{ 1 } << bindingIndex );
        const bool   bAlreadySet = ( state._slotSetMask & slotBit ) != 0;
        if ( bAlreadySet && slot._buffer == candidate._buffer && slot._offset == candidate._offset && slot._range == candidate._range )
            return; // 같은 값 — 세트를 새로 할당할 이유가 없다.

        slot = candidate;
        state._slotSetMask |= slotBit;
        state._bDirty = 1;
    }

    void VulkanRHICommandContext::flushSlotSet( bool bCompute )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pDevice->_pipelineLayout == VK_NULL_HANDLE )
            return;

        namespace vk       = shaderslot::vk;
        namespace bindless = shaderslot::bindless;

        // 텍스처 배열 세트(set 1) — 커맨드버퍼가 사는 동안 안 바뀐다. 두 바인드 포인트에 한 번씩.
        if ( _pState->_bTextureSetBound == 0 && _pDevice->_textureSet != VK_NULL_HANDLE )
        {
            vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, bindless::kVkTextureSet, 1, &_pDevice->_textureSet, 0, nullptr );
            vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, bindless::kVkTextureSet, 1, &_pDevice->_textureSet, 0, nullptr );
            _pState->_bTextureSetBound = 1;
        }

        VulkanSlotState& state = _pState->_arrSlotState[bCompute ? 1 : 0];
        if ( state._bDirty == 0 )
            return;

        // 슬롯 상태가 바뀌었다 — 이 버퍼의 풀 묶음에서 세트를 하나 받아 걸린 슬롯만 쓴다(언리얼 Vulkan RHI 의 세트 캐시와 같은 자리).
        // 리스트는 자기 쌍의 묶음, 디바이스 프레임 스트림은 링 슬롯의 묶음 — 어느 쪽도 다른 스레드와 나누지 않으므로 락이 없다.
        // b 밴드는 셰이더가 정적으로 참조하므로 안 걸린 자리도 더미 UBO 로 채운다(픽스처의 MaterialCB 등).
        VulkanDescriptorPoolSet& poolSet = ( _pDescriptorPoolSet != nullptr ) ? *_pDescriptorPoolSet : _pDevice->currentFrameDescriptorPoolSet();
        const VkDescriptorSet    set     = _pDevice->allocateSlotSet( poolSet );
        if ( set == VK_NULL_HANDLE )
            return;

        VkDescriptorBufferInfo arrInfo[vk::kSlotBindingCount]{};
        VkWriteDescriptorSet   arrWrite[vk::kSlotBindingCount]{};
        uint32                 writeCount{ 0 };
        for ( uint32 bindingIndex = 0; bindingIndex < vk::kSlotBindingCount; ++bindingIndex )
        {
            const bool bBound   = ( state._slotSetMask & ( uint64{ 1 } << bindingIndex ) ) != 0;
            const bool bUniform = bindingIndex < vk::kTShift;
            if ( bBound == false )
            {
                if ( bUniform == false || bindingIndex >= shaderslot::kConstantBufferSlotCount || _pDevice->_dummyUBO == VK_NULL_HANDLE )
                    continue;
                arrInfo[writeCount].buffer = _pDevice->_dummyUBO;
                arrInfo[writeCount].offset = 0;
                arrInfo[writeCount].range  = 256;
            }
            else
            {
                const VulkanSlotBinding& slot = state._arrSlot[bindingIndex];
                arrInfo[writeCount].buffer    = slot._buffer;
                arrInfo[writeCount].offset    = slot._offset;
                arrInfo[writeCount].range     = slot._range;
            }
            VkWriteDescriptorSet& write = arrWrite[writeCount];
            write.sType                 = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet                = set;
            write.dstBinding            = bindingIndex;
            write.dstArrayElement       = 0;
            write.descriptorType        = bUniform ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.descriptorCount       = 1;
            write.pBufferInfo           = &arrInfo[writeCount];
            ++writeCount;
        }
        if ( writeCount > 0 )
            vkUpdateDescriptorSets( _pDevice->_device, writeCount, arrWrite, 0, nullptr );

        vkCmdBindDescriptorSets( cmd, bCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 0, 1, &set, 0, nullptr );
        state._bDirty = 0;
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
        else if ( _pState->_bActiveSwapchainRT == 0 && _pDevice->_offscreenPipeline != VK_NULL_HANDLE )
        {
            // 등록된 PSO 가 없을 때의 폴백. 판단 기준은 "지금 열린 렌더패스가 백버퍼인가"여야 한다 —
            // 예전엔 _activeOffscreenTarget 으로 판단했는데, 깊이 전용 패스처럼 컬러 타깃을 갱신하지
            // 않는 패스에서는 그 값이 직전 패스의 것이라 엉뚱한 파이프라인을 골랐다.
            pipeline = _pDevice->_offscreenPipeline;
        }

        if ( pipeline == VK_NULL_HANDLE )
            return false;

        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
        flushSlotSet( false );
        return true;
    }

    void VulkanRHICommandContext::draw( uint32 vertexCount, uint32 startVertex )
    {
        VkCommandBuffer cmd = commandBuffer();
        const bool      bCanDraw =
            ( cmd != VK_NULL_HANDLE && _pDevice->_pipelineLayout != VK_NULL_HANDLE && vertexCount > 0 );
        if ( bCanDraw == false )
            return;

        if ( bindActiveGraphicsPipeline() == false )
            return;

        // b0/b1 은 호출자가 bindConstantBuffer( index, shaderslot::k*ConstantBuffer ) 로 슬롯 상태에 걸었다 — 위에서 세트로 굳혔다.
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
        // 슬롯 세트는 드로우 직전에 굳힌다 — draw()/drawIndirect() 와 같은 규칙. 여기만 빠져 있어 인스턴스드 씬 드로우(드로우 전부)가
        // 세트 없이 나갔고, Vulkan 은 화면에 아무것도 그리지 않았다.
        flushSlotSet( false );

        vkCmdDraw( cmd, vertexCount, instanceCount, startVertex, startInstance );
    }

    void VulkanRHICommandContext::bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot )
    {
        // b# → 슬롯 세트의 b 밴드. 링 상수버퍼는 이번 프레임 슬롯 구간을 건다. 세트는 드로우 직전 flushSlotSet 이 굳힌다.
        if ( slot >= shaderslot::kConstantBufferSlotCount )
        {
            SW_LOG_TRACE( "bindConstantBuffer: 슬롯 b%# 는 슬롯 세트의 b 자리 수(%#)를 넘습니다.", slot, shaderslot::kConstantBufferSlotCount );
            return;
        }
        setSlot( false, shaderslot::vk::kBShift + slot, constantBufferIndex, false, true );
    }

    void VulkanRHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 구조버퍼(인스턴스 t4, 머티리얼 데이터 t9 …) — 리플렉션이 준 슬롯의 t 밴드에 건다.
        bindShaderResource( index, slot );
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

        flushSlotSet( true );
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

    void VulkanRHICommandContext::setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        // 푸시 상수 범위는 전 스테이지(VS/PS/CS)에 걸려 있으므로 컴퓨트 경로와 같은 호출이면 된다.
        setComputeRootConstants( rootParameterIndex, num32BitValues, pData, destOffsetIn32BitValues );
    }

    void VulkanRHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        (void)rootParameterIndex;
        VkCommandBuffer cmd = commandBuffer();
        const bool      bCanPush =
            ( cmd != VK_NULL_HANDLE && _pDevice->_pipelineLayout != VK_NULL_HANDLE && pData != nullptr && num32BitValues > 0 );
        if ( bCanPush == false || destOffsetIn32BitValues >= VulkanRHIDevice::kMaxComputeRootConstantDwords )
            return;
        const uint32 maxCount = VulkanRHIDevice::kMaxComputeRootConstantDwords - destOffsetIn32BitValues;
        const uint32 count    = num32BitValues < maxCount ? num32BitValues : maxCount;

        constexpr VkShaderStageFlags kPushStages =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        vkCmdPushConstants( cmd, _pDevice->_pipelineLayout, kPushStages, destOffsetIn32BitValues * 4, count * 4, pData );
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

        // 세트 0 은 커맨드버퍼마다 한 번 — 인다이렉트 드로우 경로도 같은 세트다(머티리얼 인덱스는 인스턴스 버퍼에서 온다).
        flushSlotSet( false );

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
        {
            flushSlotSet( true );
            vkCmdDispatchIndirect( cmd, pRecord->_buffer, argumentBufferOffset );
        }
    }

    void VulkanRHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount,
                                                RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        VkCommandBuffer                            cmd   = commandBuffer();
        const VulkanRHIDevice::VulkanBufferRecord* pArgs = _pDevice->resolveAllocatedBuffer( argumentBuffer );
        if ( cmd == VK_NULL_HANDLE || pArgs == nullptr || pArgs->_buffer == VK_NULL_HANDLE || drawCount == 0 )
            return;

        if ( bindActiveGraphicsPipeline() == false )
            return;

        // 세트 0 은 커맨드버퍼마다 한 번(머티리얼은 GPU 인스턴스 데이터에서 인덱싱).
        flushSlotSet( false );
        // 정점버퍼를 거는 건 단일 경로에만 있었다 — 합치면서 두 경우 모두 걸린다.
        bindMeshVertexBufferOrFallback();

        constexpr uint32 stride = sizeof( RHIDrawIndirectCommand );

        if ( countBuffer != 0 && _pDevice->_bDrawIndirectCount != 0 )
        {
            const VulkanRHIDevice::VulkanBufferRecord* pCountRec = _pDevice->resolveAllocatedBuffer( countBuffer );
            if ( pCountRec != nullptr )
            {
                if ( pCountRec->_buffer != VK_NULL_HANDLE )
                {
                    vkCmdDrawIndirectCount( cmd, pArgs->_buffer, argumentBufferOffset, pCountRec->_buffer, countBufferOffset,
                                            drawCount, stride );
                    return;
                }
            }
        }

        // 한 번만 그리거나 멀티를 지원하면 호출 하나로 끝난다. 아니면 하나씩 나눠 부른다.
        if ( drawCount == 1 || _pDevice->_bMultiDrawIndirect != 0 )
        {
            vkCmdDrawIndirect( cmd, pArgs->_buffer, argumentBufferOffset, drawCount, stride );
            return;
        }

        for ( uint32 commandIndex = 0; commandIndex < drawCount; ++commandIndex )
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
