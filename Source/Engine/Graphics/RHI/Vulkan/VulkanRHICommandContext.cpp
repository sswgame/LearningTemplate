#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Foundation/PlatformOSHeaders.h"

#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Profiling/FrameProfiler.h"

#include <vulkan/vulkan.h>

namespace sw
{
    IRHIDevice& VulkanRHICommandContext::getContextDevice() const
    {
        return *_pDevice;
    }

    namespace
    {
        void setVkClearColor( VkClearValue& dst, const float4& clear )
        {
            dst.color.float32[0] = clear._x;
            dst.color.float32[1] = clear._y;
            dst.color.float32[2] = clear._z;
            dst.color.float32[3] = clear._w;
        }

        void mapStateVal( RHIBufferState state, VkAccessFlags& access, VkPipelineStageFlags& stage )
        {
            switch ( state )
            {
                case RHIBufferState::UnorderedAccess:
                {
                    access = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
                    stage  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    break;
                }
                case RHIBufferState::ShaderResource:
                {
                    access = VK_ACCESS_SHADER_READ_BIT;
                    stage  = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;
                }
                case RHIBufferState::IndirectArgument:
                {
                    access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                    stage  = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
                    break;
                }
                case RHIBufferState::CopyDest:
                {
                    access = VK_ACCESS_TRANSFER_WRITE_BIT;
                    stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    break;
                }
                case RHIBufferState::CopySource:
                {
                    access = VK_ACCESS_TRANSFER_READ_BIT;
                    stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    break;
                }
                case RHIBufferState::VertexOrConstant:
                {
                    access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
                    // 컴퓨트가 읽는 상수 버퍼(`bindComputeConstantBuffer`)도 이 상태다 — DX12 의 VERTEX_AND_CONSTANT_BUFFER 가 모든 스테이지를 덮는 것과 같게 컴퓨트 스테이지를 더한다.
                    stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;
                }
                case RHIBufferState::Index:
                {
                    access = VK_ACCESS_INDEX_READ_BIT;
                    stage  = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                    break;
                }
                case RHIBufferState::Common:
                {
                    access = 0;
                    stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    break;
                }
            }
        }
    } // namespace

    VulkanRHICommandContext::VulkanRHICommandContext( VulkanRHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _targetBuffer{ nullptr }
        , _pState{ pDevice != nullptr ? &pDevice->_recordingState : nullptr }
        , _pDescriptorPoolSet{ nullptr }
        , _lastEventMarker{}
        , _breadcrumbSlotBase{ 0 }
    {
    }

    VulkanRHICommandContext::VulkanRHICommandContext( VulkanRHIDevice* pDevice, VkCommandBuffer targetBuffer,
                                                      VulkanRecordingState* pState, VulkanDescriptorPoolSet* pDescriptorPoolSet )
        : _pDevice{ pDevice }
        , _targetBuffer{ targetBuffer }
        , _pState{ pState }
        , _pDescriptorPoolSet{ pDescriptorPoolSet }
        , _lastEventMarker{}
        , _breadcrumbSlotBase{ 0 }
    {
    }

    VkCommandBuffer VulkanRHICommandContext::commandBuffer() const
    {
        if ( _targetBuffer != nullptr )
            return _targetBuffer;
        return _pDevice != nullptr ? _pDevice->currentCommandBuffer() : nullptr;
    }

    void VulkanRHICommandContext::blitTextureNative( RHITextureHandle src, RHITextureHandle dst )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || ( src == kRHIBackBufferTexture && dst == kRHIBackBufferTexture ) )
            return;

        if ( src == kRHIBackBufferTexture )
        {
            // 백버퍼 → 텍스처. 스왑체인 이미지는 PRESENT_SRC 로 끝나 있다(Present 패스 · 캡처 블릿). 블릿이라 포맷(BGRA ↔ RGBA)을 바꿔 준다.
            const VkImage                         backImage              = _pDevice->_swapChain.getCurrentImage();
            VulkanRHIDevice::VulkanTextureRecord* pDstResolved           = _pDevice->resolveTexture( dst );
            const bool                            bInvalidBackBufferBlit = backImage == VK_NULL_HANDLE || pDstResolved == nullptr || pDstResolved->_image == VK_NULL_HANDLE;
            if ( bInvalidBackBufferBlit )
                return;
            _pDevice->transitionImageLayout( cmd, backImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT );
            _pDevice->transitionTextureLayout( cmd, *pDstResolved, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT );
            VkImageBlit backBlit{};
            backBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            backBlit.srcSubresource.layerCount = 1;
            backBlit.srcOffsets[1]             = { static_cast<int32>( _pDevice->_swapChain.getExtentWidth() ), static_cast<int32>( _pDevice->_swapChain.getExtentHeight() ), 1 };
            backBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            backBlit.dstSubresource.layerCount = 1;
            backBlit.dstOffsets[1]             = { static_cast<int32>( pDstResolved->_width ), static_cast<int32>( pDstResolved->_height ), 1 };
            vkCmdBlitImage( cmd, backImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pDstResolved->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &backBlit,
                            VK_FILTER_NEAREST );
            _pDevice->transitionImageLayout( cmd, backImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT );
            _pDevice->transitionTextureLayout( cmd, *pDstResolved, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT );
            return;
        }

        VulkanRHIDevice::VulkanTextureRecord* pSrcResolved          = _pDevice->resolveTexture( src );
        const bool                            bInvalidSourceTexture = pSrcResolved == nullptr || pSrcResolved->_image == VK_NULL_HANDLE || pSrcResolved->_bDepthStencil != SW_FALSE;
        if ( bInvalidSourceTexture )
            return;

        VulkanRHIDevice::VulkanTextureRecord& srcRecord = *pSrcResolved;
        _pDevice->transitionTextureLayout( cmd, srcRecord, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           VK_IMAGE_ASPECT_COLOR_BIT );

        VkImage dstImage = VK_NULL_HANDLE;
        uint32  dstW     = _pDevice->_swapChain.getExtentWidth();
        uint32  dstH     = _pDevice->_swapChain.getExtentHeight();

