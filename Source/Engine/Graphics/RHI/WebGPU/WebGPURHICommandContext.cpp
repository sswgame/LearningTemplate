#include "pch.h"

#include "Engine/Graphics/RHI/WebGPU/WebGPURHICommandContext.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/WebGPU/WebGPURHIDevice.h"

namespace sw
{
    namespace
    {
        struct WebGPURHICommandContextInternal
        {
            /** @brief 그룹 항목 하나에 버퍼 전체를 겁니다. */
            static WGPUBindGroupEntry makeBufferEntry( uint32 binding, WGPUBuffer buffer, uint64 offset, uint64 size )
            {
                WGPUBindGroupEntry entry{};
                entry.binding = binding;
                entry.buffer  = buffer;
                entry.offset  = offset;
                entry.size    = size;
                return entry;
            }

            static WGPUBindGroupEntry makeTextureEntry( uint32 binding, WGPUTextureView view )
            {
                WGPUBindGroupEntry entry{};
                entry.binding     = binding;
                entry.textureView = view;
                return entry;
            }

            static WGPUBindGroupEntry makeSamplerEntry( uint32 binding, WGPUSampler sampler )
            {
                WGPUBindGroupEntry entry{};
                entry.binding = binding;
                entry.sampler = sampler;
                return entry;
            }

            static void fillInvalid( RHIDescriptorIndex* pArray, size_t count )
            {
                for ( size_t index = 0; index < count; ++index )
                {
                    pArray[index] = kInvalidDescriptorIndex;
                }
            }
        };
    } // namespace

    WebGPUSlotState::WebGPUSlotState()
        : _arrConstantBuffer{}
        , _arrShaderResource{}
        , _arrUnorderedAccess{}
        , _arrRootConstant{}
        , _rootSliceBuffer{ nullptr }
        , _rootSliceOffset{ 0 }
        , _rootSliceSerial{ 0 }
        , _bRootConstantSet{ SW_FALSE }
        , _bRootSliceDirty{ SW_TRUE }
        , _reserved{ 0 }
    {
        WebGPURHICommandContextInternal::fillInvalid( _arrConstantBuffer, std::size( _arrConstantBuffer ) );
        WebGPURHICommandContextInternal::fillInvalid( _arrShaderResource, std::size( _arrShaderResource ) );
        WebGPURHICommandContextInternal::fillInvalid( _arrUnorderedAccess, std::size( _arrUnorderedAccess ) );
    }

    WebGPURHICommandContext::WebGPURHICommandContext( WebGPURHIDevice* pDevice, bool bOwnEncoder )
        : _pDevice{ pDevice }
        , _ownEncoder{ nullptr }
        , _finishedCommandBuffer{ nullptr }
        , _state{}
        , _bOwnEncoder{ static_cast<uint8>( bOwnEncoder ? SW_TRUE : SW_FALSE ) }
        , _reserved{ 0 }
    {
    }

    WebGPURHICommandContext::~WebGPURHICommandContext()
    {
        detachFromDevice();
    }

    void WebGPURHICommandContext::detachFromDevice()
    {
        endComputePass();
        if ( _state._renderPass != nullptr )
        {
            wgpuRenderPassEncoderEnd( _state._renderPass );
            wgpuRenderPassEncoderRelease( _state._renderPass );
            _state._renderPass = nullptr;
        }
        if ( _ownEncoder != nullptr )
        {
            wgpuCommandEncoderRelease( _ownEncoder );
            _ownEncoder = nullptr;
        }
        if ( _finishedCommandBuffer != nullptr )
        {
            wgpuCommandBufferRelease( _finishedCommandBuffer );
            _finishedCommandBuffer = nullptr;
        }
        _pDevice = nullptr;
    }

    WGPUCommandEncoder WebGPURHICommandContext::getEncoder() const
    {
        if ( _bOwnEncoder != SW_FALSE )
            return _ownEncoder;
        return _pDevice != nullptr ? _pDevice->getFrameEncoder() : nullptr;
    }

    void WebGPURHICommandContext::beginCommandList()
    {
        if ( _bOwnEncoder == SW_FALSE || _pDevice == nullptr )
            return;
        if ( _finishedCommandBuffer != nullptr )
        {
            // 넘기지 않은 지난 기록이다 — 다시 기록하면 앞 것을 버린다(다른 백엔드도 리스트를 다시 열면 앞 기록을 버린다).
            wgpuCommandBufferRelease( _finishedCommandBuffer );
            _finishedCommandBuffer = nullptr;
        }
        WGPUCommandEncoderDescriptor desc{};
        desc.label  = WebGPURHIUtil::toStringView( "SW Command List" );
        _ownEncoder = wgpuDeviceCreateCommandEncoder( _pDevice->_device, &desc );
        _state      = WebGPURecordingState{};
    }

    void WebGPURHICommandContext::endCommandList()
    {
        if ( _bOwnEncoder == SW_FALSE || _ownEncoder == nullptr )
            return;
        endRenderPass();
        endComputePass();
        WGPUCommandBufferDescriptor desc{};
        _finishedCommandBuffer = wgpuCommandEncoderFinish( _ownEncoder, &desc );
        wgpuCommandEncoderRelease( _ownEncoder );
        _ownEncoder = nullptr;
    }

