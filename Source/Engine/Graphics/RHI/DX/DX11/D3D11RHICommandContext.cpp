#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX11/D3D11RHICommandContext.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{

    D3D11RHICommandContext::D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext )
        : _pDevice{ pDevice }
        , _pContext{ pContext }
        , _pState{ pDevice != nullptr ? &pDevice->_recordingState : nullptr }
        , _bAnnotationQueried{ false }
    {
    }

    D3D11RHICommandContext::D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext,
                                                    D3D11RecordingState* pState )
        : _pDevice{ pDevice }
        , _pContext{ pContext }
        , _pState{ pState }
        , _bAnnotationQueried{ false }
    {
    }

    void D3D11RHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
    {
        if ( _pContext == nullptr || ( src == 0 && dst == 0 ) )
            return;

        if ( src == 0 )
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
                SW_LOG_ERROR( "blitTexture(backbuffer): the target must match the back buffer (fmt %# %#x%# vs %# %#x%#)", static_cast<uint32>( backDesc.Format ),
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
        if ( dst == 0 )
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

    void D3D11RHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        // 등록되지 않은 핸들도 기록한다 — 앞 PSO 를 남겨 두면 다음 드로우가 그것으로 대신 그려진다. 그 드로우는 bindGraphicsPipelineForDraw 에서 멈춘다.
        _pState->_activeGraphicsPSO                             = pso;
        const D3D11RHIDevice::D3D11PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || _pContext == nullptr )
            return;

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
            _pContext->OMSetBlendState( pRecord->_blendState.Get(), arrBlendFactor, MathUtil::kMaxUInt32 );
        }
        if ( pRecord->_depthStencilState )
            _pContext->OMSetDepthStencilState( pRecord->_depthStencilState.Get(), 0 );
    }

    void D3D11RHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
    {
        // 컴퓨트 셰이더만 건다. 그래픽스 PSO 기록(_activeGraphicsPSO)은 건드리지 않는다 — 컴퓨트 PSO 가 그래픽스 자리에 남으면 다음 드로우가 멈춘다.
        // 핸들은 레코드가 없어도 기록한다 — 디스패치가 앞 컴퓨트 PSO 로 대신 돌지 않고 requireComputePipelineForDispatch 에서 멈추게 한다.
        _pState->_activeComputePSO                              = pso;
        const D3D11RHIDevice::D3D11PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || _pContext == nullptr )
            return;
        if ( pRecord->_cs )
            _pContext->CSSetShader( pRecord->_cs.Get(), nullptr, 0 );
    }

    void D3D11RHICommandContext::requireComputePipelineForDispatch() const
    {
        // 백엔드 계약(네 백엔드 같음): 등록된 컴퓨트 PSO 없이 디스패치하면 오류로 멈춘다. 앞 디스패치의 셰이더로 대신 돌지 않고 조용히 건너뛰지도 않는다 — PSO 가 0 이면
        // 부르는 쪽이 디스패치를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · 컴퓨트 PSO 를 걸지 않은 디스패치).
        const D3D11RHIDevice::D3D11PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( _pState->_activeComputePSO );
        if ( pRecord == nullptr || pRecord->_cs == nullptr )
            SW_FATAL( "D3D11 dispatch without a registered compute PSO (handle %#)", _pState->_activeComputePSO );
    }

    void D3D11RHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
    {
        if ( _pContext == nullptr )
            return;

        ID3D11RenderTargetView* arrRTV[kMaxColorAttachments]{};
        uint32                  rtCount{ 0 };
        if ( beginInfo._bBindColor != SW_FALSE )
        {
            const uint32 colorBindCount = beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u;
            for ( uint32 attachmentIndex = 0; attachmentIndex < colorBindCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
            {
                const RHITextureHandle  colorHandle = beginInfo._arrColorTarget[attachmentIndex];
                ID3D11RenderTargetView* pRTV{ nullptr };
                if ( colorHandle == 0 )
                    pRTV = ( attachmentIndex == 0 ) ? _pDevice->_swapChain.getBackBufferRTV() : nullptr;
                else
                {
                    D3D11RHIDevice::TextureRecord* pTexture = _pDevice->resolveTexture( colorHandle );
                    if ( pTexture != nullptr )
                        pRTV = pTexture->findSliceRTV( beginInfo._arrColorTargetSlice[attachmentIndex] );
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
                pDSV = pDepthTex->findSliceDSV( beginInfo._depthTargetSlice );
        }

        if ( beginInfo._depthLoadOp == RHIRenderPassLoadOp::Clear && pDSV != nullptr )
            _pContext->ClearDepthStencilView( pDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, beginInfo._clearDepth, 0 );

        if ( rtCount > 0 )
            _pContext->OMSetRenderTargets( rtCount, arrRTV, pDSV );
        else
            _pContext->OMSetRenderTargets( 0, nullptr, pDSV );

        // 깊이 · 블렌드 상태는 PSO 의 것만 쓴다(setPipelineState 가 건다). 전역 깊이 상태로 대신하지 않는다 — PSO 없이 그리면
        // bindGraphicsPipelineForDraw 가 멈춘다. 패스 앞에 이미 PSO 가 걸려 있으면 그 상태를 다시 건다.
        const D3D11RHIDevice::D3D11PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPSO );
        if ( pRecord != nullptr )
        {
            if ( pRecord->_depthStencilState )
                _pContext->OMSetDepthStencilState( pRecord->_depthStencilState.Get(), 0 );
            if ( pRecord->_blendState )
            {
                constexpr float32 arrBlendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                _pContext->OMSetBlendState( pRecord->_blendState.Get(), arrBlendFactor, MathUtil::kMaxUInt32 );
            }
        }

        D3D11_VIEWPORT viewport{};
        viewport.Width    = static_cast<float32>( beginInfo._width > 0 ? beginInfo._width : _pDevice->_swapChain.getWidth() );
        viewport.Height   = static_cast<float32>( beginInfo._height > 0 ? beginInfo._height : _pDevice->_swapChain.getHeight() );
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        _pContext->RSSetViewports( 1, &viewport );

        // 래스터라이저 상태가 가위를 늘 켜 두므로(PSO) 가위를 패스 전체로 되돌린다 — 앞 패스의 가위가 새지 않게.
        const D3D11_RECT scissor{ 0, 0, static_cast<LONG>( viewport.Width ), static_cast<LONG>( viewport.Height ) };
        _pContext->RSSetScissorRects( 1, &scissor );
    }

    /**
     * @brief D3D11 에는 끝낼 렌더 패스가 **없습니다.** 비어 있는 것이 맞습니다.
     * @details 네 백엔드 중 여기만 본문이 비어 있어서 "빠뜨린 것" 으로 읽히기 쉽습니다. 아닙니다:
     *          - **Vulkan** 은 `vkCmdEndRenderPass` 가 API 의 요구입니다.
     *          - **GL** 은 기본 프레임버퍼와 `glClipControl` 을 되돌려야 합니다(엔진 패스를 거치지
     *            않고 그리는 에디터 ImGui 백엔드가 표준 GL 규약을 가정하기 때문입니다).
     *          - **DX12** 는 배리어 판단에 쓰는 자기 추적 상태(활성 컬러 · 깊이 타깃)를 지웁니다.
     *          D3D11 은 즉시 모드라 패스 객체가 없다. 렌더 타깃도 그대로 둔다 — 에디터 ImGui 가 마지막 패스의 백버퍼 바인딩 위에 그린다.
     *          끝난 타깃을 컴퓨트 UAV 로 거는 해저드는 `bindComputeUAV` 가 RT 를 떼서 막는다.
     *          `D3D11RecordingState` 가 들고 있는 것(메시 VB, 루트 상수 그림자)은 **패스 경계를
     *          넘어 유지되어야 하는 것**이라 여기서 지우면 오히려 틀립니다.
     */
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

    void D3D11RHICommandContext::uavBarrier( RHIBufferHandle buffer )
    {
        // D3D11 은 같은 컨텍스트의 디스패치를 순서대로 실행하고 UAV 위험도 드라이버가 처리한다.
        // 명시적 배리어 개념 자체가 없다. 의도적으로 아무것도 하지 않는다.
        (void)buffer;
    }

    void D3D11RHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
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
        }
    }

    void D3D11RHICommandContext::copyBufferToReadback( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes )
    {
        ID3D11Buffer* pSource        = _pDevice->resolveBuffer( source );
        ID3D11Buffer* pTarget        = _pDevice->resolveBuffer( readback );
        const bool    bNothingToCopy = _pContext == nullptr || pSource == nullptr || pTarget == nullptr || sizeBytes == 0;
        if ( bNothingToCopy )
            return;
        const D3D11_BOX region{ 0, 0, 0, sizeBytes, 1, 1 };
        _pContext->CopySubresourceRegion( pTarget, 0, 0, 0, 0, pSource, 0, &region );
    }

    void D3D11RHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
    {
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav = _pDevice->bindlessUAVAt( index );
        if ( _pContext != nullptr && uav != nullptr )
        {
            // 컴퓨트는 렌더 패스 밖에서 돈다 — 앞 패스의 렌더 타깃이 이 UAV 의 텍스처면(깊이 히스토리) 런타임이 RT 를 강제로 떼며 오류를 낸다. 먼저 뗀다.
            _pContext->OMSetRenderTargets( 0, nullptr, nullptr );
            ID3D11UnorderedAccessView* pUAV = uav.Get();
            _pContext->CSSetUnorderedAccessViews( slot, 1, &pUAV, nullptr );
            // 어느 버퍼가 어느 슬롯에 걸렸는지 남긴다. transitionBuffer 가 이것을 보고 뗀다.
            if ( _pState != nullptr && slot < D3D11_PS_CS_UAV_REGISTER_COUNT )
                _pState->_arrComputeUAVBuffer[slot] = _pDevice->uavSourceBufferAt( index );
        }
    }

    void D3D11RHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        const RHITextureHandle texture = _pDevice->bindlessTextureAt( index );
        if ( _pContext == nullptr || texture == 0 )
            return;
        ID3D11ShaderResourceView*      pSRV{ nullptr };
        D3D11RHIDevice::TextureRecord* pTexture = _pDevice->resolveTexture( texture );
        if ( pTexture != nullptr )
            pSRV = pTexture->_srv.Get();
        _pContext->PSSetShaderResources( slot, 1, &pSRV );
        // 머티리얼 텍스처는 정점 셰이더도 읽는다(툰 외곽선 두께 텍스처 — MToon 이 정점에서 읽는다). 다른 세 백엔드는 모든 스테이지가 본다.
        const bool bMaterialSlot = ( shaderslot::kMaterialTexture0 <= slot && slot < shaderslot::kMaterialTexture0 + shaderslot::kMaterialTextureCount );
        if ( bMaterialSlot )
            _pContext->VSSetShaderResources( slot, 1, &pSRV );
        if ( _pState != nullptr && slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT )
            _pState->_arrPixelSRVTexture[slot] = ( pSRV != nullptr ) ? texture : 0;
        // 짝 샘플러는 걸지 않는다 — DX11 샘플러 슬롯은 16 개뿐이라 엔진 텍스처 칸(t32..)과 머티리얼 칸(t16..t31)은 셰이더가 정적 세트(s9..s15)의 샘플러로 읽는다.
    }

    void D3D11RHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        // 슬롯 1 은 인스턴스 슬롯 스트림(uint, 인스턴스 스텝)이다. constant::arrVertexAttribute 의 SW_INSTANCESLOT.
        if ( slot == constant::kInstanceSlotStreamSlot )
        {
            _pState->_boundInstanceSlotVb     = buffer;
            _pState->_boundInstanceSlotOffset = offset;
            return;
        }
        _pState->_boundMeshVb     = buffer;
        _pState->_boundMeshStride = stride > 0 ? stride : static_cast<uint32>( sizeof( RHIVertex ) );
        _pState->_boundMeshOffset = offset;
    }

    void D3D11RHICommandContext::bindInstanceSlotStream()
    {
        // 슬롯 1: 인스턴스 슬롯 스트림. 안 걸린 드로우(풀스크린 · 픽스처)는 셰이더가 그 속성을 읽지 않으므로 비워 둔다.
        if ( _pState->_boundInstanceSlotVb == 0 )
            return;
        ID3D11Buffer* pStream = _pDevice->resolveBuffer( _pState->_boundInstanceSlotVb );
        if ( pStream == nullptr )
            return;
        UINT stride = constant::kInstanceSlotStreamStride;
        UINT offset = _pState->_boundInstanceSlotOffset;
        _pContext->IASetVertexBuffers( constant::kInstanceSlotStreamSlot, 1, &pStream, &stride, &offset );
    }

    bool D3D11RHICommandContext::bindGraphicsPipelineForDraw()
    {
        ID3D11VertexShader*                             pVs  = nullptr;
        ID3D11PixelShader*                              pPs  = nullptr;
        ID3D11InputLayout*                              pIl  = nullptr;
        const D3D11RHIDevice::D3D11PipelineStateRecord* pPSO = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPSO );
        // 백엔드 계약(네 백엔드 같음): 등록된 그래픽스 PSO 없이 그리면 오류로 멈춘다. 기본 상태로 대신 그리지 않는다 — PSO 가 0 이면 부르는 쪽이
        // 드로우를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · 컴퓨트 PSO 를 그래픽스로 건 것).
        if ( pPSO == nullptr || pPSO->_vs == nullptr )
            SW_FATAL( "D3D11 draw without a registered graphics PSO (handle %#)", _pState->_activeGraphicsPSO );
        pVs = pPSO->_vs.Get();
        if ( pPSO->_ps )
            pPs = pPSO->_ps.Get();
        if ( pPSO->_inputLayout )
            pIl = pPSO->_inputLayout.Get();
        // PS 는 없어도 된다 — 깊이 전용 PSO(그림자 · 깊이 프리패스)는 픽셀 스테이지를 붙이지 않고, 아래 PSSetShader( nullptr )가 앞 드로우의 PS 를 뗀다.
        // 주의: PS 까지 요구하면 깊이 전용 드로우가 **하나도** 나가지 않는다(그림자 맵이 클리어 값뿐이 된다).

        // 메시가 자기 정점 버퍼를 걸었으면 그것을, 아니면 장치의 공용 버퍼를 쓴다. 건 버퍼가 부서졌으면(세대가 달라 풀리지 않는다) 드로우를 버린다 —
        // IASetVertexBuffers 를 건너뛰면 직전 드로우의 버퍼로 그린다.
        ID3D11Buffer* pVb    = _pDevice->_vertexBuffer.Get();
        UINT          stride = static_cast<UINT>( sizeof( RHIVertex ) );
        UINT          offset = 0;
        if ( _pState->_boundMeshVb != 0 )
        {
            pVb = _pDevice->resolveBuffer( _pState->_boundMeshVb );
            if ( pVb == nullptr )
            {
                RHIDrawDiagnostics::reportDestroyedVertexBuffer( "DX11", _pState->_boundMeshVb );
                return false;
            }
            stride = _pState->_boundMeshStride;
            offset = _pState->_boundMeshOffset;
        }
        if ( pVb != nullptr )
            _pContext->IASetVertexBuffers( 0, 1, &pVb, &stride, &offset );
        bindInstanceSlotStream();

        _pContext->IASetInputLayout( pIl );
        _pContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        _pContext->VSSetShader( pVs, nullptr, 0 );
        _pContext->PSSetShader( pPs, nullptr, 0 );
        return true;
    }

    void D3D11RHICommandContext::draw( uint32 vertexCount, uint32 startVertex )
    {
        if ( _pContext == nullptr || vertexCount == 0 )
            return;

        if ( bindGraphicsPipelineForDraw() == false )
            return;

        _pContext->Draw( vertexCount, startVertex );
    }

    void D3D11RHICommandContext::drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        const bool bNothingToDraw = _pContext == nullptr || vertexCount == 0 || instanceCount == 0;
        if ( bNothingToDraw )
            return;

        if ( bindGraphicsPipelineForDraw() == false )
            return;

        _pContext->DrawInstanced( vertexCount, instanceCount, startVertex, startInstance );
    }

    void D3D11RHICommandContext::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        const bool bInvalidConstantUpdate = _pContext == nullptr || _pDevice == nullptr || buffer == 0 || pData == nullptr;
        if ( bInvalidConstantUpdate )
            return;
        ID3D11Buffer* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // 리스트의 Deferred Context 는 이 리스트만 쓰므로 잠그지 않는다. 프레임 스트림(즉시 컨텍스트)은 다른 스레드의
        // `IRHIResourceFactory::updateConstantBuffer` 가 같은 버퍼를 Map 할 수 있어 Map · Unmap 짝을 같은 가드로 묶는다.
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

    void D3D11RHICommandContext::bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot )
    {
        const bool bInvalidConstantBufferIndex = _pContext == nullptr || constantBufferIndex == kInvalidDescriptorIndex ||
                                                 constantBufferIndex >= static_cast<RHIDescriptorIndex>( _pDevice->bindlessBufferCount() );
        if ( bInvalidConstantBufferIndex )
            return;
        ID3D11Buffer* pCb = _pDevice->resolveBuffer( _pDevice->bindlessBufferAt( constantBufferIndex ) );
        if ( pCb == nullptr )
            return;
        _pContext->VSSetConstantBuffers( slot, 1, &pCb );
        _pContext->PSSetConstantBuffers( slot, 1, &pCb );
        _pContext->CSSetConstantBuffers( slot, 1, &pCb );
    }

    ID3D11ShaderResourceView* D3D11RHICommandContext::findBindlessBufferSRV( RHIDescriptorIndex index ) const
    {
        if ( index == kInvalidDescriptorIndex )
            return nullptr;
        // 레지스트리는 공유 락으로 읽고(범위 밖이면 핸들 0), SRV 는 버퍼 레코드에서 락 없이 읽는다(핸들 0 은 nullptr).
        return _pDevice->resolveBufferSRV( _pDevice->bindlessBufferAt( index ) );
    }

    void D3D11RHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 VS/PS 가 읽는 구조버퍼(InstanceData 등). createStructuredBuffer 에서 만든 SRV 를
        // 리플렉션 t 슬롯에 바인딩한다.
        if ( _pContext == nullptr )
            return;
        ID3D11ShaderResourceView* pSRV = findBindlessBufferSRV( index );
        if ( pSRV == nullptr )
            return;
        _pContext->VSSetShaderResources( slot, 1, &pSRV );
        _pContext->PSSetShaderResources( slot, 1, &pSRV );
        // 버퍼가 그 슬롯의 텍스처를 덮는다.
        if ( _pState != nullptr && slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT )
            _pState->_arrPixelSRVTexture[slot] = 0;
    }

    void D3D11RHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot )
    {
        const bool bInvalidConstantBufferIndex = _pContext == nullptr || constantBufferIndex == kInvalidDescriptorIndex ||
                                                 constantBufferIndex >= static_cast<RHIDescriptorIndex>( _pDevice->bindlessBufferCount() );
        if ( bInvalidConstantBufferIndex )
            return;
        ID3D11Buffer* pCb = _pDevice->resolveBuffer( _pDevice->bindlessBufferAt( constantBufferIndex ) );
        if ( pCb == nullptr )
            return;
        _pContext->CSSetConstantBuffers( slot, 1, &pCb );
    }

    void D3D11RHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        // gpucull 등 컴퓨트 셰이더가 읽는 구조버퍼(g_Instances 등)를 CS 스테이지에 바인딩한다.
        if ( _pContext == nullptr )
            return;
        ID3D11ShaderResourceView* pSRV = findBindlessBufferSRV( index );
        if ( pSRV == nullptr )
            return;
        _pContext->CSSetShaderResources( slot, 1, &pSRV );
    }

    void D3D11RHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        if ( _pContext == nullptr )
            return;
        requireComputePipelineForDispatch();
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

        // 가위 = 뷰포트(IRHICommandList::setViewport 규약). DX12 · Vulkan 과 같은 사각형이다.
        const D3D11_RECT scissor{ static_cast<LONG>( viewport._x ), static_cast<LONG>( viewport._y ), static_cast<LONG>( viewport._x + viewport._width ),
                                  static_cast<LONG>( viewport._y + viewport._height ) };
        _pContext->RSSetScissorRects( 1, &scissor );
    }

    void D3D11RHICommandContext::setScissorRect( const RHIScissorRect& rect )
    {
        if ( _pContext == nullptr )
            return;
        const D3D11_RECT scissor{ static_cast<LONG>( rect._x ), static_cast<LONG>( rect._y ), static_cast<LONG>( rect._x + rect._width ),
                                  static_cast<LONG>( rect._y + rect._height ) };
        _pContext->RSSetScissorRects( 1, &scissor );
    }

    void D3D11RHICommandContext::writeRootConstants( uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues, bool bCompute )
    {
        const bool bInvalidRootConstants = _pContext == nullptr || num32BitValues == 0 || pData == nullptr;
        if ( bInvalidRootConstants )
            return;
        if ( destOffsetIn32BitValues >= shaderslot::kRootConstantDwords )
            return;

        const uint32 maxCount = shaderslot::kRootConstantDwords - destOffsetIn32BitValues;
        const uint32 count    = num32BitValues < maxCount ? num32BitValues : maxCount;
        if ( _pDevice->ensureRootConstantCb( *_pState ) == false )
            return;

        Memory::copy( _pState->_arrRootConstantShadow + destOffsetIn32BitValues, pData, static_cast<size_t>( count ) * sizeof( uint32 ) );

        // DX11 에는 루트 상수가 없다. 계약 슬롯 b2 의 작은 상수버퍼로 흉내 낸다. WRITE_DISCARD 라 드로우마다
        // 버퍼가 새로 이름 지어져(rename) 앞 드로우가 읽던 내용과 섞이지 않는다. 버퍼와 그림자는
        // **기록 스트림마다** 따로다. 전역이면 병렬 기록이 서로의 값을 덮는다(D3D11RecordingState 주석).
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if ( isHrSuccess( _pContext->Map( _pState->_rootConstantCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ), "ID3D11DeviceContext::Map (root constants)" ) == false )
            return;
        Memory::copy( mapped.pData, _pState->_arrRootConstantShadow, sizeof( _pState->_arrRootConstantShadow ) );
        _pContext->Unmap( _pState->_rootConstantCb.Get(), 0 );

        // 루트 상수는 계약 슬롯(b SW_SLOT_ROOT_CB_EMUL, SW_ROOT_CONSTANTS_BEGIN)에 건다.
        ID3D11Buffer* pCb = _pState->_rootConstantCb.Get();
        if ( bCompute )
        {
            _pContext->CSSetConstantBuffers( shaderslot::kRootConstantEmulationSlot, 1, &pCb );
        }
        else
        {
            _pContext->VSSetConstantBuffers( shaderslot::kRootConstantEmulationSlot, 1, &pCb );
            _pContext->PSSetConstantBuffers( shaderslot::kRootConstantEmulationSlot, 1, &pCb );
        }
    }

    void D3D11RHICommandContext::setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        // rootParameterIndex 는 DX12 루트 인자 번호라 여기선 뜻이 없다.
        (void)rootParameterIndex;
        writeRootConstants( num32BitValues, pData, destOffsetIn32BitValues, false );
    }

    void D3D11RHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        (void)rootParameterIndex;
        writeRootConstants( num32BitValues, pData, destOffsetIn32BitValues, true );
    }

    void D3D11RHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount,
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

        // **파이프라인 상태를 여기서도 걸어야 한다.** DX11 은 setPipelineState 가 핸들만 기록하고
        // 실제 VS/PS/InputLayout 바인딩은 드로우 시점에 한다(draw/drawInstanced 참고). 빠뜨리면 GPU 드리븐
        // 경로(엔진의 기본 경로)의 모든 드로우가 셰이더도 정점 버퍼도 없이 나가 화면에 클리어 색만 남는다.
        if ( bindGraphicsPipelineForDraw() == false )
            return;

        for ( uint32 commandIndex = 0; commandIndex < drawCount; ++commandIndex )
        {
            _pContext->DrawInstancedIndirect( pBuf, argumentBufferOffset + commandIndex * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
        }
    }

    void D3D11RHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pContext == nullptr || argumentBuffer == 0 )
            return;

        ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
        if ( pBuf == nullptr )
            return;

        // 다른 드로우 진입점과 같이 파이프라인을 여기서 건다. 엔진에서 부르는 곳이 없어 빠뜨려도 드러나지 않는 자리다.
        if ( bindGraphicsPipelineForDraw() == false )
            return;

        _pContext->DrawIndexedInstancedIndirect( pBuf, argumentBufferOffset );
    }

    void D3D11RHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pContext != nullptr && argumentBuffer != 0 )
        {
            requireComputePipelineForDispatch();
            ID3D11Buffer* pBuf = _pDevice->resolveBuffer( argumentBuffer );
            if ( pBuf != nullptr )
                _pContext->DispatchIndirect( pBuf, argumentBufferOffset );
        }
    }

    void D3D11RHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
    {
        // DX11 은 리소스 상태를 명시적으로 전환하지 않는다. 다만 같은 텍스처가 렌더타깃으로 걸린 채
        // SRV 로도 걸려 있으면 런타임이 조용히 한쪽을 풀어버리므로, 읽기로 넘기기 전에 픽셀 셰이더
        // SRV 슬롯을 비운다. 이 호출은 항상 바인딩 **이전**에
        // 오므로(FramePassExecutor::registerPassTexture) 유효한 바인딩을 지우지 않는다.
        (void)texture;
        if ( _pContext == nullptr )
            return;

        ID3D11ShaderResourceView* arrNullSRV[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        _pContext->PSSetShaderResources( 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, arrNullSRV );
        // 머티리얼 슬롯은 정점 셰이더에도 걸린다(bindShaderResource) — 같은 범위만 뗀다(정점 셰이더의 구조버퍼 t4..t11 은 그대로 둔다).
        _pContext->VSSetShaderResources( shaderslot::kMaterialTexture0, shaderslot::kMaterialTextureCount, arrNullSRV );
        if ( _pState != nullptr )
            Memory::set( _pState->_arrPixelSRVTexture, 0, sizeof( _pState->_arrPixelSRVTexture ) );
    }

    void D3D11RHICommandContext::prepareTextureForRenderTarget( RHITextureHandle texture )
    {
        // D3D11 은 상태를 전이하지 않는다. 대신 같은 리소스가 입력과 출력에 함께 걸리면 OMSetRenderTargets 가 SRV 쪽을 NULL 로
        // 강제한다(해저드 경고). 첨부로 쓰기 전에 이 텍스처가 걸린 PS SRV 슬롯만 뗀다. 백버퍼(0)는 SRV 로 걸리지 않는다.
        const bool bNoSRVToRelease = _pContext == nullptr || _pState == nullptr || texture == 0;
        if ( bNoSRVToRelease )
            return;
        ID3D11ShaderResourceView* pNull{ nullptr };
        for ( uint32 slot = 0; slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++slot )
        {
            if ( _pState->_arrPixelSRVTexture[slot] != texture )
                continue;
            _pContext->PSSetShaderResources( slot, 1, &pNull );
            const bool bMaterialSlot = ( shaderslot::kMaterialTexture0 <= slot && slot < shaderslot::kMaterialTexture0 + shaderslot::kMaterialTextureCount );
            if ( bMaterialSlot )
                _pContext->VSSetShaderResources( slot, 1, &pNull );
            _pState->_arrPixelSRVTexture[slot] = 0;
        }
    }

    ID3DUserDefinedAnnotation* D3D11RHICommandContext::getAnnotation()
    {
        if ( _bAnnotationQueried == false )
        {
            _bAnnotationQueried = true;
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
            // **0 을 반환하고 널 종단을 보장하지 않으므로**, 그대로 `BeginEvent` 에 넘기면 배열 밖까지
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
