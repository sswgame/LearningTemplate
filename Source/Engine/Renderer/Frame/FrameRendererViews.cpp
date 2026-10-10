/**
 * @file FrameRendererViews.cpp
 * @brief 추가 뷰(CCTV · 백미러 · 미니맵 렌더 텍스처, 분할 화면 · PiP 화면 사각형)의 수명과 그리기입니다.
 * @details 뷰는 카메라 id 로 찾고, 뷰마다 트랜지언트 풀 · 컬링 칸(상수버퍼 · 간접 인자 · 가시 목록) · TAA 기록 · 리스트를 따로 듭니다. 준비(만들기 · 크기 ·
 *          텍스처 빌리기 · 컬링 칸 수)는 업로드 **전에** 끝내고, 그리기는 같은 그래프를 그 뷰의 풀 · 시드로 한 번 더 도는 것입니다. 주의: 컬링 상수버퍼를
 *          뷰끼리 나눠 쓰면 뒤 업로드가 앞 디스패치를 덮어쓴다(주 시점이 그림자 절두체로 걸러진 적이 있다) — 뷰마다 자기 것이다.
 */
#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Resource/AssetManager.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    void FrameRenderer::prepareExtraViews( const vector<RenderViewRequest>& listRequest )
    {
        if ( _pDevice == nullptr )
            return;
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            pView->_bSeenThisFrame = SW_FALSE;
        }

        const uint32 requestCount = MathUtil::min( static_cast<uint32>( listRequest.size() ), kMaxExtraRenderView );
        for ( uint32 requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            const RenderViewRequest& request = listRequest[requestIndex];
            ViewTarget*              pView   = nullptr;
            for ( unique_ptr<ViewTarget>& pExisting : _listExtraView )
            {
                if ( pExisting->_viewID == request._viewID )
                {
                    pView = pExisting.get();
                    break;
                }
            }
            if ( pView == nullptr )
            {
                _listExtraView.push_back( make_unique<ViewTarget>() );
                pView          = _listExtraView.back().get();
                pView->_viewID = request._viewID;
            }
            ViewTarget& view       = *pView;
            view._bSeenThisFrame   = SW_TRUE;
            view._settings         = request._settings;
            view._outputKind       = request._outputKind;
            view._bRenderThisFrame = request._bRender;
            view._outputWidth      = request._outputWidth;
            view._outputHeight     = request._outputHeight;
            view._hostTarget       = request._outputKind == RenderViewOutputKind::HostTarget ? request._hostTarget : RHITextureHandle{ 0 };
            if ( view._outputKind == RenderViewOutputKind::HostTarget && view._hostTarget == 0 )
                view._bRenderThisFrame = SW_FALSE;

            // 렌더 텍스처는 캐시에서 빌린다 — 머티리얼이 같은 경로로 읽는다. 크기는 텍스처의 실제 크기다(먼저 만든 쪽이 정했다).
            const bool bRenderTexture = view._outputKind == RenderViewOutputKind::RenderTexture;
            if ( bRenderTexture && ( view._outputPath == request._renderTexture ) == false && engine::areEngineServicesBound() )
            {
                TextureCache& textures = engine::getAssetManager().getTextureManager();
                if ( view._pOutputTexture != nullptr )
                    textures.release( view._outputPath.view(), _pDevice );
                textures.declareRenderTarget( request._renderTexture.view(), request._outputWidth, request._outputHeight );
                view._pOutputTexture = textures.acquire( request._renderTexture.view(), _pDevice );
                view._outputPath     = request._renderTexture;
            }
            if ( bRenderTexture )
            {
                if ( view._pOutputTexture == nullptr || view._pOutputTexture->isRhiValid() == false )
                {
                    view._bRenderThisFrame = SW_FALSE;
                    continue;
                }
                view._outputWidth  = view._pOutputTexture->getWidth();
                view._outputHeight = view._pOutputTexture->getHeight();
            }

            // 컬링 · 정렬 상수버퍼는 뷰마다 자기 것이다(나눠 쓰면 뒤 업로드가 앞 디스패치를 덮어쓴다).
            const bool bCullCb = view._cullInput._cullCb.isValid() || view._cullInput._cullCb.create( _pDevice, sizeof( FrameRendererUtil::GpuCullParams ) );
            const bool bSortCb = view._cullInput._sortCb.isValid() || view._cullInput._sortCb.create( _pDevice, sizeof( FrameRendererUtil::GpuSortParams ) );
            if ( bCullCb == false || bSortCb == false )
            {
                SW_LOG_ERROR( "Failed to create the cull / sort constant buffers of extra view %# - it is not drawn", view._viewID );
                view._bRenderThisFrame = SW_FALSE;
                continue;
            }
            view._cullInput.setViewProjection( request._viewProj );
            view._cullInput._position = request._position;

            if ( view._bRenderThisFrame == SW_TRUE )
            {
                const float32 scale  = MathUtil::clamp( view._settings._resolutionScale, 0.1f, 2.0f );
                const uint32  width  = MathUtil::max( 1u, static_cast<uint32>( static_cast<float32>( view._outputWidth ) * scale + 0.5f ) );
                const uint32  height = MathUtil::max( 1u, static_cast<uint32>( static_cast<float32>( view._outputHeight ) * scale + 0.5f ) );
                (void)ensureViewTransients( view, width, height );
            }
        }

        // 이번 요청에 없는 뷰(카메라가 사라졌다)는 놓는다. 남은 뷰의 컬링 칸은 목록 순서로 고정 칸 뒤에 붙는다.
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bSeenThisFrame == SW_FALSE )
                releaseExtraView( *pView );
        }
        _listExtraView.erase( std::remove_if( _listExtraView.begin(), _listExtraView.end(),
                                              []( const unique_ptr<ViewTarget>& pView )
        { return pView->_bSeenThisFrame == SW_FALSE; } ),
                              _listExtraView.end() );
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( _listExtraView.size() ); ++viewIndex )
        {
            _listExtraView[viewIndex]->_cullSlot = kFirstExtraCullView + viewIndex;
        }
        _gpuScene.setCullViewCount( _pDevice, kFirstExtraCullView + static_cast<uint32>( _listExtraView.size() ) );
    }

    void FrameRenderer::releaseExtraView( ViewTarget& view )
    {
        releaseViewTransients( view );
        view._cullInput._cullCb.release( _pDevice );
        view._cullInput._sortCb.release( _pDevice );
        releaseViewTransparentOrder( view );
        view._commandList.reset();
        if ( view._pOutputTexture != nullptr && engine::areEngineServicesBound() )
            engine::getAssetManager().getTextureManager().release( view._outputPath.view(), _pDevice );
        view._pOutputTexture = nullptr;
        view._outputPath     = hashed_string{};
    }

    void FrameRenderer::applyViewTransparentOrder( ViewTarget& view )
    {
        view._bHasTransparentRank = SW_FALSE;
        view._bUsesViewSlotStream = SW_FALSE;
        view._listTransparentBatchOrder.clear();
        const GpuViewTransparentOrder* pOrder  = _gpuScene.findViewTransparentOrder( view._viewID );
        const bool                     bUsable = pOrder != nullptr && pOrder->_pListRank != nullptr && pOrder->_pListTailSlot != nullptr &&
                             pOrder->_listBatchOrder.size() == _gpuScene.getTransparentBatches().size() &&
                             pOrder->_tailBase + pOrder->_pListRank->size() == _gpuScene.getInstances().size();
        if ( bUsable == false )
            return;
        view._listTransparentBatchOrder = pOrder->_listBatchOrder;
        view._transparentTailBase       = pOrder->_tailBase;
        const uint32 tailCount          = static_cast<uint32>( pOrder->_pListRank->size() );
        if ( _gpuScene.areIndirectCountsGpuFilled() )
        {
            // GPU 정렬이 순번으로 되돌린다(instancesort.hlsl). 표는 뷰마다 자기 것 — 나눠 쓰면 뒤 업로드가 앞 디스패치를 덮는다(정렬 CB 와 같은 함정).
            if ( view._transparentRank.ensureCapacity( _pDevice, sizeof( uint32 ), tailCount, RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
                return;
            view._transparentRank.upload( _pDevice, pOrder->_pListRank->data(), tailCount * static_cast<uint32>( sizeof( uint32 ) ) );
            view._bHasTransparentRank = SW_TRUE;
            return;
        }
        // 컬링이 없는 백엔드는 인스턴스 슬롯 스트림이 곧 그리는 순서다. 앞(불투명)은 항등, 꼬리는 배치 안을 뷰 순서로 다시 놓는다.
        // 내용이 지난 프레임과 같으면 버퍼를 그대로 쓴다(뷰 카메라 · 투명 물체가 서 있으면 순서가 같다).
        const uint32 instanceCount = pOrder->_tailBase + tailCount;
        const bool   bSameContent  = view._instanceSlotStream != 0 && view._listInstanceSlot.size() == instanceCount &&
                                  Memory::compare( view._listInstanceSlot.data() + pOrder->_tailBase, pOrder->_pListTailSlot->data(), tailCount * sizeof( uint32 ) ) == 0;
        if ( bSameContent == false )
        {
            view._listInstanceSlot.resize( instanceCount );
            for ( uint32 slot = 0; slot < pOrder->_tailBase; ++slot )
            {
                view._listInstanceSlot[slot] = slot;
            }
            Memory::copy( view._listInstanceSlot.data() + pOrder->_tailBase, pOrder->_pListTailSlot->data(), tailCount * sizeof( uint32 ) );
            // 지난 프레임 기록이 이 버퍼를 읽었을 수 있다 — 반환은 디바이스가 미룬다(releaseHandle).
            if ( view._instanceSlotStream != 0 )
                _pDevice->releaseHandle( RHIHandleKind::Buffer, view._instanceSlotStream );
            view._instanceSlotStream = _pDevice->getResourceFactory()->createVertexBuffer( view._listInstanceSlot.data(), instanceCount * static_cast<uint32>( sizeof( uint32 ) ) );
            if ( view._instanceSlotStream == 0 )
            {
                view._listInstanceSlot.clear();
                return;
            }
        }
        view._bUsesViewSlotStream = SW_TRUE;
    }

    void FrameRenderer::releaseViewTransparentOrder( ViewTarget& view )
    {
        view._transparentRank.release( _pDevice );
        if ( view._instanceSlotStream != 0 && _pDevice != nullptr )
            _pDevice->releaseHandle( RHIHandleKind::Buffer, view._instanceSlotStream );
        view._instanceSlotStream = 0;
        view._listInstanceSlot.clear();
        view._listTransparentBatchOrder.clear();
        view._bHasTransparentRank = SW_FALSE;
        view._bUsesViewSlotStream = SW_FALSE;
    }

    void FrameRenderer::renderExtraView( IRHIDevice* pDevice, ViewTarget& view )
    {
        if ( view._commandList == nullptr )
            view._commandList = pDevice->createCommandList();
        IRHICommandList* pCmd = view._commandList.get();
        if ( pCmd == nullptr )
            return;

        // 주 시점의 시드에서 출발해 이 뷰의 것(뷰-투영 · 풀 크기 · 플래그 · 컬링 칸)만 덮어쓴다. 라이트 · 그림자 행렬은 프레임 공통이다.
        _pActiveView = &view;
        _frameCtx    = _mainSeedScratch;
        applyViewProjection( _frameCtx, view._cullInput._viewProj );
        applyViewPassConstants( _frameCtx );
        _frameCtx._cullViewIndex = view._cullSlot;
        _frameCtx._pCmd          = pCmd; // 직렬 경로의 패스는 시드의 리스트에 기록한다
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );

        pCmd->beginCommandList();
        (void)_graph.execute( _graphContext, pCmd );
        // 렌더 텍스처는 이어서 주 시점의 머티리얼이, 호스트 타깃은 에디터 UI 가 읽는다 — 읽기 상태로 돌려 둔다(DX11 은 RTV 슬롯에서 뗀다).
        if ( view._outputKind == RenderViewOutputKind::RenderTexture && view._pOutputTexture != nullptr )
            pCmd->prepareTextureForShaderRead( view._pOutputTexture->getHandle() );
        else if ( view._outputKind == RenderViewOutputKind::HostTarget && view._hostTarget != 0 )
            pCmd->prepareTextureForShaderRead( view._hostTarget );
        pCmd->endCommandList();
        pDevice->executeCommandList( pCmd );

        _pActiveView = &_mainView;
    }
} // namespace sw