    WGPUCommandBuffer WebGPURHICommandContext::takeFinishedCommandBuffer()
    {
        WGPUCommandBuffer buffer = _finishedCommandBuffer;
        _finishedCommandBuffer   = nullptr;
        return buffer;
    }

    void WebGPURHICommandContext::endComputePass()
    {
        if ( _state._computePass == nullptr )
            return;
        wgpuComputePassEncoderEnd( _state._computePass );
        wgpuComputePassEncoderRelease( _state._computePass );
        _state._computePass = nullptr;
    }

    WGPUComputePassEncoder WebGPURHICommandContext::ensureComputePass()
    {
        if ( _state._computePass != nullptr )
            return _state._computePass;
        const WGPUCommandEncoder encoder = getEncoder();
        if ( encoder == nullptr )
            return nullptr;
        WGPUComputePassDescriptor desc{};
        _state._computePass = wgpuCommandEncoderBeginComputePass( encoder, &desc );
        return _state._computePass;
    }

    // ------------------------------------------------------------------------------
    // 렌더 패스
    // ------------------------------------------------------------------------------
    // 길이 유지(긴 함수 정리 기준): 컬러 첨부 · 깊이 첨부 · 패스 시작 · 기본 뷰포트의 순서가 곧 의미라 나누지 않는다.
    RHIRenderPassBeginResult WebGPURHICommandContext::beginRenderPassNative( const RHIRenderPassBeginInfo& beginInfo )
    {
        endComputePass();
        const WGPUCommandEncoder encoder = getEncoder();
        if ( encoder == nullptr || _pDevice == nullptr )
            return RHIRenderPassBeginResult::Failed;

        WGPURenderPassColorAttachment arrColor[kMaxColorAttachments]{};
        uint32                        colorCount   = 0;
        uint32                        targetWidth  = beginInfo._width;
        uint32                        targetHeight = beginInfo._height;
        if ( beginInfo._bBindColor != SW_FALSE )
        {
            const uint32 bindCount = MathUtil::min( beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u, kMaxColorAttachments );
            for ( uint32 attachment = 0; attachment < bindCount; ++attachment )
            {
                WGPUTextureView        view   = nullptr;
                const RHITextureHandle handle = beginInfo._arrColorTarget[attachment];
                if ( handle == 0 && attachment == 0 )
                {
                    // 백버퍼 — 이 프레임에 받은 스왑체인 텍스처다(`acquireSwapChainImage`). 없으면(최소화 · 다시 구성) 이 패스의 그리기를 알리지 않고 버린다.
                    const WebGPURHISwapChain* pSwapChain = beginInfo._pSwapChain != nullptr ? static_cast<const WebGPURHISwapChain*>( beginInfo._pSwapChain ) : &_pDevice->_swapChain;
                    view                                 = pSwapChain->getCurrentView();
                    if ( view == nullptr )
                        return RHIRenderPassBeginResult::NoBackBuffer;
                    if ( targetWidth == 0 )
                        targetWidth = pSwapChain->getWidth();
                    if ( targetHeight == 0 )
                        targetHeight = pSwapChain->getHeight();
                }
                else
                {
                    const WebGPUTextureRecord* pTexture = _pDevice->findTexture( handle );
                    const uint32               slice    = beginInfo._arrColorTargetSlice[attachment];
                    if ( pTexture == nullptr || slice >= pTexture->_listSliceView.size() )
                    {
                        if ( attachment > 0 )
                            break;
                        return RHIRenderPassBeginResult::Failed;
                    }
                    view = pTexture->_listSliceView[slice];
                    if ( targetWidth == 0 )
                        targetWidth = pTexture->_desc._width;
                    if ( targetHeight == 0 )
                        targetHeight = pTexture->_desc._height;
                }
                WGPURenderPassColorAttachment& color = arrColor[colorCount++];
                const float4&                  clear = beginInfo._arrClearColor[attachment];
                color.view                           = view;
                color.depthSlice                     = WGPU_DEPTH_SLICE_UNDEFINED;
                color.loadOp                         = WebGPURHIUtil::toLoadOp( beginInfo._arrLoadOp[attachment] );
                color.storeOp                        = WebGPURHIUtil::toStoreOp( beginInfo._arrStoreOp[attachment] );
                color.clearValue                     = WGPUColor{ clear._x, clear._y, clear._z, clear._w };
            }
        }

        WGPURenderPassDepthStencilAttachment depth{};
        bool                                 bDepth = false;
        if ( beginInfo._depthTarget != 0 )
        {
            const WebGPUTextureRecord* pDepth = _pDevice->findTexture( beginInfo._depthTarget );
            if ( pDepth == nullptr || beginInfo._depthTargetSlice >= pDepth->_listSliceView.size() )
                return RHIRenderPassBeginResult::Failed;
            bDepth                  = true;
            depth.view              = pDepth->_listSliceView[beginInfo._depthTargetSlice];
            depth.depthLoadOp       = WebGPURHIUtil::toLoadOp( beginInfo._depthLoadOp );
            depth.depthStoreOp      = WebGPURHIUtil::toStoreOp( beginInfo._depthStoreOp );
            depth.depthClearValue   = beginInfo._clearDepth;
            depth.stencilLoadOp     = depth.depthLoadOp; // 스텐실이 있는 형식은 두 칸을 다 적어야 한다(없는 형식에 적으면 검증 오류라 아래에서 지운다)
            depth.stencilStoreOp    = depth.depthStoreOp;
            depth.stencilClearValue = beginInfo._clearStencil;
            const bool bHasStencil  = pDepth->_wgpuFormat == WGPUTextureFormat_Depth24PlusStencil8 || pDepth->_wgpuFormat == WGPUTextureFormat_Depth32FloatStencil8;
            if ( bHasStencil == false )
            {
                depth.stencilLoadOp  = WGPULoadOp_Undefined;
                depth.stencilStoreOp = WGPUStoreOp_Undefined;
            }
            if ( targetWidth == 0 )
                targetWidth = pDepth->_desc._width;
            if ( targetHeight == 0 )
                targetHeight = pDepth->_desc._height;
        }

        WGPURenderPassDescriptor passDesc{};
        passDesc.colorAttachmentCount   = colorCount;
        passDesc.colorAttachments       = colorCount > 0 ? arrColor : nullptr;
        passDesc.depthStencilAttachment = bDepth ? &depth : nullptr;
        _state._renderPass              = wgpuCommandEncoderBeginRenderPass( encoder, &passDesc );
        _state._boundRenderPSO          = 0;
        if ( _state._renderPass == nullptr )
            return RHIRenderPassBeginResult::Failed;
        // 앞 패스의 뷰포트 · 가위가 새지 않게 패스 전체로 둔다(DX11 · GL 과 같다).
        wgpuRenderPassEncoderSetViewport( _state._renderPass, 0.0f, 0.0f, static_cast<float32>( targetWidth ), static_cast<float32>( targetHeight ), 0.0f, 1.0f );
        wgpuRenderPassEncoderSetScissorRect( _state._renderPass, 0, 0, targetWidth, targetHeight );
        return RHIRenderPassBeginResult::Begun;
    }

