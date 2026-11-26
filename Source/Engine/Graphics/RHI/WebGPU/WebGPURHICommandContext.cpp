#include "pch.h"

#include "Engine/Graphics/RHI/WebGPU/WebGPURHICommandContext.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/RHI/WebGPU/WebGPURHIDevice.h"

namespace sw
{
    IRHIDevice& WebGPURHICommandContext::getContextDevice() const
    {
        return *_pDevice;
    }

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
        };
    } // namespace

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
        if ( _pDevice == nullptr )
            return nullptr;
        _pDevice->markFrameEncoderUsed();
        return _pDevice->getFrameEncoder();
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
        resetBindings();
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
    RHIRenderPassBeginResult WebGPURHICommandContext::beginRenderPassNative( const RHIRenderPassInfo& beginInfo, RHIRenderPassExtent& outTargetExtent )
    {
        endComputePass();
        const WGPUCommandEncoder encoder = getEncoder();
        if ( encoder == nullptr || _pDevice == nullptr )
            return RHIRenderPassBeginResult::Failed;

        WGPURenderPassColorAttachment arrColor[kMaxColorAttachments]{};
        uint32                        colorCount = 0;
        if ( beginInfo._bBindColor != SW_FALSE )
        {
            const uint32 bindCount = MathUtil::min( beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u, kMaxColorAttachments );
            for ( uint32 attachment = 0; attachment < bindCount; ++attachment )
            {
                WGPUTextureView        view   = nullptr;
                const RHITextureHandle handle = beginInfo._arrColorTarget[attachment];
                if ( handle == kRHIBackBufferTexture && attachment == 0 )
                {
                    // 백버퍼 — 이 프레임에 받은 스왑체인 텍스처다(`acquireSwapChainImage`). 없으면(최소화 · 다시 구성) 이 패스의 그리기를 알리지 않고 버린다.
                    const WebGPURHISwapChain* pSwapChain = beginInfo._pSwapChain != nullptr ? static_cast<const WebGPURHISwapChain*>( beginInfo._pSwapChain ) : &_pDevice->_swapChain;
                    view                                 = pSwapChain->getCurrentView();
                    if ( view == nullptr )
                        return RHIRenderPassBeginResult::NoBackBuffer;
                    outTargetExtent = RHIRenderPassExtent{ pSwapChain->getWidth(), pSwapChain->getHeight() };
                }
                else
                {
                    const WebGPUTextureRecord* pTexture = _pDevice->findTexture( handle );
                    const uint32               slice    = beginInfo._arrColorTargetSlice[attachment];
                    const uint32               mip      = beginInfo._arrColorTargetMip[attachment];
                    view                                = pTexture != nullptr ? pTexture->findAttachmentView( slice, mip ) : nullptr;
                    if ( view == nullptr )
                    {
                        if ( attachment > 0 )
                            break;
                        return RHIRenderPassBeginResult::Failed;
                    }
                    if ( attachment == 0 )
                        outTargetExtent = RHIRenderPassExtent{ MathUtil::max( pTexture->_desc._width >> mip, 1u ), MathUtil::max( pTexture->_desc._height >> mip, 1u ) };
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
            WebGPUTextureRecord* const pDepth    = _pDevice->findTexture( beginInfo._depthTarget );
            const WGPUTextureView      depthView = pDepth != nullptr ? pDepth->findAttachmentView( beginInfo._depthTargetSlice, beginInfo._depthTargetMip ) : nullptr;
            if ( depthView == nullptr )
                return RHIRenderPassBeginResult::Failed;
            ++pDepth->_depthWriteCount; // 셰이더가 읽는 R32 사본을 낡았다고 적는다(encodeDepthSampleCopy 가 다음 읽기 전에 옮긴다)
            bDepth                  = true;
            depth.view              = depthView;
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
            if ( colorCount == 0 )
                outTargetExtent = RHIRenderPassExtent{ MathUtil::max( pDepth->_desc._width >> beginInfo._depthTargetMip, 1u ), MathUtil::max( pDepth->_desc._height >> beginInfo._depthTargetMip, 1u ) };
        }

        WGPURenderPassDescriptor passDesc{};
        passDesc.colorAttachmentCount   = colorCount;
        passDesc.colorAttachments       = colorCount > 0 ? arrColor : nullptr;
        passDesc.depthStencilAttachment = bDepth ? &depth : nullptr;
        _state._renderPass              = wgpuCommandEncoderBeginRenderPass( encoder, &passDesc );
        _state._boundRenderPSO          = 0;
        if ( _state._renderPass == nullptr )
            return RHIRenderPassBeginResult::Failed;
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

    void WebGPURHICommandContext::setViewportNative( const RHIViewport& viewport )
    {
        if ( _state._renderPass != nullptr )
            wgpuRenderPassEncoderSetViewport( _state._renderPass, viewport._x, viewport._y, viewport._width, viewport._height, viewport._minDepth, viewport._maxDepth );
    }

    void WebGPURHICommandContext::setScissorRectNative( const RHIScissorRect& rect )
    {
        if ( _state._renderPass != nullptr )
            wgpuRenderPassEncoderSetScissorRect( _state._renderPass, rect._x, rect._y, rect._width, rect._height );
    }

    // ------------------------------------------------------------------------------
    // 슬롯
    // ------------------------------------------------------------------------------
    void WebGPURHICommandContext::updateConstantBufferNative( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        // 기록 중 갱신은 상수 링의 새 칸으로 이름을 바꾼다 — 같은 버퍼를 한 프레임에 여러 번 갱신해도 앞서 기록한 그리기는 제 칸을 읽는다(DX11 WRITE_DISCARD 와 같은 뜻).
        WebGPUBufferRecord* const pRecord   = _pDevice != nullptr ? _pDevice->findBuffer( buffer ) : nullptr;
        const bool                bNoUpdate = pRecord == nullptr || pData == nullptr || size == 0;
        if ( bNoUpdate )
            return;
        WGPUBuffer ringBuffer = nullptr;
        uint64     ringOffset = 0;
        // 묶음은 버퍼 크기만큼 건다(`findBindGroup`) — 일부만 갱신해도 칸은 버퍼 크기를 잡는다. 작게 잡으면 다음 칸을 읽고, 링 끝에서는 범위를 넘어 묶음 생성이 검증 오류다.
        _pDevice->allocateConstantSlice( pData, size, MathUtil::max( static_cast<uint64>( size ), pRecord->_sizeBytes ), ringBuffer, ringOffset );
        pRecord->_overrideSerial = _pDevice->getFrameSerial();
        pRecord->_overrideBuffer = ringBuffer;
        pRecord->_overrideOffset = ringOffset;
    }

    WGPUTextureView WebGPURHICommandContext::resolveSampledView( uint32 slot, RHIDescriptorIndex index ) const
    {
        const WebGPUBindlessEntry  entry    = _pDevice->findBindless( _pDevice->_textureTable, index );
        const WebGPUTextureRecord* pTexture = entry._texture != 0 ? _pDevice->findTexture( entry._texture ) : nullptr;
        if ( pTexture == nullptr )
        {
            if ( index != kInvalidDescriptorIndex )
                _pDevice->getDrawDiagnostics().reportDescriptorNotRegistered( "t", slot, index );
            return _pDevice->_defaultTextureView;
        }
        // 깊이 텍스처의 샘플 뷰는 R32 사본이다 — 깊이를 쓴 뒤 옮기지 않았으면 낡은 깊이(또는 0)를 읽는다. 부르는 쪽이 `prepareTextureForShaderRead` 를 빠뜨렸다.
        if ( pTexture->_depthSampleTexture != nullptr && pTexture->_depthCopySourceWriteCount != pTexture->_depthWriteCount )
            SW_LOG_ERROR_ONCE_ID( "WebGPU.DepthSampledWithoutPrepare", "WebGPU: a depth texture (%#x%#) is sampled without prepareTextureForShaderRead after a depth write - its R32 copy is stale",
                                  pTexture->_desc._width, pTexture->_desc._height );
        return pTexture->_sampleView;
    }

    // 길이 유지(긴 함수 정리 기준): 바인딩 번호의 띠(b · t · s · u)마다 한 갈래라 나누면 같은 표를 네 번 넘긴다.
    WGPUBindGroup WebGPURHICommandContext::findBindGroup( const WebGPUPipelineRecord& pipeline, RHIBindPoint point )
    {
        namespace wgpu = shaderslot::wgpu;
        using Internal = WebGPURHICommandContextInternal;
        if ( pipeline._groupLayout == nullptr || pipeline._listBinding.empty() )
            return nullptr;
        // 그룹은 슬롯 전부로 짓는다 — 바뀐 비트는 보지 않는다(같은 항목의 그룹은 캐시가 준다). 걸지 않은 칸은 아래에서 빈 자원이다.
        const RHIShaderSlots&      slots       = _bindings.getSlots( point );
        RHIRootConstants&          constants   = _bindings.getRootConstants( point );
        WebGPURootSlice&           rootSlice   = _state._arrRootSlice[static_cast<uint32>( point )];
        const RHIDrawDiagnostics&  diagnostics = _pDevice->getDrawDiagnostics();
        vector<WGPUBindGroupEntry> listEntry;
        listEntry.reserve( pipeline._listBinding.size() );
        for ( const uint32 binding : pipeline._listBinding )
        {
            if ( binding >= wgpu::kUShift )
            {
                const uint32              slot  = binding - wgpu::kUShift;
                const RHIDescriptorIndex  index = slot < RHIShaderSlots::kUnorderedAccessCount ? slots.get( RHIShaderSlotKind::UnorderedAccess, slot ) : kInvalidDescriptorIndex;
                const WebGPUBindlessEntry entry = _pDevice->findBindless( _pDevice->_uavTable, index );
                if ( entry._texture != 0 )
                {
                    const WebGPUTextureRecord* pTexture = _pDevice->findTexture( entry._texture );
                    if ( pTexture != nullptr && pTexture->_listSliceView.empty() == false )
                    {
                        listEntry.push_back( Internal::makeTextureEntry( binding, pTexture->_listSliceView[0] ) );
                        continue;
                    }
                }
                const WebGPUBufferRecord* pBuffer = entry._buffer != 0 ? _pDevice->_buffers.get( entry._buffer ) : nullptr;
                if ( pBuffer == nullptr )
                {
                    if ( index != kInvalidDescriptorIndex )
                        diagnostics.reportDescriptorNotRegistered( "u", slot, index );
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
                const RHIDescriptorIndex index      = slot < RHIShaderSlots::kShaderResourceCount ? slots.get( RHIShaderSlotKind::ShaderResource, slot ) : kInvalidDescriptorIndex;
                if ( bTextureAt )
                {
                    listEntry.push_back( Internal::makeTextureEntry( binding, resolveSampledView( slot, index ) ) );
                    continue;
                }
                const WebGPUBindlessEntry entry   = _pDevice->findBindless( _pDevice->_bufferTable, index );
                const WebGPUBufferRecord* pBuffer = entry._buffer != 0 ? _pDevice->_buffers.get( entry._buffer ) : nullptr;
                if ( pBuffer == nullptr && index != kInvalidDescriptorIndex )
                    diagnostics.reportDescriptorNotRegistered( "t", slot, index );
                // 안 걸린 읽기 버퍼는 빈 버퍼다 — 셰이더는 PassCB 의 인덱스가 무효면 읽지 않는다(DX11 · GL 의 빈 SRV 와 같다).
                listEntry.push_back( pBuffer != nullptr ? Internal::makeBufferEntry( binding, pBuffer->_buffer, 0, pBuffer->_sizeBytes )
                                                        : Internal::makeBufferEntry( binding, _pDevice->_defaultBuffer, 0, WGPU_WHOLE_SIZE ) );
                continue;
            }
            const uint32 slot = binding - wgpu::kBShift;
            if ( slot == shaderslot::kRootConstantEmulationSlot )
            {
                const bool bNeedSlice = constants.isDirty() || rootSlice._serial != _pDevice->getFrameSerial() || rootSlice._buffer == nullptr;
                if ( bNeedSlice )
                {
                    _pDevice->allocateConstantSlice( constants._arrDword, sizeof( constants._arrDword ), sizeof( constants._arrDword ), rootSlice._buffer, rootSlice._offset );
                    rootSlice._serial = _pDevice->getFrameSerial();
                    constants.markFlushed();
                }
                listEntry.push_back( Internal::makeBufferEntry( binding, rootSlice._buffer, rootSlice._offset, sizeof( constants._arrDword ) ) );
                continue;
            }
            const RHIDescriptorIndex  index   = slot < RHIShaderSlots::kConstantBufferCount ? slots.get( RHIShaderSlotKind::ConstantBuffer, slot ) : kInvalidDescriptorIndex;
            const WebGPUBindlessEntry entry   = _pDevice->findBindless( _pDevice->_bufferTable, index );
            const WebGPUBufferRecord* pBuffer = entry._buffer != 0 ? _pDevice->_buffers.get( entry._buffer ) : nullptr;
            if ( pBuffer == nullptr )
            {
                if ( index != kInvalidDescriptorIndex )
                    diagnostics.reportDescriptorNotRegistered( "b", slot, index );
                listEntry.push_back( Internal::makeBufferEntry( binding, _pDevice->_defaultBuffer, 0, WGPU_WHOLE_SIZE ) );
                continue;
            }
            // 상수 버퍼 페이지의 구간이면 그 오프셋 · 크기만 건다(감사 R-4). 구간은 팩토리 갱신만 받아 이름 바꾼 칸이 없다.
            if ( entry._sliceSize != 0 )
            {
                listEntry.push_back( Internal::makeBufferEntry( binding, pBuffer->_buffer, entry._sliceOffset, entry._sliceSize ) );
                continue;
            }
            const bool bRenamed = pBuffer->_overrideSerial == _pDevice->getFrameSerial() && pBuffer->_overrideBuffer != nullptr;
            listEntry.push_back( bRenamed ? Internal::makeBufferEntry( binding, pBuffer->_overrideBuffer, pBuffer->_overrideOffset, pBuffer->_sizeBytes )
                                          : Internal::makeBufferEntry( binding, pBuffer->_buffer, 0, pBuffer->_sizeBytes ) );
        }
        // 같은 (레이아웃, 항목) 은 한 번만 짓는다(프레임을 넘겨 든다) — 브라우저에서 그룹 하나는 JS 객체 하나라 그리기마다 지으면 GC 압력이 그리기 수만큼 는다.
        return _pDevice->_bindGroupCache.getOrCreate( _pDevice->_device, pipeline._groupLayout, listEntry );
    }

    // ------------------------------------------------------------------------------
    // 그리기
    // ------------------------------------------------------------------------------
    bool WebGPURHICommandContext::prepareDraw()
    {
        const WebGPUPipelineRecord* pPipeline = _pDevice->_pipelines.get( _bindings._graphicsPSO );
        // 백엔드 계약(다섯 백엔드 같음): 등록된 그래픽스 PSO 없이 그리면 오류로 멈춘다(부르는 쪽의 결함).
        if ( pPipeline == nullptr || pPipeline->_renderPipeline == nullptr )
            reportFatalError( "WebGPU draw without a registered graphics PSO (handle %#)", _bindings._graphicsPSO.getPacked() );
        if ( _state._boundRenderPSO != _bindings._graphicsPSO )
        {
            wgpuRenderPassEncoderSetPipeline( _state._renderPass, pPipeline->_renderPipeline );
            wgpuRenderPassEncoderSetStencilReference( _state._renderPass, pPipeline->_stencilRef );
            _state._boundRenderPSO = _bindings._graphicsPSO;
        }
        const bool bBound = _bindings._geometry.bindVertexBuffersForDraw( _pDevice->getDrawDiagnostics(), pPipeline->_vertexLayout, [this]( uint32 slot, const RHIBufferBinding& binding )
        {
            const WebGPUBufferRecord* pBuffer = _pDevice->findBuffer( binding._buffer );
            if ( pBuffer == nullptr )
                return false;
            wgpuRenderPassEncoderSetVertexBuffer( _state._renderPass, slot, pBuffer->_buffer, binding._offset, pBuffer->_sizeBytes - binding._offset );
            return true;
        } );
        if ( bBound == false )
            return false;
        const WGPUBindGroup group = findBindGroup( *pPipeline, RHIBindPoint::Graphics );
        if ( group != nullptr )
            wgpuRenderPassEncoderSetBindGroup( _state._renderPass, 0, group, 0, nullptr ); // 그룹은 캐시가 가진다 — 인코더가 기록한 동안 붙들고 있다(명세)
        return true;
    }

    bool WebGPURHICommandContext::prepareIndexBuffer()
    {
        return _bindings._geometry.bindIndexBufferForDraw( _pDevice->getDrawDiagnostics(), [this]( const RHIBufferBinding& binding )
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
            reportFatalError( "WebGPU drawIndirect with a count buffer - the backend reports no multi-draw-indirect" );
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
            reportFatalError( "WebGPU drawIndexedIndirect with a count buffer - the backend reports no multi-draw-indirect" );
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
        const WebGPUPipelineRecord* pPipeline = _pDevice != nullptr ? _pDevice->_pipelines.get( _bindings._computePSO ) : nullptr;
        if ( pPipeline == nullptr || pPipeline->_computePipeline == nullptr )
            reportFatalError( "WebGPU dispatch without a registered compute PSO (handle %#)", _bindings._computePSO.getPacked() );
        const WGPUComputePassEncoder pass = ensureComputePass();
        if ( pass == nullptr )
            return nullptr;
        wgpuComputePassEncoderSetPipeline( pass, pPipeline->_computePipeline );
        const WGPUBindGroup group = findBindGroup( *pPipeline, RHIBindPoint::Compute );
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
        if ( encoder == nullptr || ( src == kRHIBackBufferTexture && dst == kRHIBackBufferTexture ) )
            return;
        // `kRHIBackBufferTexture` 는 백버퍼다. 표면 텍스처는 CopySrc | CopyDst 로 구성한다(`WebGPURHISwapChain::configure`). 모양(형식 · 크기)이 같아야 한다 — 다르면 검증 오류로 드러난다.
        const auto resolve = [this]( RHITextureHandle handle, uint32& outWidth, uint32& outHeight ) -> WGPUTexture
        {
            if ( handle == kRHIBackBufferTexture )
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
        WebGPUTextureRecord* const pDstRecord = dst != kRHIBackBufferTexture ? _pDevice->findTexture( dst ) : nullptr;
        if ( pDstRecord != nullptr )
            ++pDstRecord->_depthWriteCount; // 깊이로 복사하면 그 R32 사본이 낡는다
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

    uint64 WebGPURHICommandContext::findBufferBytes( RHIBufferHandle buffer ) const
    {
        const WebGPUBufferRecord* const pRecord = _pDevice->findBuffer( buffer );
        return pRecord != nullptr ? pRecord->_sizeBytes : 0;
    }

    void WebGPURHICommandContext::copyBufferRegionNative( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes )
    {
        endComputePass();
        const WGPUCommandEncoder  encoder = getEncoder();
        const WebGPUBufferRecord* pSource = _pDevice->findBuffer( source );
        const WebGPUBufferRecord* pTarget = _pDevice->findBuffer( destination );
        if ( encoder == nullptr || pSource == nullptr || pTarget == nullptr )
            return;
        // 명세: 오프셋 · 크기는 4 의 배수다. 정점 흐름(12 B 위치 · 4 의 배수 속성)은 늘 맞는다 — 어긋나면 기록하지 않고 알린다(검증 오류로 장치를 잃지 않게).
        constexpr uint64 kCopyAlignment = 4;
        if ( sourceOffset % kCopyAlignment != 0 || destinationOffset % kCopyAlignment != 0 || sizeBytes % kCopyAlignment != 0 )
        {
            SW_LOG_ERROR_ONCE_ID( "WebGPURHICommandContext.CopyBufferRegionUnaligned", "copyBufferRegion: offsets and size must be multiples of 4 (src %#, dst %#, size %#)", sourceOffset, destinationOffset, sizeBytes );
            return;
        }
        wgpuCommandEncoderCopyBufferToBuffer( encoder, pSource->_buffer, sourceOffset, pTarget->_buffer, destinationOffset, sizeBytes );
    }

    void WebGPURHICommandContext::beginEventMarker( const utf8* pName )
    {
        // 표지는 연 곳에서 닫아야 한다(https://www.w3.org/TR/webgpu/#dom-gpudebugcommandsmixin-popdebuggroup — 빈 스택의 pop 은 인코더를 무효로 만든다).
        // 계산 패스는 디스패치가 처음 나올 때 이 컨텍스트가 연다 — 표지를 연 쪽은 그 경계를 모른다. 그래서 렌더 패스 밖의 표지는 늘 인코더에 걸고, 열린 계산 패스는 먼저 닫는다.
        if ( _state._renderPass != nullptr )
        {
            wgpuRenderPassEncoderPushDebugGroup( _state._renderPass, WebGPURHIUtil::toStringView( pName ) );
            return;
        }
        endComputePass();
        const WGPUCommandEncoder encoder = getEncoder();
        if ( encoder != nullptr )
            wgpuCommandEncoderPushDebugGroup( encoder, WebGPURHIUtil::toStringView( pName ) );
    }

    void WebGPURHICommandContext::endEventMarker()
    {
        // 여는 쪽과 같은 규칙이다 — 렌더 패스를 걸쳐 열고 닫는 표지만 짝이 어긋난다(부르는 쪽의 결함, 검증 오류로 드러난다).
        if ( _state._renderPass != nullptr )
        {
            wgpuRenderPassEncoderPopDebugGroup( _state._renderPass );
            return;
        }
        endComputePass();
        const WGPUCommandEncoder encoder = getEncoder();
        if ( encoder != nullptr )
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
