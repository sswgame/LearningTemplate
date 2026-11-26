#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX11/D3D11RHICommandContext.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{

    IRHIDevice& D3D11RHICommandContext::getContextDevice() const
    {
        return *_pDevice;
    }

    D3D11RHICommandContext::D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext )
        : _pDevice{ pDevice }
        , _pContext{ pContext }
        , _pState{ pDevice != nullptr ? &pDevice->_recordingState : nullptr }
        , _bAnnotationQueried{ SW_FALSE }
        , _bNullBeforeOffsetRebind{ SW_FALSE }
    {
        initializeConstantBufferBinding();
    }

    D3D11RHICommandContext::D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext,
                                                    D3D11RecordingState* pState )
        : _pDevice{ pDevice }
        , _pContext{ pContext }
        , _pState{ pState }
        , _bAnnotationQueried{ SW_FALSE }
        , _bNullBeforeOffsetRebind{ SW_FALSE }
    {
        initializeConstantBufferBinding();
    }

    void D3D11RHICommandContext::initializeConstantBufferBinding()
    {
        if ( _pContext == nullptr )
            return;
        // 장치 초기화가 D3D11.1 구간 능력을 요구하므로 Windows 8+ 런타임이다 — 실패는 오류로 남기고 구간 바인딩만 건너뛴다.
        (void)isHrSuccess( _pContext->QueryInterface( IID_PPV_ARGS( _context1.GetAddressOf() ) ), "QueryInterface (ID3D11DeviceContext1)" );
        const bool bEmulatedDeferred = _pContext->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED && _pDevice != nullptr && _pDevice->_bDriverCommandLists == SW_FALSE;
        _bNullBeforeOffsetRebind     = bEmulatedDeferred ? SW_TRUE : SW_FALSE;
    }

    void D3D11RHICommandContext::blitTextureNative( RHITextureHandle src, RHITextureHandle dst )
    {
        if ( _pContext == nullptr || ( src == kRHIBackBufferTexture && dst == kRHIBackBufferTexture ) )
            return;

        if ( src == kRHIBackBufferTexture )
        {
            // 백버퍼 → 텍스처. CopyResource 는 포맷 · 크기가 같아야 한다(받는 쪽은 getBackBufferFormat 으로 만든다).
            const Microsoft::WRL::ComPtr<ID3D11Texture2D> backTex                = _pDevice->_swapChain.getBackBufferTexture();
            const D3D11RHIDevice::TextureRecord*          pDstRecord             = _pDevice->resolveTexture( dst );
            const bool                                    bInvalidBackBufferBlit = backTex == nullptr || pDstRecord == nullptr || pDstRecord->_texture == nullptr || pDstRecord->_bDepth != SW_FALSE;
            if ( bInvalidBackBufferBlit )
                return;
            D3D11_TEXTURE2D_DESC backDesc{};
            D3D11_TEXTURE2D_DESC dstDesc{};
            backTex->GetDesc( &backDesc );
            pDstRecord->_texture->GetDesc( &dstDesc );
            const bool bSameShape = backDesc.Format == dstDesc.Format && backDesc.Width == dstDesc.Width && backDesc.Height == dstDesc.Height;
            if ( bSameShape == false )
            {
                SW_LOG_ERROR_ID( "D3D11RHICommandContext.BlitTextureBackbufferTargetMust", "blitTexture(backbuffer): the target must match the back buffer (fmt %# %#x%# vs %# %#x%#)", static_cast<uint32>( backDesc.Format ),
                                 backDesc.Width, backDesc.Height, static_cast<uint32>( dstDesc.Format ), dstDesc.Width, dstDesc.Height );
                return;
            }
            _pContext->CopyResource( pDstRecord->_texture.Get(), backTex.Get() );
            return;
        }

        const D3D11RHIDevice::TextureRecord* pSrcRecord            = _pDevice->resolveTexture( src );
        const bool                           bInvalidSourceTexture = pSrcRecord == nullptr || pSrcRecord->_texture == nullptr || pSrcRecord->_bDepth != SW_FALSE;
        if ( bInvalidSourceTexture )
            return;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> dstTex;
        if ( dst == kRHIBackBufferTexture )
        {
            dstTex = _pDevice->_swapChain.getBackBufferTexture();
            if ( dstTex == nullptr )
                return;
        }
        else
        {
            const D3D11RHIDevice::TextureRecord* pDstRecord                 = _pDevice->resolveTexture( dst );
            const bool                           bInvalidDestinationTexture = pDstRecord == nullptr || pDstRecord->_texture == nullptr || pDstRecord->_bDepth != SW_FALSE;
            if ( bInvalidDestinationTexture )
                return;
            dstTex = pDstRecord->_texture;
        }

        _pContext->CopyResource( dstTex.Get(), pSrcRecord->_texture.Get() );
    }

    void D3D11RHICommandContext::beginRecording()
    {
        resetBindings();
        onNativeStateCleared();
    }

    void D3D11RHICommandContext::onNativeStateCleared()
    {
        markBindingsDirty();
        _pState->_constantBuffers.invalidate();
        _pState->_boundNativeGraphicsPSO = 0;
        _pState->_boundNativeComputePSO  = 0;
        _pState->_pixelSRVMask           = 0;
        Memory::set( _pState->_arrPixelSRVTexture, 0, sizeof( _pState->_arrPixelSRVTexture ) );
        Memory::set( _pState->_arrComputeUAVBuffer, 0, sizeof( _pState->_arrComputeUAVBuffer ) );
    }

    void D3D11RHICommandContext::syncNativeState()
    {
        if ( _pState != &_pDevice->_recordingState )
            return; // 리스트의 Deferred Context 는 이 컨텍스트만 쓴다
        const uint32 serial = _pDevice->_nativeContextSerial.load();
        if ( serial == _pState->_nativeContextSerialSeen )
            return;
        onNativeStateCleared();
        _pState->_nativeContextSerialSeen = serial;
    }

    void D3D11RHICommandContext::bindComputePipelineForDispatch()
    {
        // 백엔드 계약(네 백엔드 같음): 등록된 컴퓨트 PSO 없이 디스패치하면 오류로 멈춘다. 앞 디스패치의 셰이더로 대신 돌지 않고 조용히 건너뛰지도 않는다 — PSO 가 0 이면
        // 부르는 쪽이 디스패치를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · 컴퓨트 PSO 를 걸지 않은 디스패치).
        const D3D11RHIDevice::D3D11PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( _bindings._computePSO );
        if ( pRecord == nullptr || pRecord->_cs == nullptr )
            reportFatalError( "D3D11 dispatch without a registered compute PSO (handle %#)", _bindings._computePSO.getPacked() );
        syncNativeState();
        if ( _pState->_boundNativeComputePSO != _bindings._computePSO )
        {
            _pContext->CSSetShader( pRecord->_cs.Get(), nullptr, 0 );
            _pState->_boundNativeComputePSO = _bindings._computePSO;
        }
        flushShaderSlots( RHIBindPoint::Compute );
        flushRootConstants( RHIBindPoint::Compute );
    }

    RHIRenderPassBeginResult D3D11RHICommandContext::beginRenderPassNative( const RHIRenderPassInfo& beginInfo, RHIRenderPassExtent& outTargetExtent )
    {
        if ( _pContext == nullptr )
            return RHIRenderPassBeginResult::Failed;

        ID3D11RenderTargetView* arrRTV[kMaxColorAttachments]{};
        uint32                  rtCount{ 0 };
        const bool              bBackBuffer = beginInfo._bBindColor != SW_FALSE && beginInfo._arrColorTarget[0] == kRHIBackBufferTexture;
        D3D11RHISwapChain&      swapChain   = bBackBuffer ? _pDevice->useSwapChain( beginInfo._pSwapChain ) : _pDevice->_swapChain;
        outTargetExtent                     = RHIRenderPassExtent{ swapChain.getWidth(), swapChain.getHeight() };
        if ( beginInfo._bBindColor != SW_FALSE )
        {
            const uint32 colorBindCount = beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u;
            for ( uint32 attachmentIndex = 0; attachmentIndex < colorBindCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
            {
                const RHITextureHandle  colorHandle = beginInfo._arrColorTarget[attachmentIndex];
                ID3D11RenderTargetView* pRTV{ nullptr };
                if ( colorHandle == kRHIBackBufferTexture )
                    pRTV = ( attachmentIndex == 0 ) ? swapChain.getBackBufferRTV() : nullptr;
                else
                {
                    D3D11RHIDevice::TextureRecord* pTexture = _pDevice->resolveTexture( colorHandle );
                    if ( pTexture != nullptr )
                        pRTV = pTexture->findSliceRTV( beginInfo._arrColorTargetSlice[attachmentIndex], beginInfo._arrColorTargetMip[attachmentIndex] );
                    if ( pTexture != nullptr && attachmentIndex == 0 )
                        outTargetExtent = pTexture->getMipExtent( beginInfo._arrColorTargetMip[attachmentIndex] );
                }
                if ( pRTV == nullptr && attachmentIndex > 0 )
                    break;
                arrRTV[rtCount++] = pRTV;

                const RHIRenderPassLoadOp loadOp = beginInfo._arrLoadOp[attachmentIndex];
                const float32*            pClear = &beginInfo._arrClearColor[attachmentIndex]._x;
                if ( loadOp == RHIRenderPassLoadOp::Clear && pRTV != nullptr )
                    _pContext->ClearRenderTargetView( pRTV, pClear );
            }
        }

        ID3D11DepthStencilView* pDSV{ nullptr };
        if ( beginInfo._depthTarget != 0 )
        {
            D3D11RHIDevice::TextureRecord* pDepthTex = _pDevice->resolveTexture( beginInfo._depthTarget );
            if ( pDepthTex != nullptr )
                pDSV = pDepthTex->findSliceDSV( beginInfo._depthTargetSlice, beginInfo._depthTargetMip );
            if ( pDepthTex != nullptr && beginInfo._bBindColor == SW_FALSE )
                outTargetExtent = pDepthTex->getMipExtent( beginInfo._depthTargetMip );
        }

        if ( beginInfo._depthLoadOp == RHIRenderPassLoadOp::Clear && pDSV != nullptr )
            _pContext->ClearDepthStencilView( pDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, beginInfo._clearDepth, beginInfo._clearStencil );

        if ( rtCount > 0 )
            _pContext->OMSetRenderTargets( rtCount, arrRTV, pDSV );
        else
            _pContext->OMSetRenderTargets( 0, nullptr, pDSV );
        _pState->_boundRenderTargetCount = rtCount;

        return RHIRenderPassBeginResult::Begun;
    }

    /**
     * @brief D3D11 에는 끝낼 렌더 패스가 없습니다. 비어 있는 것이 맞습니다.
     * @details 패스 규칙(열림 상태, 패스 밖 일 전에 닫기, 패스 없는 드로우 버리기)은 `IRHICommandContext` 가 네 백엔드에 같이 지킨다 — 여기는 네이티브로 할 일만 한다.
     *          Vulkan · DX12 는 네이티브 패스를 닫고, GL 은 기본 프레임버퍼와 `glClipControl` 을 되돌린다. D3D11 은 즉시 모드라 패스 객체가 없고 렌더 타깃도 그대로 둔다.
     *          끝난 타깃을 컴퓨트 UAV 로 거는 해저드는 `bindComputeUAV` 가 RT 를 떼서 막는다.
     */
    void D3D11RHICommandContext::endRenderPassNative()
    {
    }

    void D3D11RHICommandContext::uavBarrierNative( RHIBufferHandle buffer )
    {
        // D3D11 은 같은 컨텍스트의 디스패치를 순서대로 실행하고 UAV 위험도 드라이버가 처리한다.
        // 명시적 배리어 개념 자체가 없다. 의도적으로 아무것도 하지 않는다.
        (void)buffer;
    }

    void D3D11RHICommandContext::uavBarrierTextureNative( RHITextureHandle texture )
    {
        // 버퍼 판과 같다 — 컨텍스트가 디스패치를 순서대로 실행하고 UAV 위험을 드라이버가 처리한다.
        (void)texture;
    }

    void D3D11RHICommandContext::transitionBufferNative( RHIBufferHandle buffer, RHIBufferState newState )
    {
        // D3D11 에 배리어는 없다. 하지만 "이제 읽는다" 는 요청이 할 일 없는 것은 아니다. 같은 리소스를
        // 출력(UAV)과 입력(SRV)에 동시에 걸 수 없어서, UAV 를 안 떼면 런타임이 SRV 를 NULL 로 강제하고
        // 경고만 낸다. 정점 셰이더는 0 을 읽고 화면에서 통째로 사라진다(예: 인스턴스 버퍼 t4).
        // 그래서 읽기 상태로 돌릴 때 이 버퍼가 걸린 CS UAV 슬롯을 여기서 뗀다.
        // 상태마다 뗄지를 열거로 정한다 — `default:` 가 없어 새 상태가 생기면 이 switch 가 컴파일 경고(-Werror=switch)로 결정을 요구한다.
        bool bReleasesUAV{ true };
        switch ( newState )
        {
            case RHIBufferState::UnorderedAccess:
            {
                bReleasesUAV = false; // 쓰기로 돌아갈 때는 bindComputeUAV 가 다시 건다
                break;
            }
            case RHIBufferState::Common:
            case RHIBufferState::ShaderResource:
            case RHIBufferState::IndirectArgument:
            case RHIBufferState::CopyDest:
            case RHIBufferState::VertexOrConstant:
            case RHIBufferState::Index:
            case RHIBufferState::CopySource:
            {
                break; // 읽기 · 복사 상태는 UAV 에서 뗀다
            }
        }
        const bool bNoUAVToRelease = _pContext == nullptr || _pState == nullptr || buffer == 0 || bReleasesUAV == false;
        if ( bNoUAVToRelease )
            return;

        ID3D11UnorderedAccessView* pNull{ nullptr };
        for ( uint32 slot = 0; slot < D3D11_PS_CS_UAV_REGISTER_COUNT; ++slot )
        {
            if ( _pState->_arrComputeUAVBuffer[slot] != buffer )
                continue;
            _pContext->CSSetUnorderedAccessViews( slot, 1, &pNull, nullptr );
            _pState->_arrComputeUAVBuffer[slot] = 0;
            if ( slot < RHIShaderSlots::kUnorderedAccessCount )
                _bindings.getSlots( RHIBindPoint::Compute ).markDirty( RHIShaderSlotKind::UnorderedAccess, slot ); // 걸린 채면 다음 디스패치가 다시 건다
        }
    }

    void D3D11RHICommandContext::copyBufferToReadbackNative( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes )
    {
        ID3D11Buffer* pSource        = _pDevice->resolveBuffer( source );
        ID3D11Buffer* pTarget        = _pDevice->resolveBuffer( readback );
        const bool    bNothingToCopy = _pContext == nullptr || pSource == nullptr || pTarget == nullptr || sizeBytes == 0;
        if ( bNothingToCopy )
            return;
        const D3D11_BOX region{ 0, 0, 0, sizeBytes, 1, 1 };
        _pContext->CopySubresourceRegion( pTarget, 0, 0, 0, 0, pSource, 0, &region );
    }

    uint64 D3D11RHICommandContext::findBufferBytes( RHIBufferHandle buffer ) const
    {
        ID3D11Buffer* const pBuffer = _pDevice->resolveBuffer( buffer );
        if ( pBuffer == nullptr )
            return 0;
        D3D11_BUFFER_DESC desc{};
        pBuffer->GetDesc( &desc );
        return desc.ByteWidth;
    }

    void D3D11RHICommandContext::copyBufferRegionNative( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes )
    {
        ID3D11Buffer* pSource = _pDevice->resolveBuffer( source );
        ID3D11Buffer* pTarget = _pDevice->resolveBuffer( destination );
        if ( _pContext == nullptr || pSource == nullptr || pTarget == nullptr )
            return;
        // DX11 은 상태가 없다 — 즉시 컨텍스트가 순서를 지킨다. 박스는 원본 바이트 구간이다(버퍼는 1 차원).
        const D3D11_BOX region{ static_cast<UINT>( sourceOffset ), 0, 0, static_cast<UINT>( sourceOffset + sizeBytes ), 1, 1 };
        _pContext->CopySubresourceRegion( pTarget, 0, static_cast<UINT>( destinationOffset ), 0, 0, pSource, 0, &region );
    }

    void D3D11RHICommandContext::setGraphicsUnorderedAccess( uint32 slot, ID3D11UnorderedAccessView* pUAV )
    {
        // 픽셀 셰이더의 u 번호는 렌더 타깃 칸과 같은 자리다 — UAVStartSlot 은 건 렌더 타깃 수 이상이어야 한다. 모자라면 걸지 않는다(셰이더의 쓰기는 버려진다).
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargetsandunorderedaccessviews
        if ( slot < _pState->_boundRenderTargetCount || slot >= D3D11_PS_CS_UAV_REGISTER_COUNT )
        {
            if ( pUAV != nullptr )
                SW_LOG_ERROR_ONCE_ID( "D3D11RHICommandContext.GraphicsUAVBehindTargets", "bindGraphicsUAV: u%# is not behind the %# bound render targets - D3D11 pixel shader UAVs share the render target slots, the writes are dropped",
                                      slot, _pState->_boundRenderTargetCount );
            return;
        }
        // 렌더 타깃 · 깊이는 그대로 두고(KEEP) UAV 하나만 건다. 같은 버퍼가 CS UAV 로 걸려 있으면 런타임이 그쪽을 뗀다.
        _pContext->OMSetRenderTargetsAndUnorderedAccessViews( D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, slot, 1, &pUAV, nullptr );
        _pState->_arrComputeUAVBuffer[slot] = 0; // CS 쪽 기록은 런타임이 뗐다
        _bindings.getSlots( RHIBindPoint::Compute ).markDirty( RHIShaderSlotKind::UnorderedAccess, slot );
    }

    void D3D11RHICommandContext::flushShaderSlots( RHIBindPoint point )
    {
        RHIShaderSlots&            slots       = _bindings.getSlots( point );
        const bool                 bCompute    = point == RHIBindPoint::Compute;
        const RHIDrawDiagnostics&  diagnostics = _pDevice->getDrawDiagnostics();
        ID3D11UnorderedAccessView* pNullUAV{ nullptr };
        if ( slots.isDirty( RHIShaderSlotKind::ConstantBuffer ) )
        {
            slots.flush( RHIShaderSlotKind::ConstantBuffer, [this, point]( uint32 slot, RHIDescriptorIndex index )
            { bindConstantBufferAt( index, slot, point ); } );
        }
        // u 를 t 보다 먼저 낸다 — 앞 PSO 의 u 로 남은 버퍼를 이번 PSO 가 t 로 읽으면 런타임이 SRV 를 비운다.
        if ( slots.isDirty( RHIShaderSlotKind::UnorderedAccess ) )
        {
            // 컴퓨트는 렌더 패스 밖에서 돈다 — 앞 패스의 렌더 타깃이 이 UAV 의 텍스처면(깊이 히스토리) 런타임이 RT 를 강제로 떼며 오류를 낸다. 먼저 뗀다.
            if ( bCompute )
            {
                _pContext->OMSetRenderTargets( 0, nullptr, nullptr );
                _pState->_boundRenderTargetCount = 0;
            }
            slots.flush( RHIShaderSlotKind::UnorderedAccess, [&]( uint32 slot, RHIDescriptorIndex index )
            {
                Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
                if ( index != kInvalidDescriptorIndex )
                {
                    uav = _pDevice->bindlessUAVAt( RHIUnorderedAccessIndex{ index } );
                    if ( uav == nullptr )
                        diagnostics.reportDescriptorNotRegistered( "u", slot, index );
                }
                ID3D11UnorderedAccessView* pUAV = uav.Get();
                if ( bCompute == false )
                {
                    setGraphicsUnorderedAccess( slot, pUAV );
                    return;
                }
                _pContext->CSSetUnorderedAccessViews( slot, 1, pUAV != nullptr ? &pUAV : &pNullUAV, nullptr );
                // 어느 버퍼가 어느 슬롯에 걸렸는지 남긴다. transitionBuffer 가 이것을 보고 뗀다.
                _pState->_arrComputeUAVBuffer[slot] = pUAV != nullptr ? _pDevice->uavSourceBufferAt( RHIUnorderedAccessIndex{ index } ) : RHIBufferHandle{ 0 };
            } );
        }
        if ( slots.isDirty( RHIShaderSlotKind::ShaderResource ) == false )
            return;
        slots.flush( RHIShaderSlotKind::ShaderResource, [&]( uint32 slot, RHIDescriptorIndex index )
        {
            ID3D11ShaderResourceView* pSRV{ nullptr };
            RHITextureHandle          texture{ 0 };
            if ( index != kInvalidDescriptorIndex )
            {
                if ( slot < RHIShaderSlots::kBufferResourceCount )
                {
                    pSRV = findBindlessBufferSRV( RHIBufferSRVIndex{ index } );
                }
                else
                {
                    texture                                            = _pDevice->bindlessTextureAt( RHITextureSRVIndex{ index } );
                    const D3D11RHIDevice::TextureRecord* const pRecord = texture != 0 ? _pDevice->resolveTexture( texture ) : nullptr;
                    pSRV                                               = pRecord != nullptr ? pRecord->_srv.Get() : nullptr;
                }
                if ( pSRV == nullptr )
                    diagnostics.reportDescriptorNotRegistered( "t", slot, index );
            }
            if ( bCompute )
            {
                _pContext->CSSetShaderResources( slot, 1, &pSRV );
                return;
            }
            setGraphicsShaderResource( slot, pSRV );
            _pState->_arrPixelSRVTexture[slot] = pSRV != nullptr ? texture : RHITextureHandle{ 0 };
        } );
    }

    void D3D11RHICommandContext::setGraphicsShaderResource( uint32 slot, ID3D11ShaderResourceView* pSRV )
    {
        _pContext->PSSetShaderResources( slot, 1, &pSRV );
        const uint64 bit       = uint64{ 1 } << slot;
        _pState->_pixelSRVMask = pSRV != nullptr ? ( _pState->_pixelSRVMask | bit ) : ( _pState->_pixelSRVMask & ~bit );
        // 구조 버퍼는 모든 스테이지가 읽는다(변위 머티리얼의 헐 · 도메인이 머티리얼 · 인스턴스 버퍼를 읽는다). 머티리얼 텍스처는 정점 셰이더도 읽고(툰 외곽선 두께 텍스처 —
        // MToon), 변위 머티리얼의 높이 맵은 도메인이 읽는다. 헐은 읽지 않지만 같은 자리를 함께 건다 — 스테이지마다 다른 표를 두지 않는다. 다른 세 백엔드는 모든 스테이지가 본다.
        const bool bBufferSlot   = slot < RHIShaderSlots::kBufferResourceCount;
        const bool bMaterialSlot = shaderslot::kMaterialTexture0 <= slot && slot < shaderslot::kMaterialTexture0 + shaderslot::kMaterialTextureCount;
        if ( bBufferSlot || bMaterialSlot )
        {
            _pContext->VSSetShaderResources( slot, 1, &pSRV );
            _pContext->HSSetShaderResources( slot, 1, &pSRV );
            _pContext->DSSetShaderResources( slot, 1, &pSRV );
        }
        // 엔진 칸 중 정점 단계가 읽는 칸(표면 변형 층 — 지면 · 지형이 정점을 내린다)도 정점 단계에 건다. 칸은 표가 정한다(shaderslot::emulated).
        if ( slot == shaderslot::emulated::kVertexStageEngineTexture )
            _pContext->VSSetShaderResources( slot, 1, &pSRV );
        // 짝 샘플러는 걸지 않는다 — DX11 샘플러 슬롯은 16 개뿐이라 엔진 텍스처 칸(t32..)과 머티리얼 칸(t16..t31)은 셰이더가 정적 세트(s9..s15)의 샘플러로 읽는다.
    }

    bool D3D11RHICommandContext::bindGraphicsPipelineForDraw()
    {
        const D3D11RHIDevice::D3D11PipelineStateRecord* pPSO = _pDevice->_pipelineStates.get( _bindings._graphicsPSO );
        // 백엔드 계약(네 백엔드 같음): 등록된 그래픽스 PSO 없이 그리면 오류로 멈춘다. 기본 상태로 대신 그리지 않는다 — PSO 가 0 이면 부르는 쪽이
        // 드로우를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · 컴퓨트 PSO 를 그래픽스로 건 것).
        if ( pPSO == nullptr || pPSO->_vs == nullptr )
            reportFatalError( "D3D11 draw without a registered graphics PSO (handle %#)", _bindings._graphicsPSO.getPacked() );

        // PSO 가 읽는 슬롯에만 버퍼를 건다(정점 입력이 없는 셰이더는 레이아웃이 비어 아무 슬롯도 읽지 않는다). 건 버퍼가 부서졌으면(세대가 달라 풀리지 않는다)
        // 드로우를 버린다 — IASetVertexBuffers 를 건너뛰면 직전 드로우의 버퍼로 그린다.
        const bool bBound = _bindings._geometry.bindVertexBuffersForDraw( _pDevice->getDrawDiagnostics(), pPSO->_vertexLayout, [this]( uint32 slot, const RHIBufferBinding& binding )
        {
            ID3D11Buffer* const pVb = _pDevice->resolveBuffer( binding._buffer );
            if ( pVb == nullptr )
                return false;
            UINT stride = binding._stride;
            UINT offset = binding._offset;
            _pContext->IASetVertexBuffers( slot, 1, &pVb, &stride, &offset );
            return true;
        } );
        if ( bBound == false )
            return false;

        syncNativeState();
        if ( _pState->_boundNativeGraphicsPSO != _bindings._graphicsPSO )
        {
            // PS · 선택 스테이지는 없는 PSO 에서 비운다 — 남겨 두면 다음 드로우가 앞 PSO 의 스테이지를 지난다(깊이 전용 PSO 는 픽셀 스테이지가 없다).
            _pContext->IASetInputLayout( pPSO->_inputLayout.Get() );
            _pContext->IASetPrimitiveTopology( pPSO->_topology );
            _pContext->GSSetShader( pPSO->_gs.Get(), nullptr, 0 );
            _pContext->HSSetShader( pPSO->_hs.Get(), nullptr, 0 );
            _pContext->DSSetShader( pPSO->_ds.Get(), nullptr, 0 );
            _pContext->VSSetShader( pPSO->_vs.Get(), nullptr, 0 );
            _pContext->PSSetShader( pPSO->_ps.Get(), nullptr, 0 );
            if ( pPSO->_rasterizerState )
                _pContext->RSSetState( pPSO->_rasterizerState.Get() );
            if ( pPSO->_blendState )
            {
                constexpr float32 arrBlendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                _pContext->OMSetBlendState( pPSO->_blendState.Get(), arrBlendFactor, MathUtil::kMaxUInt32 );
            }
            if ( pPSO->_depthStencilState )
                _pContext->OMSetDepthStencilState( pPSO->_depthStencilState.Get(), pPSO->_stencilRef );
            _pState->_boundNativeGraphicsPSO = _bindings._graphicsPSO;
        }
        flushShaderSlots( RHIBindPoint::Graphics );
        flushRootConstants( RHIBindPoint::Graphics );
        return true;
    }

    template <bool bIndexed, typename TIssue>
    void D3D11RHICommandContext::drawWith( TIssue&& issue )
    {
        if ( bindGraphicsPipelineForDraw() == false )
            return;
        if constexpr ( bIndexed )
        {
            if ( bindIndexBufferForDraw() == false )
                return;
        }
        issue( *_pContext );
    }

    void D3D11RHICommandContext::drawNative( uint32 vertexCount, uint32 startVertex )
    {
        if ( _pContext == nullptr || vertexCount == 0 )
            return;
        drawWith<false>( [&]( ID3D11DeviceContext& context )
        { context.Draw( vertexCount, startVertex ); } );
    }

    void D3D11RHICommandContext::drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        const bool bNothingToDraw = _pContext == nullptr || vertexCount == 0 || instanceCount == 0;
        if ( bNothingToDraw )
            return;

        drawWith<false>( [&]( ID3D11DeviceContext& context )
        { context.DrawInstanced( vertexCount, instanceCount, startVertex, startInstance ); } );
    }

    void D3D11RHICommandContext::updateConstantBufferNative( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        const bool bInvalidConstantUpdate = _pContext == nullptr || _pDevice == nullptr || buffer == 0 || pData == nullptr;
        if ( bInvalidConstantUpdate )
            return;
        ID3D11Buffer* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // 리스트의 Deferred Context 는 이 리스트만 쓰므로 잠그지 않는다. 프레임 스트림(즉시 컨텍스트)은 다른 스레드의
        // 전송 제출이 같은 컨텍스트를 쓸 수 있어 Map · Unmap 짝을 같은 가드로 묶는다.
        const bool bFrameStream = _pState == &_pDevice->_recordingState;
        if ( bFrameStream == false )
        {
            // Map 실패(장치 제거 · 메모리 부족)는 오류로 남긴다 — 조용히 건너뛰면 이전 프레임의 상수로 그려 원인이 화면에서만 보인다.
            // 장치 제거는 다음 Present 가 분실로 처리하므로 여기서 멈추지 않는다(아래도 같다).
            if ( isHrSuccess( _pContext->Map( pResource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ), "ID3D11DeviceContext::Map (deferred constant buffer)" ) )
            {
                Memory::copy( mapped.pData, pData, size );
                _pContext->Unmap( pResource, 0 );
            }
            return;
        }
        const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
        if ( isHrSuccess( context->Map( pResource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ), "ID3D11DeviceContext::Map (constant buffer)" ) )
        {
            Memory::copy( mapped.pData, pData, size );
            context->Unmap( pResource, 0 );
        }
    }

    void D3D11RHICommandContext::bindConstantBufferAt( RHIDescriptorIndex index, uint32 slot, RHIBindPoint point )
    {
        const bool           bBound = index != kInvalidDescriptorIndex;
        const RHIBufferSlice source = bBound ? _pDevice->bindlessBufferSliceAt( RHIBufferSRVIndex{ index } ) : RHIBufferSlice{};
        ID3D11Buffer* const  pCb    = bBound ? _pDevice->resolveBuffer( source._buffer ) : nullptr;
        if ( bBound && pCb == nullptr )
            _pDevice->getDrawDiagnostics().reportDescriptorNotRegistered( "b", slot, index );
        // 상수 버퍼 페이지의 구간이면 첫 상수 · 상수 수(16 B 단위, 16 의 배수)로 건다(D3D11.1).
        const bool bSlice        = pCb != nullptr && source._size != 0;
        const UINT firstConstant = bSlice ? source._offset / D3D11RHIDevice::kShaderConstantBytes : D3D11ConstantBufferBindings::kWholeBuffer;
        const UINT constantCount = source._size / D3D11RHIDevice::kShaderConstantBytes;
        if ( point == RHIBindPoint::Compute )
        {
            setConstantBuffer( kCSStage, slot, pCb, firstConstant, constantCount );
            return;
        }
        // 변위 머티리얼의 헐 · 도메인도 패스 상수를 읽는다.
        for ( const uint32 stage : { kVSStage, kPSStage, kHSStage, kDSStage } )
        {
            setConstantBuffer( stage, slot, pCb, firstConstant, constantCount );
        }
    }

    void D3D11RHICommandContext::setConstantBuffer( const uint32 stage, const uint32 slot, ID3D11Buffer* pCb, const UINT firstConstant, const UINT constantCount )
    {
        const bool          bWhole       = firstConstant == D3D11ConstantBufferBindings::kWholeBuffer;
        const bool          bShadowed    = slot < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        ID3D11Buffer* const shadowBuffer = bShadowed ? _pState->_constantBuffers._arrBuffer[stage][slot] : nullptr;
        if ( bShadowed && shadowBuffer == pCb && pCb != nullptr && _pState->_constantBuffers._arrFirstConstant[stage][slot] == firstConstant )
            return;
        if ( bWhole == false && _context1 == nullptr )
            return; // QueryInterface 실패는 만들 때 오류로 남겼다
        // 그림자를 모르면(nullptr) 같은 버퍼일 수 있으니 빈 칸을 먼저 건다.
        const bool          bRebindSameBuffer = bShadowed == false || shadowBuffer == nullptr || shadowBuffer == pCb;
        ID3D11Buffer* const pNull             = nullptr;
        const bool          bNullFirst        = bWhole == false && _bNullBeforeOffsetRebind != SW_FALSE && bRebindSameBuffer;
        switch ( stage )
        {
            case kVSStage:
                if ( bNullFirst )
                    _pContext->VSSetConstantBuffers( slot, 1, &pNull );
                bWhole ? _pContext->VSSetConstantBuffers( slot, 1, &pCb ) : _context1->VSSetConstantBuffers1( slot, 1, &pCb, &firstConstant, &constantCount );
                break;
            case kPSStage:
                if ( bNullFirst )
                    _pContext->PSSetConstantBuffers( slot, 1, &pNull );
                bWhole ? _pContext->PSSetConstantBuffers( slot, 1, &pCb ) : _context1->PSSetConstantBuffers1( slot, 1, &pCb, &firstConstant, &constantCount );
                break;
            case kCSStage:
                if ( bNullFirst )
                    _pContext->CSSetConstantBuffers( slot, 1, &pNull );
                bWhole ? _pContext->CSSetConstantBuffers( slot, 1, &pCb ) : _context1->CSSetConstantBuffers1( slot, 1, &pCb, &firstConstant, &constantCount );
                break;
            case kHSStage:
                if ( bNullFirst )
                    _pContext->HSSetConstantBuffers( slot, 1, &pNull );
                bWhole ? _pContext->HSSetConstantBuffers( slot, 1, &pCb ) : _context1->HSSetConstantBuffers1( slot, 1, &pCb, &firstConstant, &constantCount );
                break;
            default:
                if ( bNullFirst )
                    _pContext->DSSetConstantBuffers( slot, 1, &pNull );
                bWhole ? _pContext->DSSetConstantBuffers( slot, 1, &pCb ) : _context1->DSSetConstantBuffers1( slot, 1, &pCb, &firstConstant, &constantCount );
                break;
        }
        if ( bShadowed )
        {
            _pState->_constantBuffers._arrBuffer[stage][slot]        = pCb;
            _pState->_constantBuffers._arrFirstConstant[stage][slot] = firstConstant;
        }
    }

    ID3D11ShaderResourceView* D3D11RHICommandContext::findBindlessBufferSRV( RHIBufferSRVIndex index ) const
    {
        // 레지스트리는 공유 락으로 읽고(범위 밖이면 핸들 0), SRV 는 버퍼 레코드에서 락 없이 읽는다(핸들 0 은 nullptr).
        return _pDevice->resolveBufferSRV( _pDevice->bindlessBufferAt( index ) );
    }

    void D3D11RHICommandContext::dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        if ( _pContext == nullptr )
            return;
        bindComputePipelineForDispatch();
        _pContext->Dispatch( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void D3D11RHICommandContext::setViewportNative( const RHIViewport& viewport )
    {
        D3D11_VIEWPORT d3dvp{};
        d3dvp.TopLeftX = viewport._x;
        d3dvp.TopLeftY = viewport._y;
        d3dvp.Width    = viewport._width;
        d3dvp.Height   = viewport._height;
        d3dvp.MinDepth = viewport._minDepth;
        d3dvp.MaxDepth = viewport._maxDepth;
        _pContext->RSSetViewports( 1, &d3dvp );
    }

    void D3D11RHICommandContext::setScissorRectNative( const RHIScissorRect& rect )
    {
        // 래스터라이저 상태가 가위를 늘 켜 둔다(PSO).
        const D3D11_RECT scissor{ static_cast<LONG>( rect._x ), static_cast<LONG>( rect._y ), static_cast<LONG>( rect._x + rect._width ),
                                  static_cast<LONG>( rect._y + rect._height ) };
        _pContext->RSSetScissorRects( 1, &scissor );
    }

    void D3D11RHICommandContext::flushRootConstants( RHIBindPoint point )
    {
        RHIRootConstants& constants = _bindings.getRootConstants( point );
        if ( constants.isDirty() == false || _pDevice->ensureRootConstantCb( *_pState, point ) == false )
            return;

        // DX11 에는 루트 상수가 없다. 계약 슬롯 b2 의 작은 상수버퍼로 흉내 낸다. WRITE_DISCARD 라 드로우마다
        // 버퍼가 새로 이름 지어져(rename) 앞 드로우가 읽던 내용과 섞이지 않는다. 버퍼는 기록 스트림 · 바인드 포인트마다
        // 따로다. 전역이면 병렬 기록이 서로의 값을 덮고, 바인드 포인트가 나눠 쓰면 디스패치가 채운 값이 다음 드로우에 보인다.
        ID3D11Buffer* const      pCb = _pState->_arrRootConstantCb[static_cast<uint32>( point )].Get();
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if ( isHrSuccess( _pContext->Map( pCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ), "ID3D11DeviceContext::Map (root constants)" ) == false )
            return;
        Memory::copy( mapped.pData, constants._arrDword, sizeof( constants._arrDword ) );
        _pContext->Unmap( pCb, 0 );
        constants.markFlushed();

        // 루트 상수는 계약 슬롯(b SW_SLOT_ROOT_CB_EMUL, SW_ROOT_CONSTANTS_BEGIN)에 건다.
        // 동적 버퍼의 WRITE_DISCARD 이름 바꾸기는 건 바인딩에 그대로 보이므로 같은 버퍼면 그림자가 다시 걸기를 건너뛴다.
        if ( point == RHIBindPoint::Compute )
        {
            setConstantBuffer( kCSStage, shaderslot::kRootConstantEmulationSlot, pCb, D3D11ConstantBufferBindings::kWholeBuffer, 0 );
            return;
        }
        // 머티리얼 원소 수(SW_MATERIAL 의 클램프)를 헐 · 도메인도 읽는다.
        for ( const uint32 stage : { kVSStage, kPSStage, kHSStage, kDSStage } )
        {
            setConstantBuffer( stage, shaderslot::kRootConstantEmulationSlot, pCb, D3D11ConstantBufferBindings::kWholeBuffer, 0 );
        }
    }

    void D3D11RHICommandContext::drawIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount,
                                                     RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        // D3D11 에는 멀티 드로우도 countBuffer 도 없다. 커맨드마다 한 번씩 부른다.
        // (countBuffer 는 GPU 가 정한 개수라 CPU 가 읽을 수 없으므로 drawCount 를 상한으로 그대로 쓴다.)
        (void)countBuffer;
        (void)countBufferOffset;
        const bool bNothingToDrawIndirect = _pDevice == nullptr || _pContext == nullptr || argumentBuffer == 0 || drawCount == 0;
        if ( bNothingToDrawIndirect )
            return;

        ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
        if ( pBuf == nullptr )
            return;

        drawWith<false>( [&]( ID3D11DeviceContext& context )
        {
            for ( uint32 commandIndex = 0; commandIndex < drawCount; ++commandIndex )
            {
                context.DrawInstancedIndirect( pBuf, argumentBufferOffset + commandIndex * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
            }
        } );
    }

    bool D3D11RHICommandContext::bindIndexBufferForDraw()
    {
        return _bindings._geometry.bindIndexBufferForDraw( _pDevice->getDrawDiagnostics(), [this]( const RHIBufferBinding& binding )
        {
            ID3D11Buffer* const pIb = _pDevice->resolveBuffer( binding._buffer );
            if ( pIb == nullptr )
                return false;
            _pContext->IASetIndexBuffer( pIb, ( binding._stride == 2 ) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, binding._offset );
            return true;
        } );
    }

    void D3D11RHICommandContext::drawIndexedInstancedNative( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance )
    {
        const bool bNothingToDraw = _pContext == nullptr || indexCount == 0 || instanceCount == 0;
        if ( bNothingToDraw )
            return;
        drawWith<true>( [&]( ID3D11DeviceContext& context )
        { context.DrawIndexedInstanced( indexCount, instanceCount, startIndex, baseVertex, startInstance ); } );
    }

    void D3D11RHICommandContext::drawIndexedIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer,
                                                            uint32 countBufferOffset )
    {
        // D3D11 에는 멀티 드로우도 개수 버퍼도 없다 — 커맨드마다 한 번씩(`drawIndirect` 와 같다).
        (void)countBuffer;
        (void)countBufferOffset;
        if ( _pContext == nullptr || argumentBuffer == 0 || drawCount == 0 )
            return;

        ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
        if ( pBuf == nullptr )
            return;

        drawWith<true>( [&]( ID3D11DeviceContext& context )
        {
            for ( uint32 commandIndex = 0; commandIndex < drawCount; ++commandIndex )
            {
                context.DrawIndexedInstancedIndirect( pBuf, argumentBufferOffset + commandIndex * static_cast<uint32>( sizeof( RHIDrawIndexedIndirectCommand ) ) );
            }
        } );
    }

    void D3D11RHICommandContext::dispatchIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pContext != nullptr && argumentBuffer != 0 )
        {
            ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
            if ( pBuf == nullptr )
                return;
            bindComputePipelineForDispatch();
            _pContext->DispatchIndirect( pBuf, argumentBufferOffset );
        }
    }

    void D3D11RHICommandContext::prepareTextureForShaderReadNative( RHITextureHandle texture )
    {
        // DX11 은 리소스 상태를 명시적으로 전환하지 않는다. 다만 같은 텍스처가 렌더타깃으로 걸린 채
        // SRV 로도 걸려 있으면 런타임이 조용히 한쪽을 풀어버리므로, 읽기로 넘기기 전에 픽셀 셰이더
        // SRV 슬롯을 비운다. 이 호출은 항상 바인딩 이전에
        // 오므로(FramePassExecutor::registerPassTexture) 유효한 바인딩을 지우지 않는다.
        (void)texture;
        if ( _pContext == nullptr )
            return;

        // 컴퓨트가 RW 로 쓴 텍스처도 같다 — 슬롯에 남겨 두면 픽셀 셰이더가 읽을 때 런타임이 그 SRV 를 비운다. 읽기로 넘기는 지금 RW 슬롯을 비운다. 걸린 채인 칸은 다음 디스패치가 다시 건다.
        RHIShaderSlots& computeSlots = _bindings.getSlots( RHIBindPoint::Compute );
        for ( uint32 slot = shaderslot::kComputeTextureUAV0; slot < shaderslot::kComputeTextureUAV0 + shaderslot::kComputeTextureUAVSlotCount; ++slot )
        {
            computeSlots.markDirty( RHIShaderSlotKind::UnorderedAccess, slot );
        }
        ID3D11UnorderedAccessView* arrNullUAV[shaderslot::kComputeTextureUAVSlotCount] = {};
        _pContext->CSSetUnorderedAccessViews( shaderslot::kComputeTextureUAV0, shaderslot::kComputeTextureUAVSlotCount, arrNullUAV, nullptr );

        if ( _pState->_pixelSRVMask == 0 )
            return; // 픽셀 단계에 건 것이 없다(전이가 이어질 때 되풀이하지 않는다)
        ID3D11ShaderResourceView* arrNullSRV[RHIShaderSlots::kShaderResourceCount] = {};
        _pContext->PSSetShaderResources( 0, RHIShaderSlots::kShaderResourceCount, arrNullSRV );
        // 머티리얼 슬롯은 정점 셰이더에도 걸린다(setGraphicsShaderResource) — 같은 범위만 뗀다(정점 셰이더의 구조버퍼 t4..t11 은 그대로 둔다).
        _pContext->VSSetShaderResources( shaderslot::kMaterialTexture0, shaderslot::kMaterialTextureCount, arrNullSRV );
        _pContext->HSSetShaderResources( shaderslot::kMaterialTexture0, shaderslot::kMaterialTextureCount, arrNullSRV );
        _pContext->DSSetShaderResources( shaderslot::kMaterialTexture0, shaderslot::kMaterialTextureCount, arrNullSRV );
        Memory::set( _pState->_arrPixelSRVTexture, 0, sizeof( _pState->_arrPixelSRVTexture ) );
        _pState->_pixelSRVMask = 0;
        _bindings.getSlots( RHIBindPoint::Graphics ).markAllDirty();
    }

    void D3D11RHICommandContext::prepareTextureForRenderTargetNative( RHITextureHandle texture )
    {
        // D3D11 은 상태를 전이하지 않는다. 대신 같은 리소스가 입력과 출력에 함께 걸리면 OMSetRenderTargets 가 SRV 쪽을 NULL 로
        // 강제한다(해저드 경고). 첨부로 쓰기 전에 이 텍스처가 걸린 PS SRV 슬롯만 뗀다. 백버퍼는 SRV 로 걸리지 않는다.
        if ( _pContext == nullptr || texture == kRHIBackBufferTexture )
            return;
        for ( uint32 slot = 0; slot < RHIShaderSlots::kShaderResourceCount; ++slot )
        {
            if ( _pState->_arrPixelSRVTexture[slot] != texture )
                continue;
            setGraphicsShaderResource( slot, nullptr );
            _pState->_arrPixelSRVTexture[slot] = 0;
            _bindings.getSlots( RHIBindPoint::Graphics ).markDirty( RHIShaderSlotKind::ShaderResource, slot );
        }
    }

    ID3DUserDefinedAnnotation* D3D11RHICommandContext::getAnnotation()
    {
        if ( _bAnnotationQueried == false )
        {
            _bAnnotationQueried = SW_TRUE;
            // 주석 인터페이스는 선택이다(디버그 도구가 없으면 없다) — 실패는 "이벤트 마커를 남기지 않는다" 이고 오류가 아니다.
            if ( _pContext != nullptr && FAILED( _pContext->QueryInterface( IID_PPV_ARGS( _annotation.GetAddressOf() ) ) ) )
                _annotation.Reset();
        }
        return _annotation.Get();
    }

    void D3D11RHICommandContext::writeBreadcrumb( uint32 markerID )
    {
        // DX11 은 소실 뒤에도 읽을 수 있는 디바이스측 쓰기 수단이 없다(`RHICapabilities::_bGPUBreadcrumbs` 거짓). 소실 보고는 CPU 가 마지막으로 기록한 패스를 싣는다.
        (void)markerID;
    }

    void D3D11RHICommandContext::beginEventMarker( const utf8* pName )
    {
        if ( pName == nullptr )
            return;
        ID3DUserDefinedAnnotation* pAnnotation = getAnnotation();
        if ( pAnnotation != nullptr )
        {
            // 주의: 고정 배열에 `MultiByteToWideChar` 로 옮기지 말 것. 그 API 는 이름이 버퍼보다 길면
            // 0 을 반환하고 널 종단을 보장하지 않으므로, 그대로 `BeginEvent` 에 넘기면 배열 밖까지
            // 읽는다. 길이에 상한이 없는 `StringUtil::utf8ToUTF16` 은 잘라 담을 일도, 다중바이트 시퀀스가
            // 중간에서 끊길 일도 없다. 마커는 그래픽스 디버거가 붙었을 때만 동작하므로(그때만 annotation 이
            // 널이 아니다) 여기서 한 번 할당하는 비용은 캡처 비용에 묻힌다.
            const wstring wideName = StringUtil::utf8ToUTF16( pName );
            pAnnotation->BeginEvent( wideName.c_str() );
        }
    }

    void D3D11RHICommandContext::endEventMarker()
    {
        ID3DUserDefinedAnnotation* pAnnotation = getAnnotation();
        if ( pAnnotation != nullptr )
            pAnnotation->EndEvent();
    }

} // namespace sw
#endif