    void WebGPURHICommandContext::endRenderPassNative()
    {
        if ( _state._renderPass == nullptr )
            return;
        wgpuRenderPassEncoderEnd( _state._renderPass );
        wgpuRenderPassEncoderRelease( _state._renderPass );
        _state._renderPass     = nullptr;
        _state._boundRenderPSO = 0;
    }

    void WebGPURHICommandContext::setViewport( const RHIViewport& viewport )
    {
        if ( _state._renderPass != nullptr )
            wgpuRenderPassEncoderSetViewport( _state._renderPass, viewport._x, viewport._y, viewport._width, viewport._height, viewport._minDepth, viewport._maxDepth );
    }

    void WebGPURHICommandContext::setScissorRect( const RHIScissorRect& rect )
    {
        if ( _state._renderPass != nullptr )
            wgpuRenderPassEncoderSetScissorRect( _state._renderPass, rect._x, rect._y, rect._width, rect._height );
    }

    // ------------------------------------------------------------------------------
    // 파이프라인 · 슬롯
    // ------------------------------------------------------------------------------
    void WebGPURHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        // 등록되지 않은 핸들도 적는다 — 앞 PSO 로 대신 그리지 않고 prepareDraw 에서 멈춘다(DX11 과 같다).
        _state._graphicsPSO = pso;
        _state._geometry.clearVertexBuffers(); // 정점 버퍼는 PSO 뒤에 건다(IRHICommandList::setPipelineState 계약)
    }

    void WebGPURHICommandContext::setComputePipelineStateNative( RHIPipelineStateHandle pso )
    {
        _state._computePSO = pso;
    }

    void WebGPURHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        _state._geometry.setVertexBuffer( slot, buffer, stride, offset );
    }

    void WebGPURHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
    {
        _state._geometry.setIndexBuffer( buffer, indexStride, offset );
    }

    void WebGPURHICommandContext::writeRootConstants( WebGPUSlotState& slots, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        if ( pData == nullptr || num32BitValues == 0 )
            return;
        if ( destOffsetIn32BitValues + num32BitValues > shaderslot::kRootConstantDwords )
            SW_FATAL( "WebGPU root constants overflow: offset %# + %# dwords > %#", destOffsetIn32BitValues, num32BitValues, shaderslot::kRootConstantDwords );
        Memory::copy( &slots._arrRootConstant[destOffsetIn32BitValues], pData, num32BitValues * sizeof( uint32 ) );
        slots._bRootConstantSet = SW_TRUE;
        slots._bRootSliceDirty  = SW_TRUE;
    }

    void WebGPURHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        (void)rootParameterIndex; // 에뮬 백엔드는 루트 매개변수가 하나(b2)다
        writeRootConstants( _state._compute, num32BitValues, pData, destOffsetIn32BitValues );
    }

    void WebGPURHICommandContext::setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        (void)rootParameterIndex;
        writeRootConstants( _state._graphics, num32BitValues, pData, destOffsetIn32BitValues );
    }

    void WebGPURHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
    {
        if ( slot < shaderslot::wgpu::kURange )
            _state._compute._arrUnorderedAccess[slot] = index;
    }

    void WebGPURHICommandContext::bindGraphicsUAV( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 단계 u 바인딩은 구현하지 않았다(`RHICapabilities::_bGraphicsShaderWrites` 가 거짓) — 셰이더 메시지 채널은 이 백엔드에서 켜지지 않아 여기에 오지 않는다.
        (void)index;
        (void)slot;
    }

    void WebGPURHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        if ( slot < shaderslot::wgpu::kTRange )
            _state._graphics._arrShaderResource[slot] = index;
    }

    void WebGPURHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        if ( slot < shaderslot::wgpu::kTRange )
            _state._graphics._arrShaderResource[slot] = index;
    }

    void WebGPURHICommandContext::bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot )
    {
        if ( slot < shaderslot::wgpu::kBRange )
            _state._graphics._arrConstantBuffer[slot] = constantBufferIndex;
    }

    void WebGPURHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot )
    {
        if ( slot < shaderslot::wgpu::kBRange )
            _state._compute._arrConstantBuffer[slot] = constantBufferIndex;
    }

    void WebGPURHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        if ( slot < shaderslot::wgpu::kTRange )
            _state._compute._arrShaderResource[slot] = index;
    }

    void WebGPURHICommandContext::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        // 기록 중 갱신은 상수 링의 새 칸으로 이름을 바꾼다 — 같은 버퍼를 한 프레임에 여러 번 갱신해도 앞서 기록한 그리기는 제 칸을 읽는다(DX11 WRITE_DISCARD 와 같은 뜻).
        WebGPUBufferRecord* const pRecord   = _pDevice != nullptr ? _pDevice->findBuffer( buffer ) : nullptr;
        const bool                bNoUpdate = pRecord == nullptr || pData == nullptr || size == 0;
        if ( bNoUpdate )
            return;
        WGPUBuffer ringBuffer = nullptr;
        uint64     ringOffset = 0;
        _pDevice->allocateConstantSlice( pData, size, ringBuffer, ringOffset );
        pRecord->_overrideSerial = _pDevice->getFrameSerial();
        pRecord->_overrideBuffer = ringBuffer;
        pRecord->_overrideOffset = ringOffset;
    }

    WGPUTextureView WebGPURHICommandContext::resolveSampledView( RHIDescriptorIndex index ) const
    {
        const WebGPUBindlessEntry* pEntry   = _pDevice->_textureTable.find( index );
        const WebGPUTextureRecord* pTexture = pEntry != nullptr ? _pDevice->findTexture( pEntry->_handle ) : nullptr;
        if ( pTexture == nullptr )
            return _pDevice->_defaultTextureView;
        // 깊이 텍스처의 샘플 뷰는 R32 사본이다 — 이 프레임에 채우지 않았으면 지난 깊이(또는 0)를 읽는다. 부르는 쪽이 `prepareTextureForShaderRead` 를 빠뜨렸다.
        if ( pTexture->_depthSampleTexture != nullptr && pTexture->_depthCopySerial != _pDevice->getFrameSerial() )
            SW_LOG_ERROR_ONCE_ID( "WebGPU.DepthSampledWithoutPrepare", "WebGPU: a depth texture (%#x%#) is sampled without prepareTextureForShaderRead this frame - its R32 copy is stale",
                                  pTexture->_desc._width, pTexture->_desc._height );
        return pTexture->_sampleView;
    }

    // 길이 유지(긴 함수 정리 기준): 바인딩 번호의 띠(b · t · s · u)마다 한 갈래라 나누면 같은 표를 네 번 넘긴다.
    WGPUBindGroup WebGPURHICommandContext::findBindGroup( const WebGPUPipelineRecord& pipeline, WebGPUSlotState& slots )
    {
        namespace wgpu = shaderslot::wgpu;
        using Internal = WebGPURHICommandContextInternal;
        if ( pipeline._groupLayout == nullptr || pipeline._listBinding.empty() )
            return nullptr;
        vector<WGPUBindGroupEntry> listEntry;
        listEntry.reserve( pipeline._listBinding.size() );
        for ( const uint32 binding : pipeline._listBinding )
        {
            if ( binding >= wgpu::kUShift )
            {
                const uint32               slot   = binding - wgpu::kUShift;
                const WebGPUBindlessEntry* pEntry = slot < wgpu::kURange ? _pDevice->_uavTable.find( slots._arrUnorderedAccess[slot] ) : nullptr;
                if ( pEntry != nullptr && pEntry->_bTexture != SW_FALSE )
                {
                    const WebGPUTextureRecord* pTexture = _pDevice->findTexture( pEntry->_handle );
                    if ( pTexture != nullptr && pTexture->_listSliceView.empty() == false )
                    {
                        listEntry.push_back( Internal::makeTextureEntry( binding, pTexture->_listSliceView[0] ) );
                        continue;
                    }
                }
                const WebGPUBufferRecord* pBuffer = pEntry != nullptr && pEntry->_bTexture == SW_FALSE ? _pDevice->_buffers.get( pEntry->_handle ) : nullptr;
                if ( pBuffer == nullptr )
                {
                    // 셰이더가 쓰는 쓰기 자리를 엔진이 걸지 않았다 — 빈 버퍼에 쓴다. DX11 · GL 도 안 걸린 UAV 쓰기를 버린다(같은 결과).
                    listEntry.push_back( Internal::makeBufferEntry( binding, _pDevice->_defaultBuffer, 0, WGPU_WHOLE_SIZE ) );
                    continue;
                }
                listEntry.push_back( Internal::makeBufferEntry( binding, pBuffer->_buffer, 0, pBuffer->_sizeBytes ) );
                continue;
            }
            if ( binding >= wgpu::kSShift )
            {
                const uint32 slot        = binding - wgpu::kSShift;
                const uint32 staticIndex = slot >= shaderslot::dx11::kStaticSampler0 ? slot - shaderslot::dx11::kStaticSampler0 : 0u;
                const uint32 safeIndex   = staticIndex < shaderslot::kStaticSamplerArrayCount ? staticIndex : 0u;
                listEntry.push_back( Internal::makeSamplerEntry( binding, _pDevice->_arrStaticSampler[safeIndex] ) );
                continue;
            }
            if ( binding >= wgpu::kTShift )
            {
                const uint32             slot       = binding - wgpu::kTShift;
                const bool               bTextureAt = slot >= shaderslot::kMaterialTexture0; // 머티리얼(t16..) · 엔진 텍스처 칸(t32..)은 텍스처, 그 앞 띠는 구조 버퍼다(binding_slots.hlsli 2)
                const RHIDescriptorIndex index      = slot < wgpu::kTRange ? slots._arrShaderResource[slot] : kInvalidDescriptorIndex;
                if ( bTextureAt )
                {
                    listEntry.push_back( Internal::makeTextureEntry( binding, resolveSampledView( index ) ) );
                    continue;
                }
                const WebGPUBindlessEntry* pEntry  = _pDevice->_bufferTable.find( index );
                const WebGPUBufferRecord*  pBuffer = pEntry != nullptr ? _pDevice->_buffers.get( pEntry->_handle ) : nullptr;
                // 안 걸린 읽기 버퍼는 빈 버퍼다 — 셰이더는 PassCB 의 인덱스가 무효면 읽지 않는다(DX11 · GL 의 빈 SRV 와 같다).
                listEntry.push_back( pBuffer != nullptr ? Internal::makeBufferEntry( binding, pBuffer->_buffer, 0, pBuffer->_sizeBytes )
                                                        : Internal::makeBufferEntry( binding, _pDevice->_defaultBuffer, 0, WGPU_WHOLE_SIZE ) );
                continue;
            }
            const uint32 slot = binding - wgpu::kBShift;
            if ( slot == shaderslot::kRootConstantEmulationSlot )
            {
                const bool bNeedSlice = slots._bRootSliceDirty != SW_FALSE || slots._rootSliceSerial != _pDevice->getFrameSerial() || slots._rootSliceBuffer == nullptr;
                if ( bNeedSlice )
                {
                    _pDevice->allocateConstantSlice( slots._arrRootConstant, sizeof( slots._arrRootConstant ), slots._rootSliceBuffer, slots._rootSliceOffset );
                    slots._rootSliceSerial = _pDevice->getFrameSerial();
                    slots._bRootSliceDirty = SW_FALSE;
                }
                listEntry.push_back( Internal::makeBufferEntry( binding, slots._rootSliceBuffer, slots._rootSliceOffset, sizeof( slots._arrRootConstant ) ) );
                continue;
            }
            const WebGPUBindlessEntry* pEntry  = slot < wgpu::kBRange ? _pDevice->_bufferTable.find( slots._arrConstantBuffer[slot] ) : nullptr;
            const WebGPUBufferRecord*  pBuffer = pEntry != nullptr ? _pDevice->_buffers.get( pEntry->_handle ) : nullptr;
            if ( pBuffer == nullptr )
            {
                listEntry.push_back( Internal::makeBufferEntry( binding, _pDevice->_defaultBuffer, 0, WGPU_WHOLE_SIZE ) );
                continue;
            }
            const bool bRenamed = pBuffer->_overrideSerial == _pDevice->getFrameSerial() && pBuffer->_overrideBuffer != nullptr;
            listEntry.push_back( bRenamed ? Internal::makeBufferEntry( binding, pBuffer->_overrideBuffer, pBuffer->_overrideOffset, pBuffer->_sizeBytes )
                                          : Internal::makeBufferEntry( binding, pBuffer->_buffer, 0, pBuffer->_sizeBytes ) );
        }
        // 같은 (레이아웃, 항목) 은 프레임 안에서 한 번만 짓는다 — 브라우저에서 그룹 하나는 JS 객체 하나라 그리기마다 지으면 GC 압력이 그리기 수만큼 는다.
        return _pDevice->_bindGroupCache.findOrCreate( _pDevice->_device, pipeline._groupLayout, listEntry );
    }

    // ------------------------------------------------------------------------------
    // 그리기
    // ------------------------------------------------------------------------------
    bool WebGPURHICommandContext::prepareDraw()
    {
        const WebGPUPipelineRecord* pPipeline = _pDevice->_pipelines.get( _state._graphicsPSO );
        // 백엔드 계약(다섯 백엔드 같음): 등록된 그래픽스 PSO 없이 그리면 오류로 멈춘다(부르는 쪽의 결함).
        if ( pPipeline == nullptr || pPipeline->_renderPipeline == nullptr )
            SW_FATAL( "WebGPU draw without a registered graphics PSO (handle %#)", _state._graphicsPSO );
        if ( _state._boundRenderPSO != _state._graphicsPSO )
        {
            wgpuRenderPassEncoderSetPipeline( _state._renderPass, pPipeline->_renderPipeline );
            wgpuRenderPassEncoderSetStencilReference( _state._renderPass, pPipeline->_stencilRef );
            _state._boundRenderPSO = _state._graphicsPSO;
        }
        const bool bBound = _state._geometry.bindVertexBuffersForDraw( "WebGPU", pPipeline->_vertexLayout, [this]( uint32 slot, const RHIBufferBinding& binding )
        {
            const WebGPUBufferRecord* pBuffer = _pDevice->findBuffer( binding._buffer );
            if ( pBuffer == nullptr )
                return false;
            wgpuRenderPassEncoderSetVertexBuffer( _state._renderPass, slot, pBuffer->_buffer, binding._offset, pBuffer->_sizeBytes - binding._offset );
            return true;
        } );
        if ( bBound == false )
            return false;
        const WGPUBindGroup group = findBindGroup( *pPipeline, _state._graphics );
        if ( group != nullptr )
            wgpuRenderPassEncoderSetBindGroup( _state._renderPass, 0, group, 0, nullptr ); // 그룹은 캐시가 가진다 — 인코더가 기록한 동안 붙들고 있다(명세)
        return true;
    }

    bool WebGPURHICommandContext::prepareIndexBuffer()
    {
        return _state._geometry.bindIndexBufferForDraw( "WebGPU", [this]( const RHIBufferBinding& binding )
        {
            const WebGPUBufferRecord* pBuffer = _pDevice->findBuffer( binding._buffer );
            if ( pBuffer == nullptr )
                return false;
            const WGPUIndexFormat format = binding._stride == 2 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
            wgpuRenderPassEncoderSetIndexBuffer( _state._renderPass, pBuffer->_buffer, format, binding._offset, pBuffer->_sizeBytes - binding._offset );
            return true;
        } );
    }

    void WebGPURHICommandContext::drawNative( uint32 vertexCount, uint32 startVertex )
    {
        drawInstancedNative( vertexCount, 1, startVertex, 0 );
    }

    void WebGPURHICommandContext::drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        const bool bNothingToDraw = _state._renderPass == nullptr || vertexCount == 0 || instanceCount == 0;
        if ( bNothingToDraw || prepareDraw() == false )
            return;
        wgpuRenderPassEncoderDraw( _state._renderPass, vertexCount, instanceCount, startVertex, startInstance );
    }

    void WebGPURHICommandContext::drawIndexedInstancedNative( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance )
    {
        const bool bNothingToDraw = _state._renderPass == nullptr || indexCount == 0 || instanceCount == 0;
        if ( bNothingToDraw || prepareDraw() == false || prepareIndexBuffer() == false )
            return;
        wgpuRenderPassEncoderDrawIndexed( _state._renderPass, indexCount, instanceCount, startIndex, baseVertex, startInstance );
    }

    void WebGPURHICommandContext::drawIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        // 멀티 드로우 · count 버퍼가 없다(`_bMultiDrawIndirect` 거짓) — 인자마다 한 번씩 낸다(DX11 의 루프와 같다). count 버퍼를 넘긴 호출은 능력을 무시한 결함이다.
        (void)countBufferOffset;
        if ( countBuffer != 0 )
            SW_FATAL( "WebGPU drawIndirect with a count buffer - the backend reports no multi-draw-indirect" );
        const WebGPUBufferRecord* pArgs = _pDevice->findBuffer( argumentBuffer );
        if ( _state._renderPass == nullptr || pArgs == nullptr || prepareDraw() == false )
            return;
        for ( uint32 drawIndex = 0; drawIndex < drawCount; ++drawIndex )
        {
            wgpuRenderPassEncoderDrawIndirect( _state._renderPass, pArgs->_buffer, argumentBufferOffset + drawIndex * sizeof( RHIDrawIndirectCommand ) );
        }
    }

    void WebGPURHICommandContext::drawIndexedIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer,
                                                             uint32 countBufferOffset )
    {
        (void)countBufferOffset;
        if ( countBuffer != 0 )
            SW_FATAL( "WebGPU drawIndexedIndirect with a count buffer - the backend reports no multi-draw-indirect" );
        const WebGPUBufferRecord* pArgs = _pDevice->findBuffer( argumentBuffer );
        if ( _state._renderPass == nullptr || pArgs == nullptr || prepareDraw() == false || prepareIndexBuffer() == false )
            return;
        for ( uint32 drawIndex = 0; drawIndex < drawCount; ++drawIndex )
        {
            wgpuRenderPassEncoderDrawIndexedIndirect( _state._renderPass, pArgs->_buffer, argumentBufferOffset + drawIndex * sizeof( RHIDrawIndexedIndirectCommand ) );
        }
    }

    // ------------------------------------------------------------------------------
    // 계산
    // ------------------------------------------------------------------------------
    WGPUComputePassEncoder WebGPURHICommandContext::prepareDispatch()
    {
        const WebGPUPipelineRecord* pPipeline = _pDevice != nullptr ? _pDevice->_pipelines.get( _state._computePSO ) : nullptr;
        if ( pPipeline == nullptr || pPipeline->_computePipeline == nullptr )
            SW_FATAL( "WebGPU dispatch without a registered compute PSO (handle %#)", _state._computePSO );
        const WGPUComputePassEncoder pass = ensureComputePass();
        if ( pass == nullptr )
            return nullptr;
        wgpuComputePassEncoderSetPipeline( pass, pPipeline->_computePipeline );
        const WGPUBindGroup group = findBindGroup( *pPipeline, _state._compute );
        if ( group != nullptr )
            wgpuComputePassEncoderSetBindGroup( pass, 0, group, 0, nullptr );
        return pass;
    }

    void WebGPURHICommandContext::dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        const WGPUComputePassEncoder pass = prepareDispatch();
        if ( pass != nullptr )
            wgpuComputePassEncoderDispatchWorkgroups( pass, threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void WebGPURHICommandContext::dispatchIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        const WebGPUBufferRecord*    pArgs = _pDevice->findBuffer( argumentBuffer );
        const WGPUComputePassEncoder pass  = pArgs != nullptr ? prepareDispatch() : nullptr;
        if ( pass != nullptr )
            wgpuComputePassEncoderDispatchWorkgroupsIndirect( pass, pArgs->_buffer, argumentBufferOffset );
    }

    // ------------------------------------------------------------------------------
    // 패스 밖의 일
    // ------------------------------------------------------------------------------
    void WebGPURHICommandContext::blitTextureNative( RHITextureHandle src, RHITextureHandle dst )
    {
        endComputePass();
        const WGPUCommandEncoder encoder = getEncoder();
        if ( encoder == nullptr || ( src == 0 && dst == 0 ) )
            return;
        // 0 은 백버퍼다. 표면 텍스처는 CopySrc | CopyDst 로 구성한다(`WebGPURHISwapChain::configure`). 모양(형식 · 크기)이 같아야 한다 — 다르면 검증 오류로 드러난다.
        const auto resolve = [this]( RHITextureHandle handle, uint32& outWidth, uint32& outHeight ) -> WGPUTexture
        {
            if ( handle == 0 )
            {
                outWidth  = _pDevice->_swapChain.getWidth();
                outHeight = _pDevice->_swapChain.getHeight();
                return _pDevice->_swapChain.getCurrentTexture();
            }
            const WebGPUTextureRecord* pRecord = _pDevice->findTexture( handle );
            if ( pRecord == nullptr )
                return nullptr;
            outWidth  = pRecord->_desc._width;
            outHeight = pRecord->_desc._height;
            return pRecord->_texture;
        };
        uint32            width      = 0;
        uint32            height     = 0;
        uint32            dstWidth   = 0;
        uint32            dstHeight  = 0;
        const WGPUTexture srcTexture = resolve( src, width, height );
        const WGPUTexture dstTexture = resolve( dst, dstWidth, dstHeight );
        if ( srcTexture == nullptr || dstTexture == nullptr )
            return;
        WGPUTexelCopyTextureInfo source{};
        source.texture = srcTexture;
        WGPUTexelCopyTextureInfo target{};
        target.texture = dstTexture;
        const WGPUExtent3D extent{ MathUtil::min( width, dstWidth ), MathUtil::min( height, dstHeight ), 1 };
        wgpuCommandEncoderCopyTextureToTexture( encoder, &source, &target, &extent );
    }

    void WebGPURHICommandContext::prepareTextureForShaderReadNative( RHITextureHandle texture )
    {
        WebGPUTextureRecord* const pRecord = _pDevice != nullptr ? _pDevice->findTexture( texture ) : nullptr;
        if ( pRecord == nullptr || pRecord->_depthSampleTexture == nullptr )
            return; // 깊이가 아닌 텍스처는 구현이 상태를 따라간다
        if ( _state._renderPass != nullptr )
        {
            // 패스 안에서는 다른 렌더 패스(사본)를 열 수 없다 — 다른 백엔드도 패스 안의 전이는 결함이다(Vulkan 레이아웃 전이는 패스 밖이어야 한다).
            SW_LOG_ERROR_ONCE_ID( "WebGPU.DepthPrepareInsidePass", "WebGPU: prepareTextureForShaderRead on a depth texture inside an open render pass - the R32 copy is not refreshed" );
            return;
        }
        endComputePass();
        const WGPUCommandEncoder encoder = getEncoder();
        if ( encoder != nullptr )
            _pDevice->encodeDepthSampleCopy( encoder, *pRecord );
    }

    void WebGPURHICommandContext::writeTimestamp( uint32 slotIndex )
    {
        if ( _pDevice == nullptr )
            return;
        if ( _state._renderPass != nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "WebGPU.TimestampInsidePass", "WebGPU: writeTimestamp inside an open render pass - WebGPU writes timestamps only at pass boundaries" );
            return;
        }
        endComputePass();
        _pDevice->writeTimestampSlot( getEncoder(), slotIndex );
    }

    void WebGPURHICommandContext::transitionBufferNative( RHIBufferHandle buffer, RHIBufferState newState )
    {
        // 상태는 구현이 따라간다. 다만 계산 패스 안의 쓰기와 뒤 패스의 읽기 사이는 패스 경계가 동기화하므로, 읽기 상태로 돌리면 계산 패스를 닫는다.
        (void)buffer;
        if ( newState != RHIBufferState::UnorderedAccess )
            endComputePass();
    }

    void WebGPURHICommandContext::copyBufferToReadbackNative( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes )
    {
        endComputePass();
        const WGPUCommandEncoder  encoder        = getEncoder();
        const WebGPUBufferRecord* pSource        = _pDevice->findBuffer( source );
        WebGPUBufferRecord*       pTarget        = _pDevice->findBuffer( readback );
        const bool                bNothingToCopy = encoder == nullptr || pSource == nullptr || pTarget == nullptr || pTarget->_bReadback == SW_FALSE || sizeBytes == 0;
        if ( bNothingToCopy )
            return;
        // 매핑된 채로는 복사 도착지가 될 수 없다 — 지난 읽기를 놓는다(그 결과는 이미 `readReadbackBuffer` 가 읽었다).
        if ( pTarget->_bReadbackReady != SW_FALSE )
        {
            wgpuBufferUnmap( pTarget->_buffer );
            pTarget->_bReadbackReady = SW_FALSE;
        }
        const uint64 alignedSize = MathUtil::align( static_cast<uint64>( sizeBytes ), uint64{ 4 } ); // 복사 크기는 4 의 배수여야 한다(명세)
        wgpuCommandEncoderCopyBufferToBuffer( encoder, pSource->_buffer, 0, pTarget->_buffer, 0, MathUtil::min( alignedSize, pTarget->_sizeBytes ) );
        if ( pTarget->_bReadbackQueued == SW_FALSE )
        {
            pTarget->_bReadbackQueued = SW_TRUE;
            _pDevice->_listQueuedReadbackBuffer.push_back( readback );
        }
    }

    void WebGPURHICommandContext::beginEventMarker( const utf8* pName )
    {
        const WGPUCommandEncoder encoder = getEncoder();
        if ( _state._renderPass != nullptr )
            wgpuRenderPassEncoderPushDebugGroup( _state._renderPass, WebGPURHIUtil::toStringView( pName ) );
        else if ( _state._computePass != nullptr )
            wgpuComputePassEncoderPushDebugGroup( _state._computePass, WebGPURHIUtil::toStringView( pName ) );
        else if ( encoder != nullptr )
            wgpuCommandEncoderPushDebugGroup( encoder, WebGPURHIUtil::toStringView( pName ) );
    }

    void WebGPURHICommandContext::endEventMarker()
    {
        // 표지는 연 곳에서 닫아야 한다(검증 규칙) — 패스가 표지 사이에 열리고 닫히면 짝이 어긋나 검증 오류로 드러난다. 엔진은 표지를 패스 안에서 열고 닫는다.
        const WGPUCommandEncoder encoder = getEncoder();
        if ( _state._renderPass != nullptr )
            wgpuRenderPassEncoderPopDebugGroup( _state._renderPass );
        else if ( _state._computePass != nullptr )
            wgpuComputePassEncoderPopDebugGroup( _state._computePass );
        else if ( encoder != nullptr )
            wgpuCommandEncoderPopDebugGroup( encoder );
    }

    void WebGPURHICommandContext::writeBreadcrumb( uint32 markerID )
    {
        (void)markerID;
    }

    // ------------------------------------------------------------------------------
    // 커맨드 리스트
    // ------------------------------------------------------------------------------
    WebGPURHICommandList::WebGPURHICommandList( WebGPURHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _context{ pDevice, true }
    {
        _pContext = &_context;
        if ( _pDevice != nullptr )
            _pDevice->registerCommandList( this );
    }

    WebGPURHICommandList::~WebGPURHICommandList()
    {
        if ( _pDevice != nullptr )
            _pDevice->unregisterCommandList( this );
    }

    void WebGPURHICommandList::detachFromDevice()
    {
        _context.detachFromDevice();
        _pDevice = nullptr;
    }
} // namespace sw
