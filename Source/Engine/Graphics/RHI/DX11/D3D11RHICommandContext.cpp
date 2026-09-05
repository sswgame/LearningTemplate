#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHICommandContext.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{

    D3D11RHICommandContext::D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext )
        : _pDevice{ pDevice }
        , _pContext{ pContext }
        , _pState{ pDevice != nullptr ? &pDevice->_recordingState : nullptr }
    {
    }

    D3D11RHICommandContext::D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext,
                                                    D3D11RecordingState* pState )
        : _pDevice{ pDevice }
        , _pContext{ pContext }
        , _pState{ pState }
    {
    }

    void D3D11RHICommandContext::beginOffscreenPass( RHITextureHandle colorTarget, const float4& clearColor )
    {
        if ( colorTarget == 0 )
        {
            _pDevice->beginFrame( clearColor );
            return;
        }

        if ( _pContext == nullptr )
            return;

        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( colorTarget );
        if ( pRecord == nullptr || pRecord->_rtv == nullptr )
            return;

        _pContext->ClearRenderTargetView( pRecord->_rtv.Get(), &clearColor._x );
        _pContext->OMSetRenderTargets( 1, pRecord->_rtv.GetAddressOf(), nullptr );

        D3D11_VIEWPORT vp{};
        vp.Width    = static_cast<float32>( pRecord->_width );
        vp.Height   = static_cast<float32>( pRecord->_height );
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        _pContext->RSSetViewports( 1, &vp );
    }

    void D3D11RHICommandContext::endOffscreenPass( RHITextureHandle colorTarget )
    {
        if ( colorTarget == 0 || _pContext == nullptr )
            return;

        ID3D11RenderTargetView* pNullRtv{ nullptr };
        _pContext->OMSetRenderTargets( 1, &pNullRtv, nullptr );

        // Unbind possible SRV uses of the offscreen color target before ImGui samples it.
        ID3D11ShaderResourceView* nullSrvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        _pContext->PSSetShaderResources( 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs );
    }

    void D3D11RHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
    {
        if ( _pContext == nullptr || src == 0 )
            return;

        const D3D11RHIDevice::TextureRecord* pSrcRecord = _pDevice->resolveTexture( src );
        if ( pSrcRecord == nullptr || pSrcRecord->_texture == nullptr || pSrcRecord->_bDepth != 0 )
            return;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> dstTex;
        if ( dst == 0 )
        {
            if ( _pDevice->_swapChain == nullptr )
                return;
            if ( FAILED( _pDevice->_swapChain->GetBuffer( 0, IID_PPV_ARGS( dstTex.GetAddressOf() ) ) ) )
                return;
        }
        else
        {
            const D3D11RHIDevice::TextureRecord* pDstRecord = _pDevice->resolveTexture( dst );
            if ( pDstRecord == nullptr || pDstRecord->_texture == nullptr || pDstRecord->_bDepth != 0 )
                return;
            dstTex = pDstRecord->_texture;
        }

        _pContext->CopyResource( dstTex.Get(), pSrcRecord->_texture.Get() );
    }

    void D3D11RHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        const D3D11RHIDevice::D3D11PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || _pContext == nullptr )
            return;

        _pState->_activeGraphicsPso = pso;
        if ( pRecord->_vs )
            _pContext->VSSetShader( pRecord->_vs.Get(), nullptr, 0 );
        if ( pRecord->_ps )
            _pContext->PSSetShader( pRecord->_ps.Get(), nullptr, 0 );
        if ( pRecord->_cs )
            _pContext->CSSetShader( pRecord->_cs.Get(), nullptr, 0 );
        if ( pRecord->_inputLayout )
            _pContext->IASetInputLayout( pRecord->_inputLayout.Get() );
        if ( pRecord->_rasterizerState )
            _pContext->RSSetState( pRecord->_rasterizerState.Get() );
        if ( pRecord->_blendState )
        {
            constexpr float32 arrBlendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            _pContext->OMSetBlendState( pRecord->_blendState.Get(), arrBlendFactor, MathUtil::MaxUInt32 );
        }
        if ( pRecord->_depthStencilState )
            _pContext->OMSetDepthStencilState( pRecord->_depthStencilState.Get(), 0 );
    }

    void D3D11RHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
    {
        setPipelineState( pso );
    }

    void D3D11RHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
    {
        if ( _pContext == nullptr )
            return;

        _lastBoundMaterialDescriptor = kInvalidDescriptorIndex;

        ID3D11RenderTargetView* arrRtv[kMaxColorAttachments]{};
        uint32                  rtCount{ 0 };
        if ( beginInfo._bBindColor != 0 )
        {
            const uint32 wantCount = beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u;
            for ( uint32 attachmentIndex = 0; attachmentIndex < wantCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
            {
                const RHITextureHandle  colorHandle = beginInfo._arrColorTarget[attachmentIndex];
                ID3D11RenderTargetView* pRtv{ nullptr };
                if ( colorHandle == 0 )
                    pRtv = ( attachmentIndex == 0 ) ? _pDevice->_renderTargetView.Get() : nullptr;
                else
                {
                    D3D11RHIDevice::TextureRecord* pTex = _pDevice->resolveTexture( colorHandle );
                    if ( pTex != nullptr && pTex->_rtv )
                        pRtv = pTex->_rtv.Get();
                }
                if ( pRtv == nullptr && attachmentIndex > 0 )
                    break;
                arrRtv[rtCount++] = pRtv;

                const RHIRenderPassLoadOp loadOp = beginInfo._arrLoadOp[attachmentIndex];
                const float32*            pClear = &beginInfo._arrClearColor[attachmentIndex]._x;
                if ( loadOp == RHIRenderPassLoadOp::Clear && pRtv != nullptr )
                    _pContext->ClearRenderTargetView( pRtv, pClear );
            }
        }

        ID3D11DepthStencilView* pDsv{ nullptr };
        if ( beginInfo._depthTarget != 0 )
        {
            D3D11RHIDevice::TextureRecord* pDepthTex = _pDevice->resolveTexture( beginInfo._depthTarget );
            if ( pDepthTex != nullptr && pDepthTex->_dsv )
                pDsv = pDepthTex->_dsv.Get();
        }

        if ( beginInfo._depthLoadOp == RHIRenderPassLoadOp::Clear && pDsv != nullptr )
            _pContext->ClearDepthStencilView( pDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, beginInfo._clearDepth, 0 );

        if ( rtCount > 0 )
            _pContext->OMSetRenderTargets( rtCount, arrRtv, pDsv );
        else
            _pContext->OMSetRenderTargets( 0, nullptr, pDsv );

        // Prefer active PSO depth/blend (depth-write / alpha); fall back to global depth states.
        const D3D11RHIDevice::D3D11PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPso );
        if ( pRecord != nullptr )
        {
            if ( pRecord->_depthStencilState )
                _pContext->OMSetDepthStencilState( pRecord->_depthStencilState.Get(), 0 );
            else if ( pDsv != nullptr && _pDevice->_depthEnabledState )
                _pContext->OMSetDepthStencilState( _pDevice->_depthEnabledState.Get(), 0 );
            else if ( _pDevice->_depthDisabledState )
                _pContext->OMSetDepthStencilState( _pDevice->_depthDisabledState.Get(), 0 );
            if ( pRecord->_blendState )
            {
                constexpr float32 arrBlendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                _pContext->OMSetBlendState( pRecord->_blendState.Get(), arrBlendFactor, MathUtil::MaxUInt32 );
            }
        }
        else if ( pDsv != nullptr && _pDevice->_depthEnabledState )
            _pContext->OMSetDepthStencilState( _pDevice->_depthEnabledState.Get(), 0 );
        else if ( _pDevice->_depthDisabledState )
            _pContext->OMSetDepthStencilState( _pDevice->_depthDisabledState.Get(), 0 );

        D3D11_VIEWPORT vp{};
        vp.Width    = static_cast<float32>( beginInfo._width > 0 ? beginInfo._width : _pDevice->_width );
        vp.Height   = static_cast<float32>( beginInfo._height > 0 ? beginInfo._height : _pDevice->_height );
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        _pContext->RSSetViewports( 1, &vp );
    }

    void D3D11RHICommandContext::endRenderPass()
    {
    }

    void D3D11RHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
    {
        if ( _pContext == nullptr || buffer == 0 )
            return;
        ID3D11Buffer* pIb = _pDevice->resolveBuffer( buffer );
        if ( pIb == nullptr )
            return;
        const DXGI_FORMAT format = ( indexStride == 2 ) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        _pContext->IASetIndexBuffer( pIb, format, offset );
    }

    void D3D11RHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
    {
        // D3D11 has no explicit buffer state transitions; GPU sync is via Flush / FinishCommandList.
        (void)buffer;
        (void)newState;
    }

    void D3D11RHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
    {
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav = _pDevice->bindlessUavAt( index );
        if ( _pContext != nullptr && uav != nullptr )
        {
            ID3D11UnorderedAccessView* pUav = uav.Get();
            _pContext->CSSetUnorderedAccessViews( slot, 1, &pUav, nullptr );
        }
    }

    void D3D11RHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        const RHITextureHandle texture = _pDevice->bindlessTextureAt( index );
        if ( _pContext == nullptr || texture == 0 )
            return;
        ID3D11ShaderResourceView*      pSrv{ nullptr };
        D3D11RHIDevice::TextureRecord* pTex = _pDevice->resolveTexture( texture );
        if ( pTex != nullptr )
            pSrv = pTex->_srv.Get();
        _pContext->PSSetShaderResources( slot, 1, &pSrv );
        if ( _pDevice->_linearSampler )
            _pContext->PSSetSamplers( 0, 1, _pDevice->_linearSampler.GetAddressOf() );
    }

    void D3D11RHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        (void)slot;
        _pState->_boundMeshVb     = buffer;
        _pState->_boundMeshStride = stride > 0 ? stride : static_cast<uint32>( sizeof( RHIVertex ) );
        _pState->_boundMeshOffset = offset;
    }

    void D3D11RHICommandContext::bindPassAndMaterialCb( RHIDescriptorIndex passCbDescriptorIndex,
                                                        RHIDescriptorIndex materialCbDescriptorIndex )
    {
        defaultBindPassAndMaterialCb( passCbDescriptorIndex, materialCbDescriptorIndex,
                                      _pDevice->bindlessBufferCount(), 0 /*b0=PassCB*/, 1 /*b1=MaterialCB*/,
                                      [this]( RHIDescriptorIndex index, uint32 slot )
        {
            ID3D11Buffer* pCb = _pDevice->resolveBuffer( _pDevice->bindlessBufferAt( index ) );
            if ( pCb == nullptr )
                return;
            _pContext->PSSetConstantBuffers( slot, 1, &pCb );
            _pContext->VSSetConstantBuffers( slot, 1, &pCb );
        } );
    }

    void D3D11RHICommandContext::draw( uint32 vertexCount, uint32 startVertex,
                                       RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex )
    {
        if ( _pContext == nullptr || vertexCount == 0 )
            return;

        ID3D11VertexShader*                             pVs  = nullptr;
        ID3D11PixelShader*                              pPs  = nullptr;
        ID3D11InputLayout*                              pIl  = nullptr;
        const D3D11RHIDevice::D3D11PipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPso );
        if ( pPso != nullptr )
        {
            if ( pPso->_vs )
                pVs = pPso->_vs.Get();
            if ( pPso->_ps )
                pPs = pPso->_ps.Get();
            if ( pPso->_inputLayout )
                pIl = pPso->_inputLayout.Get();
        }
        if ( pVs == nullptr || pPs == nullptr )
            return;

        bindPassAndMaterialCb( passCbDescriptorIndex, materialCbDescriptorIndex );

        ID3D11Buffer* pVb    = _pState->_boundMeshVb != 0 ? _pDevice->resolveBuffer( _pState->_boundMeshVb ) : _pDevice->_vertexBuffer.Get();
        UINT          stride = _pState->_boundMeshVb != 0 ? _pState->_boundMeshStride : static_cast<UINT>( sizeof( RHIVertex ) );
        UINT          offset = _pState->_boundMeshVb != 0 ? _pState->_boundMeshOffset : 0;
        if ( pVb != nullptr )
            _pContext->IASetVertexBuffers( 0, 1, &pVb, &stride, &offset );

        _pContext->IASetInputLayout( pIl );
        _pContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        _pContext->VSSetShader( pVs, nullptr, 0 );
        _pContext->PSSetShader( pPs, nullptr, 0 );
        _pContext->Draw( vertexCount, startVertex );
    }

    void D3D11RHICommandContext::drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        if ( _pContext == nullptr || vertexCount == 0 || instanceCount == 0 )
            return;

        ID3D11VertexShader*                             pVs  = nullptr;
        ID3D11PixelShader*                              pPs  = nullptr;
        ID3D11InputLayout*                              pIl  = nullptr;
        const D3D11RHIDevice::D3D11PipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPso );
        if ( pPso != nullptr )
        {
            if ( pPso->_vs )
                pVs = pPso->_vs.Get();
            if ( pPso->_ps )
                pPs = pPso->_ps.Get();
            if ( pPso->_inputLayout )
                pIl = pPso->_inputLayout.Get();
        }
        if ( pVs == nullptr || pPs == nullptr )
            return;

        ID3D11Buffer* pVb    = _pState->_boundMeshVb != 0 ? _pDevice->resolveBuffer( _pState->_boundMeshVb ) : _pDevice->_vertexBuffer.Get();
        UINT          stride = _pState->_boundMeshVb != 0 ? _pState->_boundMeshStride : static_cast<UINT>( sizeof( RHIVertex ) );
        UINT          offset = _pState->_boundMeshVb != 0 ? _pState->_boundMeshOffset : 0;
        if ( pVb != nullptr )
            _pContext->IASetVertexBuffers( 0, 1, &pVb, &stride, &offset );

        _pContext->IASetInputLayout( pIl );
        _pContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        _pContext->VSSetShader( pVs, nullptr, 0 );
        _pContext->PSSetShader( pPs, nullptr, 0 );
        _pContext->DrawInstanced( vertexCount, instanceCount, startVertex, startInstance );
    }

    void D3D11RHICommandContext::bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot )
    {
        if ( _pContext == nullptr || cb == kInvalidDescriptorIndex ||
             cb >= static_cast<RHIDescriptorIndex>( _pDevice->bindlessBufferCount() ) )
            return;
        ID3D11Buffer* pCb = _pDevice->resolveBuffer( _pDevice->bindlessBufferAt( cb ) );
        if ( pCb == nullptr )
            return;
        _pContext->VSSetConstantBuffers( slot, 1, &pCb );
        _pContext->PSSetConstantBuffers( slot, 1, &pCb );
        _pContext->CSSetConstantBuffers( slot, 1, &pCb );
    }

    void D3D11RHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 VS/PS 가 읽는 구조버퍼(SwInstanceData 등). createStructuredBuffer 에서 만든 SRV 를
        // 리플렉션 t 슬롯에 바인딩한다.
        if ( _pContext == nullptr || index == kInvalidDescriptorIndex ||
             index >= static_cast<RHIDescriptorIndex>( _pDevice->bindlessBufferCount() ) )
            return;
        const RHIBufferHandle buffer = _pDevice->bindlessBufferAt( index );
        if ( buffer == 0 )
            return;
        const auto it = _pDevice->_mapBufferSrv.find( buffer );
        if ( it == _pDevice->_mapBufferSrv.end() || it->second == nullptr )
            return;
        ID3D11ShaderResourceView* pSrv = it->second.Get();
        _pContext->VSSetShaderResources( slot, 1, &pSrv );
        _pContext->PSSetShaderResources( slot, 1, &pSrv );
    }

    void D3D11RHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pContext == nullptr || index == kInvalidDescriptorIndex ||
             index >= static_cast<RHIDescriptorIndex>( _pDevice->bindlessBufferCount() ) )
            return;
        ID3D11Buffer* pCb = _pDevice->resolveBuffer( _pDevice->bindlessBufferAt( index ) );
        if ( pCb == nullptr )
            return;
        _pContext->CSSetConstantBuffers( slot, 1, &pCb );
    }

    void D3D11RHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        // gpucull 등 컴퓨트 셰이더가 읽는 구조버퍼(g_Instances 등)를 CS 스테이지에 바인딩한다.
        if ( _pContext == nullptr || index == kInvalidDescriptorIndex ||
             index >= static_cast<RHIDescriptorIndex>( _pDevice->bindlessBufferCount() ) )
            return;
        const RHIBufferHandle buffer = _pDevice->bindlessBufferAt( index );
        if ( buffer == 0 )
            return;
        const auto it = _pDevice->_mapBufferSrv.find( buffer );
        if ( it == _pDevice->_mapBufferSrv.end() || it->second == nullptr )
            return;
        ID3D11ShaderResourceView* pSrv = it->second.Get();
        _pContext->CSSetShaderResources( slot, 1, &pSrv );
    }

    void D3D11RHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        if ( _pContext != nullptr )
            _pContext->Dispatch( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void D3D11RHICommandContext::setViewport( const RHIViewport& viewport )
    {
        if ( _pContext == nullptr )
            return;

        D3D11_VIEWPORT d3dvp{};
        d3dvp.TopLeftX = viewport._x;
        d3dvp.TopLeftY = viewport._y;
        d3dvp.Width    = viewport._width;
        d3dvp.Height   = viewport._height;
        d3dvp.MinDepth = viewport._minDepth;
        d3dvp.MaxDepth = viewport._maxDepth;
        _pContext->RSSetViewports( 1, &d3dvp );
    }

    void D3D11RHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        if ( _pContext == nullptr || num32BitValues == 0 || pData == nullptr )
            return;
        if ( destOffsetIn32BitValues >= D3D11RHIDevice::kMaxComputeRootConstantDwords )
            return;

        const uint32 maxCount = D3D11RHIDevice::kMaxComputeRootConstantDwords - destOffsetIn32BitValues;
        const uint32 count    = num32BitValues < maxCount ? num32BitValues : maxCount;
        if ( _pDevice->ensureComputeRootConstantCB() == false )
            return;

        Memory::copy( _pDevice->_arrComputeRootConstantShadow + destOffsetIn32BitValues, pData, static_cast<size_t>( count ) * sizeof( uint32 ) );

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if ( FAILED( _pContext->Map( _pDevice->_computeRootConstantCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
            return;
        Memory::copy( mapped.pData, _pDevice->_arrComputeRootConstantShadow, sizeof( _pDevice->_arrComputeRootConstantShadow ) );
        _pContext->Unmap( _pDevice->_computeRootConstantCB.Get(), 0 );

        ID3D11Buffer* pCb = _pDevice->_computeRootConstantCB.Get();
        _pContext->CSSetConstantBuffers( rootParameterIndex, 1, &pCb );
    }

    void D3D11RHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
                                               RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex )
    {
        if ( _pDevice == nullptr || _pContext == nullptr || argumentBuffer == 0 )
            return;

        bindPassAndMaterialCb( passCbDescriptorIndex, materialCbDescriptorIndex );
        _lastBoundMaterialDescriptor = materialCbDescriptorIndex;

        ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
        if ( pBuf == nullptr )
            return;
        _pContext->DrawInstancedIndirect( pBuf, argumentBufferOffset );
    }

    void D3D11RHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pContext != nullptr && argumentBuffer != 0 )
        {
            ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
            if ( pBuf != nullptr )
                _pContext->DrawIndexedInstancedIndirect( pBuf, argumentBufferOffset );
        }
    }

    void D3D11RHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pContext != nullptr && argumentBuffer != 0 )
        {
            ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
            if ( pBuf != nullptr )
                _pContext->DispatchIndirect( pBuf, argumentBufferOffset );
        }
    }

    void D3D11RHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
    {
        // DX11은 리소스 상태를 명시적으로 전환하지 않음. 심볼 링크용 스텁.
        (void)texture;
    }

    ID3DUserDefinedAnnotation* D3D11RHICommandContext::getAnnotation()
    {
        if ( _bAnnotationQueried == false )
        {
            _bAnnotationQueried = true;
            if ( _pContext != nullptr )
                _pContext->QueryInterface( IID_PPV_ARGS( _annotation.GetAddressOf() ) );
        }
        return _annotation.Get();
    }

    void D3D11RHICommandContext::beginEventMarker( const utf8* pName )
    {
        if ( pName == nullptr )
            return;
        ID3DUserDefinedAnnotation* pAnnotation = getAnnotation();
        if ( pAnnotation != nullptr )
        {
            utf16 wide[constant::kMaxBuffer256]{};
            MultiByteToWideChar( CP_UTF8, 0, pName, -1, wide, constant::kMaxBuffer256 );
            pAnnotation->BeginEvent( wide );
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
