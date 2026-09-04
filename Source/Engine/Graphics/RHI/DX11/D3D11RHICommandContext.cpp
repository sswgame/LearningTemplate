#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHICommandContext.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{

    void D3D11RHICommandContext::beginOffscreenPass( RHITextureHandle colorTarget, const float4& clearColor )
    {
        if ( colorTarget == 0 )
        {
            _pDevice->beginFrame( clearColor );
            return;
        }

        if ( _pDevice->_deviceContext == nullptr )
            return;

        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( colorTarget );
        if ( pRecord == nullptr || pRecord->_rtv == nullptr )
            return;

        _pDevice->_deviceContext->ClearRenderTargetView( pRecord->_rtv.Get(), &clearColor._x );
        _pDevice->_deviceContext->OMSetRenderTargets( 1, pRecord->_rtv.GetAddressOf(), nullptr );

        D3D11_VIEWPORT vp{};
        vp.Width    = static_cast<float32>( pRecord->_width );
        vp.Height   = static_cast<float32>( pRecord->_height );
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        _pDevice->_deviceContext->RSSetViewports( 1, &vp );
    }

    void D3D11RHICommandContext::endOffscreenPass( RHITextureHandle colorTarget )
    {
        if ( colorTarget == 0 || _pDevice->_deviceContext == nullptr )
            return;

        ID3D11RenderTargetView* pNullRtv{ nullptr };
        _pDevice->_deviceContext->OMSetRenderTargets( 1, &pNullRtv, nullptr );

        // Unbind possible SRV uses of the offscreen color target before ImGui samples it.
        ID3D11ShaderResourceView* nullSrvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        _pDevice->_deviceContext->PSSetShaderResources( 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs );
    }

    void D3D11RHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
    {
        if ( _pDevice->_deviceContext == nullptr || src == 0 )
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

        _pDevice->_deviceContext->CopyResource( dstTex.Get(), pSrcRecord->_texture.Get() );
    }

    void D3D11RHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        const D3D11RHIDevice::D3D11PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || _pDevice->_deviceContext == nullptr )
            return;

        _pDevice->_activeGraphicsPso = pso;
        if ( pRecord->_vs )
            _pDevice->_deviceContext->VSSetShader( pRecord->_vs.Get(), nullptr, 0 );
        if ( pRecord->_ps )
            _pDevice->_deviceContext->PSSetShader( pRecord->_ps.Get(), nullptr, 0 );
        if ( pRecord->_cs )
            _pDevice->_deviceContext->CSSetShader( pRecord->_cs.Get(), nullptr, 0 );
        if ( pRecord->_inputLayout )
            _pDevice->_deviceContext->IASetInputLayout( pRecord->_inputLayout.Get() );
        if ( pRecord->_rasterizerState )
            _pDevice->_deviceContext->RSSetState( pRecord->_rasterizerState.Get() );
        if ( pRecord->_blendState )
        {
            constexpr float32 arrBlendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            _pDevice->_deviceContext->OMSetBlendState( pRecord->_blendState.Get(), arrBlendFactor, MathUtil::MaxUInt32 );
        }
        if ( pRecord->_depthStencilState )
            _pDevice->_deviceContext->OMSetDepthStencilState( pRecord->_depthStencilState.Get(), 0 );
    }

    void D3D11RHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
    {
        setPipelineState( pso );
    }

    void D3D11RHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
    {
        if ( _pDevice->_deviceContext == nullptr )
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
                    _pDevice->_deviceContext->ClearRenderTargetView( pRtv, pClear );
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
            _pDevice->_deviceContext->ClearDepthStencilView( pDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, beginInfo._clearDepth, 0 );

        if ( rtCount > 0 )
            _pDevice->_deviceContext->OMSetRenderTargets( rtCount, arrRtv, pDsv );
        else
            _pDevice->_deviceContext->OMSetRenderTargets( 0, nullptr, pDsv );

        // Prefer active PSO depth/blend (depth-write / alpha); fall back to global depth states.
        const D3D11RHIDevice::D3D11PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( _pDevice->_activeGraphicsPso );
        if ( pRecord != nullptr )
        {
            if ( pRecord->_depthStencilState )
                _pDevice->_deviceContext->OMSetDepthStencilState( pRecord->_depthStencilState.Get(), 0 );
            else if ( pDsv != nullptr && _pDevice->_depthEnabledState )
                _pDevice->_deviceContext->OMSetDepthStencilState( _pDevice->_depthEnabledState.Get(), 0 );
            else if ( _pDevice->_depthDisabledState )
                _pDevice->_deviceContext->OMSetDepthStencilState( _pDevice->_depthDisabledState.Get(), 0 );
            if ( pRecord->_blendState )
            {
                constexpr float32 arrBlendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                _pDevice->_deviceContext->OMSetBlendState( pRecord->_blendState.Get(), arrBlendFactor, MathUtil::MaxUInt32 );
            }
        }
        else if ( pDsv != nullptr && _pDevice->_depthEnabledState )
            _pDevice->_deviceContext->OMSetDepthStencilState( _pDevice->_depthEnabledState.Get(), 0 );
        else if ( _pDevice->_depthDisabledState )
            _pDevice->_deviceContext->OMSetDepthStencilState( _pDevice->_depthDisabledState.Get(), 0 );

        D3D11_VIEWPORT vp{};
        vp.Width    = static_cast<float32>( beginInfo._width > 0 ? beginInfo._width : _pDevice->_width );
        vp.Height   = static_cast<float32>( beginInfo._height > 0 ? beginInfo._height : _pDevice->_height );
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        _pDevice->_deviceContext->RSSetViewports( 1, &vp );
    }

    void D3D11RHICommandContext::endRenderPass()
    {
    }

    void D3D11RHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
    {
        if ( _pDevice->_deviceContext == nullptr || buffer == 0 )
            return;
        ID3D11Buffer* pIb = _pDevice->resolveBuffer( buffer );
        if ( pIb == nullptr )
            return;
        const DXGI_FORMAT format = ( indexStride == 2 ) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        _pDevice->_deviceContext->IASetIndexBuffer( pIb, format, offset );
    }

    void D3D11RHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
    {
        // D3D11 has no explicit buffer state transitions; GPU sync is via Flush / FinishCommandList.
        (void)buffer;
        (void)newState;
    }

    void D3D11RHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pDevice->_deviceContext != nullptr && index < _pDevice->_listRegisteredUAV.size() && _pDevice->_listRegisteredUAV[index] != nullptr )
        {
            ID3D11UnorderedAccessView* pUav = _pDevice->_listRegisteredUAV[index].Get();
            _pDevice->_deviceContext->CSSetUnorderedAccessViews( slot, 1, &pUav, nullptr );
        }
    }

    void D3D11RHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pDevice->_deviceContext == nullptr || index >= _pDevice->_listRegisteredTexture.size() )
            return;
        ID3D11ShaderResourceView*      pSrv{ nullptr };
        D3D11RHIDevice::TextureRecord* pTex = _pDevice->resolveTexture( _pDevice->_listRegisteredTexture[index] );
        if ( pTex != nullptr )
            pSrv = pTex->_srv.Get();
        _pDevice->_deviceContext->PSSetShaderResources( slot, 1, &pSrv );
        if ( _pDevice->_linearSampler )
            _pDevice->_deviceContext->PSSetSamplers( 0, 1, _pDevice->_linearSampler.GetAddressOf() );
    }

    void D3D11RHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        (void)slot;
        _pDevice->_boundMeshVb     = buffer;
        _pDevice->_boundMeshStride = stride > 0 ? stride : static_cast<uint32>( sizeof( RHIVertex ) );
        _pDevice->_boundMeshOffset = offset;
    }

    void D3D11RHICommandContext::bindPassAndMaterialCb( RHIDescriptorIndex passCbDescriptorIndex,
                                                        RHIDescriptorIndex materialCbDescriptorIndex )
    {
        auto bindSlot = [this]( RHIDescriptorIndex index, UINT slot )
        {
            if ( index == kInvalidDescriptorIndex ||
                 index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
                return;
            ID3D11Buffer* pCb = _pDevice->resolveBuffer( _pDevice->_listRegisteredBindless[index] );
            if ( pCb == nullptr )
                return;
            _pDevice->_deviceContext->PSSetConstantBuffers( slot, 1, &pCb );
            _pDevice->_deviceContext->VSSetConstantBuffers( slot, 1, &pCb );
        };
        bindSlot( passCbDescriptorIndex, 0 );     // b0 = PassCB
        bindSlot( materialCbDescriptorIndex, 1 ); // b1 = MaterialCB
    }

    void D3D11RHICommandContext::draw( uint32 vertexCount, uint32 startVertex,
                                       RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex )
    {
        if ( _pDevice->_deviceContext == nullptr || vertexCount == 0 )
            return;

        ID3D11VertexShader*                             pVs  = nullptr;
        ID3D11PixelShader*                              pPs  = nullptr;
        ID3D11InputLayout*                              pIl  = nullptr;
        const D3D11RHIDevice::D3D11PipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pDevice->_activeGraphicsPso );
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

        ID3D11Buffer* pVb    = _pDevice->_boundMeshVb != 0 ? _pDevice->resolveBuffer( _pDevice->_boundMeshVb ) : _pDevice->_vertexBuffer.Get();
        UINT          stride = _pDevice->_boundMeshVb != 0 ? _pDevice->_boundMeshStride : static_cast<UINT>( sizeof( RHIVertex ) );
        UINT          offset = _pDevice->_boundMeshVb != 0 ? _pDevice->_boundMeshOffset : 0;
        if ( pVb != nullptr )
            _pDevice->_deviceContext->IASetVertexBuffers( 0, 1, &pVb, &stride, &offset );

        _pDevice->_deviceContext->IASetInputLayout( pIl );
        _pDevice->_deviceContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        _pDevice->_deviceContext->VSSetShader( pVs, nullptr, 0 );
        _pDevice->_deviceContext->PSSetShader( pPs, nullptr, 0 );
        _pDevice->_deviceContext->Draw( vertexCount, startVertex );
    }

    void D3D11RHICommandContext::drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        if ( _pDevice->_deviceContext == nullptr || vertexCount == 0 || instanceCount == 0 )
            return;

        ID3D11VertexShader*                             pVs  = nullptr;
        ID3D11PixelShader*                              pPs  = nullptr;
        ID3D11InputLayout*                              pIl  = nullptr;
        const D3D11RHIDevice::D3D11PipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pDevice->_activeGraphicsPso );
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

        ID3D11Buffer* pVb    = _pDevice->_boundMeshVb != 0 ? _pDevice->resolveBuffer( _pDevice->_boundMeshVb ) : _pDevice->_vertexBuffer.Get();
        UINT          stride = _pDevice->_boundMeshVb != 0 ? _pDevice->_boundMeshStride : static_cast<UINT>( sizeof( RHIVertex ) );
        UINT          offset = _pDevice->_boundMeshVb != 0 ? _pDevice->_boundMeshOffset : 0;
        if ( pVb != nullptr )
            _pDevice->_deviceContext->IASetVertexBuffers( 0, 1, &pVb, &stride, &offset );

        _pDevice->_deviceContext->IASetInputLayout( pIl );
        _pDevice->_deviceContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        _pDevice->_deviceContext->VSSetShader( pVs, nullptr, 0 );
        _pDevice->_deviceContext->PSSetShader( pPs, nullptr, 0 );
        _pDevice->_deviceContext->DrawInstanced( vertexCount, instanceCount, startVertex, startInstance );
    }

    void D3D11RHICommandContext::bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot )
    {
        if ( _pDevice->_deviceContext == nullptr || cb == kInvalidDescriptorIndex ||
             cb >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
            return;
        ID3D11Buffer* pCb = _pDevice->resolveBuffer( _pDevice->_listRegisteredBindless[cb] );
        if ( pCb == nullptr )
            return;
        _pDevice->_deviceContext->VSSetConstantBuffers( slot, 1, &pCb );
        _pDevice->_deviceContext->PSSetConstantBuffers( slot, 1, &pCb );
        _pDevice->_deviceContext->CSSetConstantBuffers( slot, 1, &pCb );
    }

    void D3D11RHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 VS/PS 가 읽는 구조버퍼(SwInstanceData 등). createStructuredBuffer 에서 만든 SRV 를
        // 리플렉션 t 슬롯에 바인딩한다.
        if ( _pDevice->_deviceContext == nullptr || index == kInvalidDescriptorIndex ||
             index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
            return;
        const RHIBufferHandle buffer = _pDevice->_listRegisteredBindless[index];
        if ( buffer == 0 )
            return;
        const auto it = _pDevice->_mapBufferSrv.find( buffer );
        if ( it == _pDevice->_mapBufferSrv.end() || it->second == nullptr )
            return;
        ID3D11ShaderResourceView* pSrv = it->second.Get();
        _pDevice->_deviceContext->VSSetShaderResources( slot, 1, &pSrv );
        _pDevice->_deviceContext->PSSetShaderResources( slot, 1, &pSrv );
    }

    void D3D11RHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pDevice->_deviceContext == nullptr || index == kInvalidDescriptorIndex ||
             index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
            return;
        ID3D11Buffer* pCb = _pDevice->resolveBuffer( _pDevice->_listRegisteredBindless[index] );
        if ( pCb == nullptr )
            return;
        _pDevice->_deviceContext->CSSetConstantBuffers( slot, 1, &pCb );
    }

    void D3D11RHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        // gpucull 등 컴퓨트 셰이더가 읽는 구조버퍼(g_Instances 등)를 CS 스테이지에 바인딩한다.
        if ( _pDevice->_deviceContext == nullptr || index == kInvalidDescriptorIndex ||
             index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
            return;
        const RHIBufferHandle buffer = _pDevice->_listRegisteredBindless[index];
        if ( buffer == 0 )
            return;
        const auto it = _pDevice->_mapBufferSrv.find( buffer );
        if ( it == _pDevice->_mapBufferSrv.end() || it->second == nullptr )
            return;
        ID3D11ShaderResourceView* pSrv = it->second.Get();
        _pDevice->_deviceContext->CSSetShaderResources( slot, 1, &pSrv );
    }

    void D3D11RHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        if ( _pDevice->_deviceContext != nullptr )
            _pDevice->_deviceContext->Dispatch( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void D3D11RHICommandContext::setViewport( const RHIViewport& viewport )
    {
        if ( _pDevice->_deviceContext == nullptr )
            return;

        D3D11_VIEWPORT d3dvp{};
        d3dvp.TopLeftX = viewport._x;
        d3dvp.TopLeftY = viewport._y;
        d3dvp.Width    = viewport._width;
        d3dvp.Height   = viewport._height;
        d3dvp.MinDepth = viewport._minDepth;
        d3dvp.MaxDepth = viewport._maxDepth;
        _pDevice->_deviceContext->RSSetViewports( 1, &d3dvp );
    }

    void D3D11RHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        if ( _pDevice->_deviceContext == nullptr || num32BitValues == 0 || pData == nullptr )
            return;
        if ( destOffsetIn32BitValues >= D3D11RHIDevice::kMaxComputeRootConstantDwords )
            return;

        const uint32 maxCount = D3D11RHIDevice::kMaxComputeRootConstantDwords - destOffsetIn32BitValues;
        const uint32 count    = num32BitValues < maxCount ? num32BitValues : maxCount;
        if ( _pDevice->ensureComputeRootConstantCB() == false )
            return;

        Memory::copy( _pDevice->_arrComputeRootConstantShadow + destOffsetIn32BitValues, pData, static_cast<size_t>( count ) * sizeof( uint32 ) );

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if ( FAILED( _pDevice->_deviceContext->Map( _pDevice->_computeRootConstantCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
            return;
        Memory::copy( mapped.pData, _pDevice->_arrComputeRootConstantShadow, sizeof( _pDevice->_arrComputeRootConstantShadow ) );
        _pDevice->_deviceContext->Unmap( _pDevice->_computeRootConstantCB.Get(), 0 );

        ID3D11Buffer* pCb = _pDevice->_computeRootConstantCB.Get();
        _pDevice->_deviceContext->CSSetConstantBuffers( rootParameterIndex, 1, &pCb );
    }

    void D3D11RHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
                                               RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex )
    {
        if ( _pDevice == nullptr || _pDevice->_deviceContext == nullptr || argumentBuffer == 0 )
            return;

        bindPassAndMaterialCb( passCbDescriptorIndex, materialCbDescriptorIndex );
        _lastBoundMaterialDescriptor = materialCbDescriptorIndex;

        ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
        if ( pBuf == nullptr )
            return;
        _pDevice->_deviceContext->DrawInstancedIndirect( pBuf, argumentBufferOffset );
    }

    void D3D11RHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pDevice->_deviceContext != nullptr && argumentBuffer != 0 )
        {
            ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
            if ( pBuf != nullptr )
                _pDevice->_deviceContext->DrawIndexedInstancedIndirect( pBuf, argumentBufferOffset );
        }
    }

    void D3D11RHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pDevice->_deviceContext != nullptr && argumentBuffer != 0 )
        {
            ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
            if ( pBuf != nullptr )
                _pDevice->_deviceContext->DispatchIndirect( pBuf, argumentBufferOffset );
        }
    }

    void D3D11RHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
    {
        // DX11은 리소스 상태를 명시적으로 전환하지 않음. 심볼 링크용 스텁.
        (void)texture;
    }

    void D3D11RHICommandContext::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
                                                    uint32 maxCommandCount, RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        (void)countBuffer;
        (void)countBufferOffset;
        for ( uint32 commandIndex = 0; commandIndex < maxCommandCount; ++commandIndex )
        {
            const uint32 offset =
                argumentBufferOffset + commandIndex * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) );
            drawIndirect( argumentBuffer, offset );
        }
    }

    void D3D11RHICommandContext::beginEventMarker( const utf8* pName )
    {
        if ( _pDevice->_deviceContext == nullptr || pName == nullptr )
            return;
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
        if ( SUCCEEDED( _pDevice->_deviceContext.As( &annotation ) ) && annotation != nullptr )
        {
            utf16 wide[constant::kMaxBuffer256]{};
            MultiByteToWideChar( CP_UTF8, 0, pName, -1, wide, constant::kMaxBuffer256 );
            annotation->BeginEvent( wide );
        }
    }

    void D3D11RHICommandContext::endEventMarker()
    {
        if ( _pDevice->_deviceContext == nullptr )
            return;
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
        if ( SUCCEEDED( _pDevice->_deviceContext.As( &annotation ) ) && annotation != nullptr )
            annotation->EndEvent();
    }

} // namespace sw
#endif