        if ( dst == kRHIBackBufferTexture )
        {
            if ( _pDevice->_swapChain.getCurrentImage() == VK_NULL_HANDLE )
                return;
            dstImage = _pDevice->_swapChain.getCurrentImage();
            // 이전 레이아웃은 UNDEFINED 로 둔다. 블릿이 백버퍼 전체를 덮어쓰므로 내용을 버려도 되고, 갓 만든 스왑체인 이미지
            // (아직 한 번도 present 되지 않아 실제로 UNDEFINED)에도 맞다. PRESENT_SRC 로 못박으면 첫 프레임에 검증 레이어가
            // "PRESENT_SRC 를 기대했는데 UNDEFINED" 를 찍는다(RenderPassGPUTest.FusedPostChainMatchesStaged).
            _pDevice->transitionImageLayout( cmd, dstImage, VK_IMAGE_LAYOUT_UNDEFINED,
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
        blit.srcOffsets[1]             = { static_cast<int32>( srcRecord._width ), static_cast<int32>( srcRecord._height ), 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[1]             = { static_cast<int32>( dstW ), static_cast<int32>( dstH ), 1 };

        vkCmdBlitImage( cmd, srcRecord._image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR );
        // 계약: 원본은 셰이더 읽기로 남는다(`IRHIPassCommandList::blitTexture`).
        _pDevice->transitionTextureLayout( cmd, srcRecord, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT );

        if ( dst == kRHIBackBufferTexture )
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

    // 단일 전이도 묶음과 같은 길(원소 하나)을 지난다 — 상태 표 · 건너뛰기 규칙이 한 벌이다(버퍼의 `transitionBufferNative` 와 같다).
    void VulkanRHICommandContext::prepareTextureForShaderReadNative( RHITextureHandle texture )
    {
        const RHITextureTransition transition{ texture, RHITextureState::ShaderResource };
        transitionTexturesNative( &transition, 1 );
    }

    void VulkanRHICommandContext::prepareTextureForUnorderedAccessNative( RHITextureHandle texture )
    {
        const RHITextureTransition transition{ texture, RHITextureState::UnorderedAccess };
        transitionTexturesNative( &transition, 1 );
    }

    void VulkanRHICommandContext::prepareTextureForRenderTargetNative( RHITextureHandle texture )
    {
        const RHITextureTransition transition{ texture, RHITextureState::RenderTarget };
        transitionTexturesNative( &transition, 1 );
    }

    void VulkanRHICommandContext::transitionTexturesNative( const RHITextureTransition* pTransition, uint32 count )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;
        constexpr uint32                     kRequestChunk = 32;
        VulkanRHIDevice::VulkanLayoutRequest arrRequest[kRequestChunk]{};
        uint32                               requestCount = 0;
        for ( uint32 index = 0; index < count; ++index )
        {
            const RHITextureTransition& transition = pTransition[index];
            // 스왑체인(`kRHIBackBufferTexture`)은 렌더패스의 initialLayout 이 담당한다.
            VulkanRHIDevice::VulkanTextureRecord* const pResolved = transition._texture != kRHIBackBufferTexture ? _pDevice->resolveTexture( transition._texture ) : nullptr;
            if ( pResolved == nullptr || pResolved->_image == VK_NULL_HANDLE )
                continue;
            const bool bDepth = pResolved->_bDepthStencil != SW_FALSE;
            // 스토리지 이미지 디스크립터(writeBindlessStorageImageSlot)가 GENERAL 을 적어 두므로 UAV 는 같은 레이아웃으로 옮긴다. 깊이는 RW 텍스처가 될 수 없다.
            if ( transition._state == RHITextureState::UnorderedAccess && bDepth )
                continue;
            VulkanRHIDevice::VulkanLayoutRequest& request = arrRequest[requestCount];
            request._pRecord                              = pResolved;
            request._aspect                               = bDepth ? _pDevice->depthAspectMask() : static_cast<uint32>( VK_IMAGE_ASPECT_COLOR_BIT );
            request._range                                = transition._range;
            switch ( transition._state )
            {
                case RHITextureState::ShaderResource:
                    request._targetLayout = static_cast<uint32>( bDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
                    break;
                case RHITextureState::RenderTarget:
                    request._targetLayout = static_cast<uint32>( bDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
                    break;
                case RHITextureState::UnorderedAccess:
                    request._targetLayout = static_cast<uint32>( VK_IMAGE_LAYOUT_GENERAL );
                    break;
            }
            if ( ++requestCount == kRequestChunk )
            {
                _pDevice->transitionTextureLayouts( cmd, arrRequest, requestCount );
                requestCount = 0;
            }
        }
        if ( requestCount > 0 )
            _pDevice->transitionTextureLayouts( cmd, arrRequest, requestCount );
    }

    // 길이 유지(긴 함수 정리 기준): 첨부 전이 · 렌더링 정보 채우기 · 시작 · 뷰포트의 순서가 곧 의미라 나누지 않는다.
    RHIRenderPassBeginResult VulkanRHICommandContext::beginRenderPassNative( const RHIRenderPassInfo& beginInfo, RHIRenderPassExtent& outTargetExtent )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return RHIRenderPassBeginResult::Failed;

        const bool   bBindColor = beginInfo._bBindColor != SW_FALSE;
        const uint32 colorCount = bBindColor ? ( beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u ) : 0u;
        const bool   bHasDepth  = beginInfo._depthTarget != 0;

        // 깊이 타깃이 있으면 깊이 전용 패스도 합성 경로를 쓴다.
        if ( bBindColor == 0 && bHasDepth == false )
            return RHIRenderPassBeginResult::Failed;

        RHITextureHandle colorHandles[kMaxColorAttachments]{};
        for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
        {
            colorHandles[attachmentIndex] = beginInfo._arrColorTarget[attachmentIndex];
        }

        // MRT · 컬러+깊이 · 깊이 전용은 합성 프레임버퍼를 쓴다. 그 밖에는 단일 RT · 스왑체인 경로를 그대로 쓴다.
        // 주의: 텍스처 하나짜리 프레임버퍼의 렌더 패스는 지우기(CLEAR)로 고정이다 — 지우지 않는 패스(Load · DontCare)는 load op 을 키로 드는
        // 합성 경로로 보낸다. 그러지 않으면 Load 로 연 패스가 앞 패스의 그림을 지운다(Present 가 그린 캡처 위의 Canvas).
        const bool bSingleTextureKeepsContent = colorCount == 1 && colorHandles[0] != kRHIBackBufferTexture && ( beginInfo._arrLoadOp[0] != RHIRenderPassLoadOp::Clear || beginInfo._arrStoreOp[0] != RHIRenderPassStoreOp::Store );
        const bool bUseComposite              = ( colorCount > 1 ) || ( colorCount == 1 && colorHandles[0] != kRHIBackBufferTexture && bHasDepth ) ||
                                   ( colorCount == 0 && bHasDepth ) || bSingleTextureKeepsContent;

        VkRenderPass  renderPass  = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D    extent{ _pDevice->_swapChain.getExtentWidth(), _pDevice->_swapChain.getExtentHeight() };
        VkClearValue  clearValues[kMaxColorAttachments + 1]{};
        uint32        clearCount{ 0 };

        if ( bUseComposite )
        {
            VulkanRHIRenderPassCache::CompositeKey key{};
            key._colorCount = ( colorCount > kMaxColorAttachments ) ? kMaxColorAttachments : colorCount;
            for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
            {
                key._arrColor[colorIndex]        = colorHandles[colorIndex];
                key._arrColorLoadOp[colorIndex]  = static_cast<uint8>( beginInfo._arrLoadOp[colorIndex] );
                key._arrColorStoreOp[colorIndex] = static_cast<uint8>( beginInfo._arrStoreOp[colorIndex] );
                key._arrColorSlice[colorIndex]   = beginInfo._arrColorTargetSlice[colorIndex];
                key._arrColorMip[colorIndex]     = beginInfo._arrColorTargetMip[colorIndex];
            }
            key._depth        = beginInfo._depthTarget;
            key._depthLoadOp  = static_cast<uint8>( beginInfo._depthLoadOp );
            key._depthStoreOp = static_cast<uint8>( beginInfo._depthStoreOp );
            key._depthSlice   = beginInfo._depthTargetSlice;
            key._depthMip     = beginInfo._depthTargetMip;

            for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
            {
                VulkanRHIDevice::VulkanTextureRecord* pTex = _pDevice->resolveTexture( key._arrColor[colorIndex] );
                if ( pTex == nullptr )
                    return RHIRenderPassBeginResult::Failed;
                constexpr uint32 aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                // 그리는 밉만 옮긴다(면은 전체 — 예전과 같다). 다른 밉을 읽는 다운샘플 체인이 그 밉의 레이아웃을 지킨다(R-10).
                _pDevice->transitionTextureLayout( cmd, *pTex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, aspect, RHISubresourceRange{ key._arrColorMip[colorIndex], 1u, 0u, RHISubresourceRange::kAll } );
            }
            if ( key._depth != 0 )
            {
                VulkanRHIDevice::VulkanTextureRecord* pTex = _pDevice->resolveTexture( key._depth );
                if ( pTex == nullptr )
                    return RHIRenderPassBeginResult::Failed;
                const uint32 aspect = _pDevice->depthAspectMask();
                _pDevice->transitionTextureLayout( cmd, *pTex, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect, RHISubresourceRange{ key._depthMip, 1u, 0u, RHISubresourceRange::kAll } );
            }

            VulkanRHIRenderPassCache::CompositeRecord composite{};
            if ( _pDevice->ensureCompositeFramebuffer( key, composite ) == false )
                return RHIRenderPassBeginResult::Failed;

            renderPass  = composite._renderPass;
            framebuffer = composite._framebuffer;
            extent      = { composite._width, composite._height };

            for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
            {
                setVkClearColor( clearValues[clearCount++], beginInfo._arrClearColor[colorIndex] );
            }
            if ( key._depth != 0 )
            {
                clearValues[clearCount].depthStencil.depth   = beginInfo._clearDepth;
                clearValues[clearCount].depthStencil.stencil = beginInfo._clearStencil;
                ++clearCount;
            }
        }
        else
        {
            // 단일 RT 경로(스왑체인 또는 텍스처별 오프스크린 프레임버퍼).
            RHITextureHandle colorTarget = ( colorCount > 0 ) ? colorHandles[0] : kRHIBackBufferTexture;
            if ( colorTarget != kRHIBackBufferTexture )
            {
                VulkanRHIDevice::VulkanTextureRecord* pTex                      = _pDevice->resolveTexture( colorTarget );
                const uint32                          mip                       = beginInfo._arrColorTargetMip[0];
                const bool                            bColorTargetNotRenderable = pTex == nullptr || pTex->findSliceFramebuffer( beginInfo._arrColorTargetSlice[0], mip ) == VK_NULL_HANDLE || pTex->_renderPass == VK_NULL_HANDLE;
                if ( bColorTargetNotRenderable )
                    return RHIRenderPassBeginResult::Failed;

                constexpr uint32 aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                _pDevice->transitionTextureLayout( cmd, *pTex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, aspect, RHISubresourceRange{ static_cast<uint16>( mip ), 1u, 0u, RHISubresourceRange::kAll } );
                renderPass  = pTex->_renderPass;
                framebuffer = pTex->findSliceFramebuffer( beginInfo._arrColorTargetSlice[0], mip );
                extent      = { MathUtil::max( pTex->_width >> mip, 1u ), MathUtil::max( pTex->_height >> mip, 1u ) };
            }
            else
            {
                const VulkanRHISwapChain* pSwapChain = _pDevice->useSwapChain( beginInfo._pSwapChain );
                if ( pSwapChain == nullptr || pSwapChain->getRenderPass() == VK_NULL_HANDLE || pSwapChain->getCurrentFramebuffer() == VK_NULL_HANDLE )
                {
                    return RHIRenderPassBeginResult::NoBackBuffer; // 그릴 백버퍼가 없다(최소화 · 스왑체인 재생성 중) — 이 패스의 드로우는 알리지 않고 버린다
                }
                framebuffer = pSwapChain->getCurrentFramebuffer();
                extent      = { pSwapChain->getExtentWidth(), pSwapChain->getExtentHeight() };
                // 스왑체인 렌더패스는 loadOp 이 렌더패스 객체에 박혀 있어 begin 시점에 못 고른다.
                // 요청된 loadOp 에 맞는 변종을 고른다. Load 인데 CLEAR 변종을 쓰면 앞 패스가 백버퍼에
                // 그린 내용이 지워진다.
                renderPass = beginInfo._arrLoadOp[0] == RHIRenderPassLoadOp::Load ? pSwapChain->getLoadRenderPass() : pSwapChain->getRenderPass();
            }

            setVkClearColor( clearValues[0], beginInfo._arrClearColor[0] );
            clearCount = 1;
        }

        outTargetExtent                      = RHIRenderPassExtent{ extent.width, extent.height };
        const RHIRenderPassExtent passExtent = computePassExtent( beginInfo, outTargetExtent );

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass        = renderPass;
        renderPassInfo.framebuffer       = framebuffer;
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = VkExtent2D{ passExtent._width, passExtent._height };
        renderPassInfo.clearValueCount   = clearCount;
        renderPassInfo.pClearValues      = clearValues;

        vkCmdBeginRenderPass( cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );
        return RHIRenderPassBeginResult::Begun;
    }

    void VulkanRHICommandContext::endRenderPassNative()
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd != VK_NULL_HANDLE )
            vkCmdEndRenderPass( cmd );
    }

    void VulkanRHICommandContext::beginTextureAliasNative( RHITextureHandle texture )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || texture == 0 )
            return;
        VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
        if ( pResolved == nullptr || pResolved->_image == VK_NULL_HANDLE || pResolved->_heapMemory == nullptr )
            return; // 겹쳐 놓지 않은 텍스처는 같은 바이트를 쓴 앞 이미지가 없다
        const uint32 aspect = pResolved->_bDepthStencil != SW_FALSE ? _pDevice->depthAspectMask() : static_cast<uint32>( VK_IMAGE_ASPECT_COLOR_BIT );
        _pDevice->beginImageAlias( cmd, *pResolved, aspect );
    }

    void VulkanRHICommandContext::uavBarrierNative( RHIBufferHandle buffer )
    {
        VkCommandBuffer                      cmd                  = commandBuffer();
        VulkanRHIDevice::VulkanBufferRecord* pRecord              = _pDevice->resolveAllocatedBuffer( buffer );
        const bool                           bInvalidBufferRecord = cmd == VK_NULL_HANDLE || pRecord == nullptr || pRecord->_buffer == VK_NULL_HANDLE;
        if ( bInvalidBufferRecord )
            return;

        // 컴퓨트 쓰기 → 컴퓨트 읽기 · 쓰기. 상태는 그대로라 레이아웃 전이가 아니라 가시성만 맞춘다.
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

    void VulkanRHICommandContext::uavBarrierTextureNative( RHITextureHandle texture )
    {
        VkCommandBuffer                             cmd       = commandBuffer();
        const VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
        if ( cmd == VK_NULL_HANDLE || pResolved == nullptr || pResolved->_image == VK_NULL_HANDLE || pResolved->_bDepthStencil != SW_FALSE )
            return;
        // RW 텍스처는 GENERAL 그대로다(`transitionTexturesNative` 의 UAV) — 레이아웃 전이가 아니라 앞 쓰기의 가시성만 맞춘다(버퍼 판과 같은 접근 · 단계).
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = pResolved->_image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );
    }

    void VulkanRHICommandContext::transitionBufferNative( RHIBufferHandle buffer, RHIBufferState newState )
    {
        const RHIBufferTransition transition{ buffer, newState };
        transitionBuffersNative( &transition, 1 );
    }

    void VulkanRHICommandContext::transitionBuffersNative( const RHIBufferTransition* pTransition, uint32 count )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;
        // 모은 전이를 vkCmdPipelineBarrier 한 번에 낸다 — 앞 · 뒤 단계는 모든 전이의 합이다(한 버퍼씩 내면 그때마다 파이프라인이 단계 사이를 비운다).
        // https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdPipelineBarrier.html (pBufferMemoryBarriers 여럿, 단계 마스크는 그 합)
        constexpr uint32      kMaxBarrierPerCall = 16;
        VkBufferMemoryBarrier arrBarrier[kMaxBarrierPerCall]{};
        uint32                barrierCount  = 0;
        VkPipelineStageFlags  srcStageSum   = 0;
        VkPipelineStageFlags  dstStageSum   = 0;
        auto                  flushBarriers = [&]()
        {
            if ( barrierCount == 0 )
                return;
            vkCmdPipelineBarrier( cmd, srcStageSum, dstStageSum, 0, 0, nullptr, barrierCount, arrBarrier, 0, nullptr );
            barrierCount = 0;
            srcStageSum  = 0;
            dstStageSum  = 0;
        };
        for ( uint32 index = 0; index < count; ++index )
        {
            const RHIBufferState                 newState = pTransition[index]._state;
            VulkanRHIDevice::VulkanBufferRecord* pRecord  = _pDevice->resolveAllocatedBuffer( pTransition[index]._buffer );
            if ( pRecord == nullptr || pRecord->_buffer == VK_NULL_HANDLE )
                continue;
            // 확인과 기록이 한 번이다 — 같은 레벨의 두 워커가 같은 버퍼를 같은 상태로 옮기면 한쪽만 배리어를 낸다(`RHITrackedState`, DX12 와 같다).
            RHIBufferState stateBefore{};
            if ( pRecord->_state.transitionTo( newState, stateBefore ) == false )
                continue;
            VkAccessFlags        srcAccess{ 0 };
            VkAccessFlags        dstAccess{ 0 };
            VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            mapStateVal( stateBefore, srcAccess, srcStage );
            mapStateVal( newState, dstAccess, dstStage );

            VkBufferMemoryBarrier& barrier = arrBarrier[barrierCount++];
            barrier.sType                  = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.pNext                  = nullptr;
            barrier.srcAccessMask          = srcAccess;
            barrier.dstAccessMask          = dstAccess;
            barrier.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer                 = pRecord->_buffer;
            barrier.offset                 = 0;
            barrier.size                   = VK_WHOLE_SIZE;
            srcStageSum |= srcStage;
            dstStageSum |= dstStage;
            if ( barrierCount == kMaxBarrierPerCall )
                flushBarriers();
        }
        flushBarriers();
    }

    void VulkanRHICommandContext::copyBufferToReadbackNative( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes )
    {
        VkCommandBuffer                      cmd            = commandBuffer();
        VulkanRHIDevice::VulkanBufferRecord* pSource        = _pDevice->resolveAllocatedBuffer( source );
        VulkanRHIDevice::VulkanBufferRecord* pTarget        = _pDevice->resolveAllocatedBuffer( readback );
        const bool                           bNothingToCopy = cmd == VK_NULL_HANDLE || pSource == nullptr || pTarget == nullptr || pSource->_buffer == VK_NULL_HANDLE || pTarget->_buffer == VK_NULL_HANDLE || sizeBytes == 0;
        if ( bNothingToCopy )
            return;
        VkBufferCopy region{};
        region.size = sizeBytes;
        vkCmdCopyBuffer( cmd, pSource->_buffer, pTarget->_buffer, 1, &region );
    }

    uint64 VulkanRHICommandContext::findBufferBytes( RHIBufferHandle buffer ) const
    {
        const VulkanRHIDevice::VulkanBufferRecord* const pRecord = _pDevice->resolveAllocatedBuffer( buffer );
        return pRecord != nullptr && pRecord->_buffer != VK_NULL_HANDLE ? pRecord->_sizeBytes : 0;
    }

    void VulkanRHICommandContext::copyBufferRegionNative( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes )
    {
        VkCommandBuffer                      cmd     = commandBuffer();
        VulkanRHIDevice::VulkanBufferRecord* pSource = _pDevice->resolveAllocatedBuffer( source );
        VulkanRHIDevice::VulkanBufferRecord* pTarget = _pDevice->resolveAllocatedBuffer( destination );
        if ( cmd == VK_NULL_HANDLE || pSource == nullptr || pTarget == nullptr || pSource->_buffer == VK_NULL_HANDLE || pTarget->_buffer == VK_NULL_HANDLE )
            return;
        // 원본 TRANSFER_READ · 도착 TRANSFER_WRITE 배리어는 부르는 쪽의 전이(CopySource · CopyDest)가 걸었다.
        VkBufferCopy region{};
        region.srcOffset = sourceOffset;
        region.dstOffset = destinationOffset;
        region.size      = sizeBytes;
        vkCmdCopyBuffer( cmd, pSource->_buffer, pTarget->_buffer, 1, &region );
    }

    void VulkanRHICommandContext::resolveSlot( VulkanSlotState& state, RHIShaderSlotKind kind, uint32 slot, RHIDescriptorIndex index ) const
    {
        namespace vk              = shaderslot::vk;
        const bool   bUAV         = kind == RHIShaderSlotKind::UnorderedAccess;
        const bool   bConstant    = kind == RHIShaderSlotKind::ConstantBuffer;
        const uint32 bindingIndex = slot + ( bConstant ? vk::kBShift : ( bUAV ? vk::kUShift : vk::kTShift ) );
        // 텍스처 칸(t16..)과 RW 텍스처 칸(u5..)은 에뮬 백엔드의 것이다 — 여기서는 네이티브 bindless 세트(set 1)를 셰이더가 번호로 읽는다.
        if ( ( bUAV && slot >= shaderslot::kComputeUAVSlotCount ) || ( bConstant == false && bUAV == false && slot >= shaderslot::kSRVSlotCount ) )
            return;

        VulkanSlotBinding candidate{};
        if ( index != kInvalidDescriptorIndex )
        {
            const RHIBufferSlice                       source  = bUAV ? RHIBufferSlice{ _pDevice->uavSourceBufferAt( index ), 0, 0 } : _pDevice->bindlessSourceSliceAt( index );
            const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( source._buffer );
            if ( pRecord == nullptr || pRecord->_buffer == VK_NULL_HANDLE )
            {
                _pDevice->getDrawDiagnostics().reportDescriptorNotRegistered( bConstant ? "b" : ( bUAV ? "u" : "t" ), slot, index );
            }
            else
            {
                candidate._buffer = pRecord->_buffer;
                candidate._range  = pRecord->_sizeBytes;
                // 링 상수버퍼(createConstantBuffer)는 프레임 슬롯마다 slotSize 만큼 떨어진 자리에 쓴다. updateConstantBuffer 가 이번 프레임 슬롯에 썼으므로
                // 같은 구간을 건다. 칸 크기는 레코드가 만든 때부터 들고 있다(락 없음 — `VulkanBufferRecord::_cbSlotSize`).
                if ( bConstant && pRecord->_cbSlotSize != 0 )
                {
                    candidate._range  = pRecord->_cbSlotSize;
                    candidate._offset = static_cast<uint64>( _pDevice->getFrameSlot() % constant::render::kMaxFrameCountInFlight ) * pRecord->_cbSlotSize;
                }
                // 상수 버퍼 페이지의 구간이면 그 칸 안의 구간만 건다(감사 R-4 — 디스크립터 offset · range).
                if ( source._size != 0 )
                {
                    candidate._offset += source._offset;
                    candidate._range = source._size;
                }
            }
        }

        // 값이 그대로면 세트를 새로 할당하지 않는다. 바인더는 드로우마다 같은 버퍼를 다시 걸고, PSO 가 바뀌어도 대개 같은 버퍼다.
        VulkanSlotBinding& current    = state._arrSlot[bindingIndex];
        const uint64       slotBit    = uint64{ 1 } << bindingIndex;
        const bool         bBound     = candidate._buffer != VK_NULL_HANDLE;
        const bool         bWasBound  = ( state._slotSetMask & slotBit ) != 0;
        const bool         bUnchanged = bBound == bWasBound && ( bBound == false || ( current._buffer == candidate._buffer && current._offset == candidate._offset && current._range == candidate._range ) );
        if ( bUnchanged )
            return;
        current = candidate;
        if ( bBound )
            state._slotSetMask |= slotBit;
        else
            state._slotSetMask &= ~slotBit;
        state._bDirty = SW_TRUE;
    }

    void VulkanRHICommandContext::flushBindings( RHIBindPoint point )
    {
        const bool      bCompute = point == RHIBindPoint::Compute;
        VkCommandBuffer cmd      = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pDevice->_pipelineLayout == VK_NULL_HANDLE )
            return;

        namespace vk       = shaderslot::vk;
        namespace bindless = shaderslot::bindless;

        // 텍스처 배열 세트(set 1)는 커맨드버퍼가 사는 동안 안 바뀐다. 두 바인드 포인트에 한 번씩 건다.
        // 이번 링 슬롯의 세트다(R-6 — 산 번호 다시 쓰기는 슬롯의 차례에만 그 세트에 쓴다).
        const VkDescriptorSet textureSet = _pDevice->getTextureSet( _pDevice->getFrameSlot() );
        if ( _pState->_bTextureSetBound == SW_FALSE && textureSet != VK_NULL_HANDLE )
        {
            vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, bindless::kVkTextureSet, 1, &textureSet, 0, nullptr );
            vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, bindless::kVkTextureSet, 1, &textureSet, 0, nullptr );
            _pState->_bTextureSetBound = SW_TRUE;
        }

        // 푸시 상수는 두 바인드 포인트가 같은 자리를 나눠 쓴다 — 다른 쪽이 마지막에 썼으면 블록 전체를 다시 낸다.
        RHIRootConstants& constants = _bindings.getRootConstants( point );
        if ( _pState->_lastPushPoint != point )
            constants.markAllDirty();
        if ( constants.isDirty() )
        {
            const uint32 first = constants._dirtyBegin;
            const uint32 count = constants._dirtyEnd - constants._dirtyBegin;
            vkCmdPushConstants( cmd, _pDevice->_pipelineLayout, _pDevice->getAllShaderStages(), first * 4u, count * 4u, constants._arrDword + first );
            constants.markFlushed();
            _pState->_lastPushPoint = point;
        }

        VulkanSlotState& state = _pState->_arrSlotState[static_cast<uint32>( point )];
        RHIShaderSlots&  slots = _bindings.getSlots( point );
        for ( const RHIShaderSlotKind kind : { RHIShaderSlotKind::ConstantBuffer, RHIShaderSlotKind::ShaderResource, RHIShaderSlotKind::UnorderedAccess } )
        {
            if ( slots.isDirty( kind ) )
            {
                slots.flush( kind, [this, &state, kind]( uint32 slot, RHIDescriptorIndex index )
                { resolveSlot( state, kind, slot, index ); } );
            }
        }
        if ( state._bDirty == SW_FALSE )
            return;

        // 슬롯 상태가 바뀌었다. 이 버퍼의 풀 묶음에서 세트를 하나 받아 걸린 슬롯만 쓴다(언리얼 Vulkan RHI 의 세트 캐시와 같은 자리).
        // 리스트는 자기 쌍의 묶음, 디바이스 프레임 스트림은 링 슬롯의 묶음을 쓴다. 어느 쪽도 다른 스레드와 나누지 않으므로 락이 없다.
        // b 범위는 셰이더가 정적으로 참조하므로 안 걸린 자리도 더미 UBO 로 채운다(픽스처의 MaterialCB 등).
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
                // 컴퓨트 쓰기 슬롯은 비워 두지 않는다 — 다시 받은 세트에 남은 디스크립터(놓은 버퍼)에 쓴다. 레이아웃이 partially-bound 라 검증 레이어도 보지 못한다.
                const bool bComputeWrite = bCompute && bindingIndex >= vk::kUShift && bindingIndex < vk::kUShift + shaderslot::kComputeUAVSlotCount;
                const bool bDummyUniform = bUniform && bindingIndex < shaderslot::kConstantBufferSlotCount;
                if ( bDummyUniform == false && bComputeWrite == false )
                    continue;
                arrInfo[writeCount].buffer = bUniform ? _pDevice->_dummyUBO : _pDevice->_dummyStorage;
                arrInfo[writeCount].offset = 0;
                arrInfo[writeCount].range  = VK_WHOLE_SIZE;
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
        SW_PROFILE_COUNT( "RT.Bind.descriptorUpdates", writeCount );
        SW_PROFILE_COUNT( "RT.Bind.descriptorSetFlushes", 1 );

        vkCmdBindDescriptorSets( cmd, bCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 0, 1, &set, 0, nullptr );
        state._bDirty = SW_FALSE;
    }

    bool VulkanRHICommandContext::bindMeshVertexBufferForDraw( const RHIVertexLayout& layout )
    {
        // 파이프라인이 선언한 바인딩(셰이더가 읽는 슬롯)에만 버퍼를 건다 — 정점 입력이 없는 셰이더는 바인딩이 없어 아무것도 걸지 않는다.
        // 건 뒤에 부서진 버퍼(세대가 달라 풀리지 않는다)면 드로우를 버린다.
        VkCommandBuffer cmd = commandBuffer();
        return _bindings._geometry.bindVertexBuffersForDraw( _pDevice->getDrawDiagnostics(), layout, [this, cmd]( uint32 slot, const RHIBufferBinding& binding )
        {
            const VulkanRHIDevice::VulkanBufferRecord* pBuffer = _pDevice->resolveAllocatedBuffer( binding._buffer );
            if ( pBuffer == nullptr || pBuffer->_buffer == VK_NULL_HANDLE )
                return false;
            const VkDeviceSize vkOffset = static_cast<VkDeviceSize>( binding._offset ); // stride 는 파이프라인의 바인딩이 가진다(validateVertexSlotsBound 가 같은지 봤다)
            vkCmdBindVertexBuffers( cmd, slot, 1, &pBuffer->_buffer, &vkOffset );
            return true;
        } );
    }

    bool VulkanRHICommandContext::prepareDraw( bool bIndexed )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE || _pDevice->_pipelineLayout == VK_NULL_HANDLE )
            return false;
        // 백엔드 계약(네 백엔드 같음): 등록된 그래픽스 PSO 없이 그리면 오류로 멈춘다. 기본 파이프라인으로 대신 그리지 않는다 — PSO 가 0 이면
        // 부르는 쪽이 드로우를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · PSO 를 걸지 않은 드로우).
        const VulkanRHIDevice::VulkanPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( _bindings._graphicsPSO );
        if ( pRecord == nullptr || pRecord->_pipeline == VK_NULL_HANDLE )
            reportFatalError( "Vulkan draw without a registered graphics PSO (handle %#)", _bindings._graphicsPSO.getPacked() );
        if ( _pState->_boundNativeGraphicsPSO != _bindings._graphicsPSO )
        {
            vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pRecord->_pipeline );
            _pState->_boundNativeGraphicsPSO = _bindings._graphicsPSO;
        }
        // 세트를 굳히지 않은 드로우는 세트 없이 나가 Vulkan 에서 아무것도 그리지 않는다 — 드로우 진입점 모두가 이 함수를 거친다.
        flushBindings( RHIBindPoint::Graphics );
        if ( bindMeshVertexBufferForDraw( pRecord->_vertexLayout ) == false )
            return false;
        if ( bIndexed == false )
            return true;
        return _bindings._geometry.bindIndexBufferForDraw( _pDevice->getDrawDiagnostics(), [this, cmd]( const RHIBufferBinding& binding )
        {
            const VulkanRHIDevice::VulkanBufferRecord* pIb = _pDevice->resolveAllocatedBuffer( binding._buffer );
            if ( pIb == nullptr || pIb->_buffer == VK_NULL_HANDLE )
                return false;
            vkCmdBindIndexBuffer( cmd, pIb->_buffer, binding._offset, ( binding._stride == 2 ) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32 );
            return true;
        } );
    }

    void VulkanRHICommandContext::prepareDispatch()
    {
        // 백엔드 계약(네 백엔드 같음): 등록된 컴퓨트 PSO 없이 디스패치하면 오류로 멈춘다. 앞 디스패치의 파이프라인으로 대신 돌지 않고 조용히 건너뛰지도 않는다 — PSO 가 0 이면
        // 부르는 쪽이 디스패치를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · 컴퓨트 PSO 를 걸지 않은 디스패치).
        const VulkanRHIDevice::VulkanPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( _bindings._computePSO );
        if ( pRecord == nullptr || pRecord->_pipeline == VK_NULL_HANDLE )
            reportFatalError( "Vulkan dispatch without a registered compute PSO (handle %#)", _bindings._computePSO.getPacked() );
        if ( _pState->_boundNativeComputePSO != _bindings._computePSO )
        {
            vkCmdBindPipeline( commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pRecord->_pipeline );
            _pState->_boundNativeComputePSO = _bindings._computePSO;
        }
        flushBindings( RHIBindPoint::Compute );
    }

    void VulkanRHICommandContext::drawNative( uint32 vertexCount, uint32 startVertex )
    {
        drawInstancedNative( vertexCount, 1, startVertex, 0 );
    }

    void VulkanRHICommandContext::drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        if ( vertexCount == 0 || instanceCount == 0 || prepareDraw( false ) == false )
            return;
        vkCmdDraw( commandBuffer(), vertexCount, instanceCount, startVertex, startInstance );
    }

    void VulkanRHICommandContext::updateConstantBufferNative( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( _pDevice != nullptr )
            _pDevice->_resourceImpl->writeConstantBuffer( buffer, pData, size );
    }

    void VulkanRHICommandContext::dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;

        prepareDispatch();
        vkCmdDispatch( cmd, threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void VulkanRHICommandContext::setViewportNative( const RHIViewport& viewport )
    {
        // RHIViewport 는 DirectX 방식이다(좌상단 원점, 픽셀 공간에서 +Y 가 아래, NDC 에서 +Y 가 위).
        // VkViewport.height 를 음수로 주면 Vulkan NDC 가 DX · GL_UPPER_LEFT 와 같은 방향이 된다.
        VkViewport vkViewport{};
        vkViewport.x        = viewport._x;
        vkViewport.y        = viewport._y + viewport._height;
        vkViewport.width    = viewport._width;
        vkViewport.height   = -viewport._height;
        vkViewport.minDepth = viewport._minDepth;
        vkViewport.maxDepth = viewport._maxDepth;
        vkCmdSetViewport( commandBuffer(), 0, 1, &vkViewport );
    }

    void VulkanRHICommandContext::setScissorRectNative( const RHIScissorRect& rect )
    {
        // 가위는 프레임버퍼 좌표(왼쪽 위 원점)라 음수 높이 뷰포트와 달리 뒤집지 않는다.
        VkRect2D scissor{};
        scissor.offset.x      = static_cast<int32>( rect._x );
        scissor.offset.y      = static_cast<int32>( rect._y );
        scissor.extent.width  = rect._width;
        scissor.extent.height = rect._height;
        vkCmdSetScissor( commandBuffer(), 0, 1, &scissor );
    }

    void VulkanRHICommandContext::drawIndexedInstancedNative( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance )
    {
        if ( indexCount == 0 || instanceCount == 0 || prepareDraw( true ) == false )
            return;
        vkCmdDrawIndexed( commandBuffer(), indexCount, instanceCount, startIndex, baseVertex, startInstance );
    }

    void VulkanRHICommandContext::drawIndexedIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer,
                                                             uint32 countBufferOffset )
    {
        const VulkanRHIDevice::VulkanBufferRecord* pArgs          = _pDevice->resolveAllocatedBuffer( argumentBuffer );
        const bool                                 bNothingToDraw = pArgs == nullptr || pArgs->_buffer == VK_NULL_HANDLE || drawCount == 0;
        if ( bNothingToDraw || prepareDraw( true ) == false )
            return;
        VkCommandBuffer  cmd    = commandBuffer();
        constexpr uint32 stride = sizeof( VkDrawIndexedIndirectCommand );
        static_assert( sizeof( VkDrawIndexedIndirectCommand ) == sizeof( RHIDrawIndexedIndirectCommand ), "the indexed indirect layout must match Vulkan's" );
        // drawIndirect 와 같은 순서 — 개수 버퍼 → 멀티 → 하나씩.
        if ( countBuffer != 0 && _pDevice->_bDrawIndirectCount != 0 )
        {
            const VulkanRHIDevice::VulkanBufferRecord* pCountRecord = _pDevice->resolveAllocatedBuffer( countBuffer );
            if ( pCountRecord != nullptr && pCountRecord->_buffer != VK_NULL_HANDLE )
            {
                reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCount>( _pDevice->_pCmdDrawIndexedIndirectCount )( cmd, pArgs->_buffer, argumentBufferOffset, pCountRecord->_buffer, countBufferOffset, drawCount, stride );
                return;
            }
        }
        if ( drawCount == 1 || _pDevice->_bMultiDrawIndirect != SW_FALSE )
        {
            vkCmdDrawIndexedIndirect( cmd, pArgs->_buffer, argumentBufferOffset, drawCount, stride );
            return;
        }
        for ( uint32 commandIndex = 0; commandIndex < drawCount; ++commandIndex )
        {
            vkCmdDrawIndexedIndirect( cmd, pArgs->_buffer, argumentBufferOffset + commandIndex * stride, 1, stride );
        }
    }
    void VulkanRHICommandContext::dispatchIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        VkCommandBuffer                            cmd     = commandBuffer();
        const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( argumentBuffer );
        if ( cmd == VK_NULL_HANDLE || pRecord == nullptr )
            return;

        if ( pRecord->_buffer != VK_NULL_HANDLE )
        {
            prepareDispatch();
            vkCmdDispatchIndirect( cmd, pRecord->_buffer, argumentBufferOffset );
        }
    }

    void VulkanRHICommandContext::drawIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount,
                                                      RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        VkCommandBuffer                            cmd                    = commandBuffer();
        const VulkanRHIDevice::VulkanBufferRecord* pArgs                  = _pDevice->resolveAllocatedBuffer( argumentBuffer );
        const bool                                 bNothingToDrawIndirect = cmd == VK_NULL_HANDLE || pArgs == nullptr || pArgs->_buffer == VK_NULL_HANDLE || drawCount == 0;
        if ( bNothingToDrawIndirect )
            return;

        if ( prepareDraw( false ) == false )
            return;

        constexpr uint32 stride = sizeof( RHIDrawIndirectCommand );

        if ( countBuffer != 0 && _pDevice->_bDrawIndirectCount != 0 )
        {
            const VulkanRHIDevice::VulkanBufferRecord* pCountRecord = _pDevice->resolveAllocatedBuffer( countBuffer );
            if ( pCountRecord != nullptr )
            {
                if ( pCountRecord->_buffer != VK_NULL_HANDLE )
                {
                    reinterpret_cast<PFN_vkCmdDrawIndirectCount>( _pDevice->_pCmdDrawIndirectCount )( cmd, pArgs->_buffer, argumentBufferOffset, pCountRecord->_buffer, countBufferOffset,
                                                                                                      drawCount, stride );
                    return;
                }
            }
        }

        // 한 번만 그리거나 멀티를 지원하면 호출 하나로 끝난다. 아니면 하나씩 나눠 부른다.
        if ( drawCount == 1 || _pDevice->_bMultiDrawIndirect != SW_FALSE )
        {
            vkCmdDrawIndirect( cmd, pArgs->_buffer, argumentBufferOffset, drawCount, stride );
            return;
        }

        for ( uint32 commandIndex = 0; commandIndex < drawCount; ++commandIndex )
        {
            vkCmdDrawIndirect( cmd, pArgs->_buffer, argumentBufferOffset + commandIndex * stride, 1, stride );
        }
    }

    void VulkanRHICommandContext::writeBreadcrumb( uint32 markerID )
    {
        VkCommandBuffer cmd = commandBuffer();
        if ( cmd == VK_NULL_HANDLE )
            return;
        switch ( _pDevice->_breadcrumbMode )
        {
            case VulkanBreadcrumbMode::None:
            {
                return;
            }
            case VulkanBreadcrumbMode::NVCheckpoints:
            {
                // 체크포인트의 값은 포인터 크기 자리다 - 패스 id 를 그대로 싣는다(소실 뒤 vkGetQueueCheckpointDataNV 가 같은 값을 돌려준다).
                reinterpret_cast<PFN_vkCmdSetCheckpointNV>( _pDevice->_pCmdSetCheckpoint )( cmd, reinterpret_cast<const void*>( static_cast<uintptr_t>( markerID ) ) );
                return;
            }
            case VulkanBreadcrumbMode::AMDBufferMarker:
            {
                // 시작 표지는 [0] 칸에 TOP_OF_PIPE(앞 명령이 파이프라인에 들어간 뒤), 끝 표지는 [1] 칸에 BOTTOM_OF_PIPE(앞 명령이 모두 끝난 뒤) — DX12 MARKER_IN · MARKER_OUT 과 같은 뜻.
                // 렌더 패스 안팎 어디서나 기록할 수 있다(명세의 Render Pass Scope: Both).
                const bool                    bEnd  = ( markerID & GPUBreadcrumbs::kEndMarkerBit ) != 0;
                const VkPipelineStageFlagBits stage = bEnd ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                reinterpret_cast<PFN_vkCmdWriteBufferMarkerAMD>( _pDevice->_pCmdWriteBufferMarker )( cmd, stage, _pDevice->_breadcrumbBuffer,
                                                                                                     ( _breadcrumbSlotBase + ( bEnd ? 1u : 0u ) ) * sizeof( uint32 ), markerID );
                return;
            }
        }
    }

    void VulkanRHICommandContext::beginEventMarker( const utf8* pName )
    {
        // 디버그 라벨 함수가 없어도(검증 레이어 없는 실행) 패스 이름은 남긴다 — 디바이스 손실 로그가 이것을 읽는다.
        if ( pName != nullptr )
            _lastEventMarker = pName;
        VkCommandBuffer cmd               = commandBuffer();
        const bool      bCannotBeginLabel = cmd == VK_NULL_HANDLE || pName == nullptr || _pDevice->_instance == VK_NULL_HANDLE;
        if ( bCannotBeginLabel )
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
