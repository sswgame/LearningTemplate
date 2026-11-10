/**
 * @file FramePassExecutorDraw.cpp
 * @brief 패스 안의 드로우 기록입니다 — 렌더 패스 열기(컬러 · MRT · 깊이), 그림자 타일 · 캐시 층, 바인딩 등록 · 커밋, GPU 배치 멀티 드로우 · 입자, 쿠키 아틀라스 · 층 굽기, 풀스크린.
 * @details `FramePassExecutor` 의 한 부분이다 — 그래프 짓기 · 제출은 `FramePassExecutor.cpp` 에 있다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/Debug/GPUPixelInspector.h"
#include "Engine/Graphics/Debug/RenderDiagnostics.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/Diagnostics/BindlessIndexValidator.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Graphics/Texture/TextureLayerBake.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/Capture/FrameCapture.h"
#include "Engine/Renderer/Frame/Execute/FramePassExecutor.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"
#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"
#include "Engine/Renderer/Frame/PostProcess/FramePostGrade.h"
#include "Engine/Renderer/Frame/PostProcess/FrameUpscale.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Frame/Resource/ShaderParameterBinder.h"
#include "Engine/Renderer/Frame/Screen/FrameScreenRotation.h"
#include "Engine/Renderer/Lighting/ShadowFilter.h"
#include "Engine/Renderer/Pipeline/GBufferLayout.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    bool FramePassExecutor::beginColorPass( FramePassContext& context, const hashed_string& colorName, const hashed_string& depthName, const float4& clearColor,
                                            RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad )
    {
        const hashed_string       arrName[]  = { colorName };
        const float4              arrClear[] = { clearColor };
        const RHIRenderPassLoadOp arrLoad[]  = { colorLoad };
        return beginColorPassMrt( context, arrName, arrClear, arrLoad, 1, depthName, depthLoad );
    }

    bool FramePassExecutor::beginColorPassMrt( FramePassContext& context, const hashed_string* pColorNames, const float4* pTargetClearColor, const RHIRenderPassLoadOp* pColorLoad,
                                               uint32 colorCount, const hashed_string& depthName, RHIRenderPassLoadOp depthLoad )
    {
        const bool bInvalidPassTargets = _pDevice == nullptr || context._pCmd == nullptr || pColorNames == nullptr || colorCount == 0;
        if ( bInvalidPassTargets )
            return false;

        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor                           = SW_TRUE;
        const TransientAttachmentPool::Attachment depth = depthName.empty() ? TransientAttachmentPool::Attachment{} : findTransientAttachment( depthName );
        beginInfo._depthLoadOp                          = depthLoad;
        beginInfo._depthStoreOp                         = depth._ops._storeOp;
        beginInfo._clearDepth                           = depth._ops._clearDepth;
        beginInfo._clearStencil                         = depth._ops._clearStencil;
        beginInfo._depthTarget                          = depth._texture;
        beginInfo._width                                = activePool().getWidth();
        beginInfo._height                               = activePool().getHeight();
        beginInfo._colorTargetCount                     = colorCount > kMaxColorAttachments ? kMaxColorAttachments : colorCount;
        for ( uint32 colorTargetIndex = 0; colorTargetIndex < beginInfo._colorTargetCount; ++colorTargetIndex )
        {
            const TransientAttachmentPool::Attachment target = findTransientAttachment( pColorNames[colorTargetIndex] );
            beginInfo._arrColorTarget[colorTargetIndex]      = target._texture;
            // 렌더 패스(뷰포트, 시저)는 타깃의 크기를 따른다. 나눗수가 있는 첨부는 프레임보다 작다. 한 패스의 타깃은 크기가 같다(검증).
            if ( colorTargetIndex == 0 && target._texture != 0 )
            {
                beginInfo._width  = target._width;
                beginInfo._height = target._height;
            }
            // 핸들 0은 백버퍼다. 없는 첨부를 그대로 열면 패스가 화면에 그린다. 열지 않고 알린다(패스 경로라 한 번만).
            if ( beginInfo._arrColorTarget[colorTargetIndex] == 0 )
            {
                if ( _bMissingColorTargetLogged.exchange( 1, std::memory_order_relaxed ) == 0 )
                    SW_LOG_ERROR_ID( "FramePassExecutor.ColorTargetDoesNot", "Color target '%#' does not exist this frame - skipping the pass (handle 0 of a missing attachment is the back buffer, so opening it as is would draw to the screen). "
                                                                             "Check _listOutput and _listAttachment of the pipeline.",
                                     pColorNames[colorTargetIndex].c_str() );
                return false;
            }
            beginInfo._arrLoadOp[colorTargetIndex]  = pColorLoad != nullptr ? pColorLoad[colorTargetIndex] : RHIRenderPassLoadOp::Clear;
            beginInfo._arrStoreOp[colorTargetIndex] = target._ops._storeOp;
            if ( pTargetClearColor != nullptr )
                beginInfo._arrClearColor[colorTargetIndex] = pTargetClearColor[colorTargetIndex];
        }
        context._pCmd->beginRenderPass( beginInfo );
        return true;
    }

    void FramePassExecutor::beginDepthOnlyPass( FramePassContext& context, const hashed_string& depthName, RHIRenderPassLoadOp depthLoad )
    {
        if ( context._pCmd == nullptr )
            return;
        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor                           = SW_FALSE;
        const TransientAttachmentPool::Attachment depth = findTransientAttachment( depthName );
        beginInfo._colorTargetCount                     = 0;
        beginInfo._depthTarget                          = depth._texture;
        beginInfo._depthLoadOp                          = depthLoad;
        beginInfo._depthStoreOp                         = depth._ops._storeOp;
        beginInfo._clearDepth                           = depth._ops._clearDepth;
        beginInfo._clearStencil                         = depth._ops._clearStencil;
        beginInfo._width                                = depth._texture != 0 ? depth._width : activePool().getWidth();
        beginInfo._height                               = depth._texture != 0 ? depth._height : activePool().getHeight();
        context._pCmd->beginRenderPass( beginInfo );
    }

    uint32 FramePassExecutor::computeShadowTileCountPerView() const
    {
        return _state._frameShadow._layout.getCascadeCount() + static_cast<uint32>( _state._frameShadow._listLocalTile.size() );
    }

    bool FramePassExecutor::prepareShadowCacheLayer( FramePassContext& context, const hashed_string& atlasName, bool bTransparentBatch )
    {
        ShadowCache& cache = _state._shadowCache;
        if ( cache.usesCacheLayer() == false || context._pCmd == nullptr )
            return false;
        // 복원 PSO 가 없으면(셋업이 알렸다) 캐시한 타일의 정적 메시가 빠진다 — 이 프레임은 캐시 없이 다 그린다.
        if ( _psoManager.getEnginePSO( RenderPassType::ShadowCacheRestore ) == 0 )
        {
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.ShadowCacheNoRestorePSO", "Shadow cache restore PSO is missing - cached point/spot shadows are drawn without the cache" );
            return false;
        }
        // 층은 기록 전에 만들었다(`prepareShadowCacheLayerResource`) — 여기서는 만들지 않는다. 아틀라스와 크기가 다르면(그 뒤에 바뀌었다) 이 프레임은 캐시 없이 다 그린다.
        const TransientAttachmentPool::Attachment atlas = findTransientAttachment( atlasName );
        const TransientAttachmentPool::Attachment layer = cache.getLayer();
        if ( atlas._texture == 0 || layer._texture == 0 || layer._width != atlas._width || layer._format != atlas._format )
            return false;
        if ( cache.needsRebuild() )
        {
            // 캐시 층을 지우고 캐시하는 타일마다 정적 배치만 그린다(그림자 컬링 칸 — 모든 그림자 볼륨을 덮는다). 타일은 모든 뷰가 같아 프레임에 한 번이다.
            RHIRenderPassBeginInfo beginInfo{};
            beginInfo._bBindColor       = SW_FALSE;
            beginInfo._colorTargetCount = 0;
            beginInfo._depthTarget      = layer._texture;
            beginInfo._depthLoadOp      = RHIRenderPassLoadOp::Clear;
            beginInfo._depthStoreOp     = RHIRenderPassStoreOp::Store;
            beginInfo._clearDepth       = layer._ops._clearDepth;
            beginInfo._clearStencil     = layer._ops._clearStencil;
            beginInfo._width            = layer._width;
            beginInfo._height           = layer._height;
            context._pCmd->beginRenderPass( beginInfo );
            const PassConstantNames&     names     = passConstantNames();
            const RHIPipelineStateHandle pso       = _psoManager.getEnginePSO( RenderPassType::Shadow );
            const uint32                 viewCull  = context._cullViewIndex;
            context._cullViewIndex                 = _pActiveView->_shadowCullSlot;
            context._shadowCasterFilter            = ShadowCasterFilter::StaticOnly;
            const vector<ShadowTileDraw>& listTile = _state._frameShadow._listLocalTile;
            for ( uint32 tileIndex = 0; tileIndex < static_cast<uint32>( listTile.size() ); ++tileIndex )
            {
                if ( cache.getAction( tileIndex ) == ShadowCacheTileAction::DrawAll || acquirePassCb( context ) == false )
                    continue;
                const ShadowTileDraw& tile = listTile[tileIndex];
                RHIViewport           viewport{};
                viewport._x      = static_cast<float32>( tile._cell._x );
                viewport._y      = static_cast<float32>( tile._cell._y );
                viewport._width  = static_cast<float32>( tile._cell._size );
                viewport._height = static_cast<float32>( tile._cell._size );
                context._pCmd->setViewport( viewport );
                context._passValues.setMatrix( names._lightViewProj, tile._viewProj );
                drawSceneMeshes( context, pso, bTransparentBatch );
            }
            context._shadowCasterFilter = ShadowCasterFilter::All;
            context._cullViewIndex      = viewCull;
            context._passValues.setMatrix( names._lightViewProj, _pActiveView->_shadow._lightViewProj );
            context._pCmd->endRenderPass();
            cache.markRebuilt();
        }
        context._pCmd->prepareTextureForShaderRead( layer._texture );
        return true;
    }

    void FramePassExecutor::drawShadowTiles( FramePassContext& context, bool bTransparentBatch, bool bCacheLayer )
    {
        // 그림자는 모든 타일을 덮는 상자로 거른 목록을 쓴다(언리얼의 뷰별 인스턴스 컬링에 해당한다). 끝나면 지금 뷰의 슬롯으로 돌아간다.
        const ViewShadowData&        shadow   = _pActiveView->_shadow;
        const RHIPipelineStateHandle pso      = _psoManager.getEnginePSO( RenderPassType::Shadow );
        const uint32                 viewCull = context._cullViewIndex;
        context._cullViewIndex                = _pActiveView->_shadowCullSlot;

        // 캐스케이드 하나가 아틀라스 전체이고 점광 · 스폿 타일이 없으면 아틀라스가 생기기 전과 같다 — 뷰포트도 행렬도 바꾸지 않는다.
        const bool bWholeAtlas = shadow._cascadeCount <= 1 && _state._frameShadow._listLocalTile.empty() &&
                                 shadow._arrCascadeTile[0]._cell._size == _state._frameShadow._layout.getAtlasResolution();
        if ( bWholeAtlas )
        {
            drawSceneMeshes( context, pso, bTransparentBatch );
        }
        else
        {
            // 타일마다 그 칸을 뷰포트로 잡고(가위도 같이 잡힌다) 그 타일의 행렬로 씬을 그린다. 상수가 바뀌므로 드로우는 새 상수 버퍼 슬롯을 받는다.
            // 캐시한 점광 · 스폿 타일은 캐시 층의 깊이를 픽셀 깊이로 옮긴 뒤(ShadowCacheRestore — 층을 ShadowMap 역할 이름으로 건다) 움직이는 배치만 그린다.
            const PassConstantNames& names = passConstantNames();
            if ( bCacheLayer )
            {
                const TransientAttachmentPool::Attachment layer = _state._shadowCache.getLayer();
                context._resourceRegistry.registerTexture( attachmentNames()._shadowMap, layer._texture, layer._srv );
            }
            const RHIPipelineStateHandle restorePSO = _psoManager.getEnginePSO( RenderPassType::ShadowCacheRestore );
            const uint32                 tileCount  = shadow._cascadeCount + static_cast<uint32>( _state._frameShadow._listLocalTile.size() );
            for ( uint32 tileIndex = 0; tileIndex < tileCount; ++tileIndex )
            {
                const ShadowTileDraw& tile = tileIndex < shadow._cascadeCount ? shadow._arrCascadeTile[tileIndex] : _state._frameShadow._listLocalTile[tileIndex - shadow._cascadeCount];
                RHIViewport           viewport{};
                viewport._x      = static_cast<float32>( tile._cell._x );
                viewport._y      = static_cast<float32>( tile._cell._y );
                viewport._width  = static_cast<float32>( tile._cell._size );
                viewport._height = static_cast<float32>( tile._cell._size );
                context._pCmd->setViewport( viewport );
                // 타일마다 새 상수 슬롯을 잡는다. 슬롯은 패스당 하나라서 같은 슬롯에 행렬만 다시 올리면, GPU 가 나중에 읽는 DX12 · Vulkan 에서는
                // 모든 타일이 마지막에 쓴 행렬을 본다(DX11 · GL 은 업데이트가 명령 순서대로 들어가 가려진다).
                if ( acquirePassCb( context ) == false ) // 링 고갈 — 이 타일은 그리지 않는다(앞 타일의 행렬로 그리지 않는다)
                    continue;
                context._passValues.setMatrix( names._lightViewProj, tile._viewProj );
                // 주 시점의 캐스케이드 타일은 자기 컬링 슬롯의 목록을 그린다(그 캐스케이드 절두체로만 거른 것). 점광 · 스폿 타일과 추가 뷰는 합친 칸이다.
                const bool bCascadeSlot = _pActiveView == &_state._mainView && tileIndex < _state._cascadeCullCount;
                context._cullViewIndex  = bCascadeSlot ? _state._cascadeCullSlotBase + tileIndex : _pActiveView->_shadowCullSlot;
                const bool bCachedTile  = bCacheLayer && tileIndex >= shadow._cascadeCount &&
                                         _state._shadowCache.getAction( tileIndex - shadow._cascadeCount ) != ShadowCacheTileAction::DrawAll;
                if ( bCachedTile )
                {
                    drawFullscreen( context, restorePSO );
                    context._shadowCasterFilter = ShadowCasterFilter::MovableOnly;
                }
                drawSceneMeshes( context, pso, bTransparentBatch );
                context._shadowCasterFilter = ShadowCasterFilter::All;
            }
            // 이 패스 컨텍스트는 패스마다 시드의 사본이지만, 기준 행렬로 되돌려 두면 뒤에 무엇이 붙어도 셰이딩과 같은 행렬을 본다.
            context._passValues.setMatrix( names._lightViewProj, shadow._lightViewProj );
        }
        context._cullViewIndex = viewCull;
    }

    void FramePassExecutor::registerPassTexture( FramePassContext& context, const hashed_string& canonicalName, const hashed_string& attachmentName )
    {
        const TransientAttachmentPool::Attachment attachment = findTransientAttachment( attachmentName );
        if ( attachment._texture != 0 && context._pCmd != nullptr )
            context._pCmd->prepareTextureForShaderRead( attachment._texture );
        context._resourceRegistry.registerTexture( canonicalName, attachment._texture, attachment._srv );
        // 원본을 비켜 읽는 효과(블룸)는 그 원본의 텍셀 크기로 비켜야 한다. 반해상도 첨부면 프레임 텍셀의 두 배다. context 는 패스마다 시드의 사본이라 다음 패스로 새지 않는다.
        const bool bSourceColorAttachment = canonicalName == attachmentNames()._sourceColor && attachment._width > 0 && attachment._height > 0;
        if ( bSourceColorAttachment )
        {
            const float32 width  = static_cast<float32>( attachment._width );
            const float32 height = static_cast<float32>( attachment._height );
            context._passValues.setFloat4( passConstantNames()._sourceTexel, float4{ 1.0f / width, 1.0f / height, width, height } );
        }
    }

    void FramePassExecutor::commitBindlessTextureBindings( FramePassContext& context )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;

        // 주의: 여기서 updatePassConstants 를 부르지 않는다. 이 함수는 드로우 루프 안에서 불리므로 그러면
        // 드로우마다 라이트와 뷰 행렬을 다시 만들고(정규화, 외적, 4x4 곱 두 번) 카메라를 다시 찾고
        // hashed_string 을 여덟 개씩 intern 한다. 그 값들은 모두 프레임 상수라 execute/executePacket
        // 이 프레임 시드(_state._frameContext)에 한 번만 채우면 되고, 패스 컨텍스트는 그 시드를 복사해 간다.
        // 드로우마다 바뀌는 것은 g_World 하나뿐이고 그것은 bindForDraw 가 넣는다.

        // DX11 과 GL 은 패스가 이름으로 건 엔진 텍스처를 역할마다 제 칸에 바인딩한다(bindless 에뮬레이션 — shaderslot::emulated 표).
        // DX12 와 VK 는 셰이더가 힙이나 배열을 직접 인덱싱한다. 리플렉션 바인더가 g_<Name>Index 를 채운다.
        if ( _pDevice->supportsNativeBindlessSampling() )
            return;

        // 역할 하나가 칸 하나라 고를 것이 없다 — 한 패스가 어떤 역할을 함께 읽어도 겹치지 않는다(예전에는 네 칸을 나눠 써 선언 밖에서 거는 쿠키 · 노출이 부딪혔다).
        // 이 함수는 드로우 루프 안에서 불린다. 이름은 처음 한 번 intern 한다 — 문자열로 찾으면 **드로우마다** intern 하게 된다(FNV 해시 + 샤드 뮤텍스).
        static const auto s_arrName = []
        {
            array<hashed_string, shaderslot::kEngineTextureCount> arrName{};
            for ( uint32 ordinal = 0; ordinal < shaderslot::kEngineTextureCount; ++ordinal )
                arrName[ordinal] = hashed_string( shaderslot::emulated::kEngineTextureNames[ordinal] );
            return arrName;
        }();
        for ( uint32 ordinal = 0; ordinal < shaderslot::kEngineTextureCount; ++ordinal )
        {
            const RegisteredTexture* pTex = context._resourceRegistry.findTexture( s_arrName[ordinal] );
            if ( pTex != nullptr && pTex->_srv != kInvalidDescriptorIndex )
                context._pCmd->bindShaderResource( pTex->_srv, shaderslot::kEngineTexture0 + ordinal );
        }
    }

    FramePassExecutor::PresentTarget FramePassExecutor::resolvePresentTarget() const
    {
        // 렌더 텍스처와 호스트 타깃 뷰는 자기 출력 전체, 주 시점과 화면 사각형 뷰는 주 출력(백버퍼, 게임 뷰 RT)이다. 스크린샷 실행이면 백버퍼 대신 캡처
        // 텍스처에 그린다. 백버퍼는 핸들이 없어 읽을 수 없고, 후처리와 UI 가 끝난 최종 화면을 볼 방법이 그것뿐이다. 캡처는 주 출력만 받는다.
        const FrameViewTarget& activeView = *_pActiveView;
        RHITextureHandle       ownOutput{ 0 };
        const bool             bExtraViewOwnOutput = isRenderingExtraView() && activeView._outputKind == RenderViewOutputKind::RenderTexture && activeView._pOutputTexture != nullptr;
        if ( bExtraViewOwnOutput )
            ownOutput = activeView._pOutputTexture->getHandle();
        else if ( isRenderingExtraView() && activeView._outputKind == RenderViewOutputKind::ViewportTarget )
            ownOutput = activeView._viewportTarget;
        const bool bOwnOutput = ownOutput != 0;
        const bool bCapture   = bOwnOutput == false && ( _state._outputRenderTarget == 0 ) && _capture.isPresentActive();
        // 캡처를 백버퍼로 옮기는 것은 출력이 곧 백버퍼일 때만이다. 크기를 덮어쓴 출력(초상화 베이크)은 화면에 나가지 않는다(크기가 다르면 복사도 안 된다).
        const bool bCaptureToBack = bCapture && _state._outputWidth == _pDevice->getBackBufferWidth() && _state._outputHeight == _pDevice->getBackBufferHeight();
        // 출력이 RT(에디터 게임 뷰)면 그 RT 에 그대로 그리고 끝에 캡처로 복사한다. 화면 사각형 뷰도 같은 RT 에 겹쳐 그리므로 뷰마다 복사하면
        // 마지막 복사가 화면과 같다.
        const bool bCaptureFromOutput = bOwnOutput == false && _state._outputRenderTarget != 0 && _capture.isPresentActive();

        PresentTarget target{};
        // 주 출력이 화면이면 화면 대상이다 — 회전한 휴대 기기 화면이면 논리 크기 대기 텍스처(끝에 FrameScreenRotation 이 스왑체인으로 돌려 옮긴다), 아니면 0(스왑체인).
        const RHITextureHandle mainOutput = _state._outputRenderTarget != 0 ? _state._outputRenderTarget : _screenRotation.getScreenTarget();
        target._texture                   = bOwnOutput ? ownOutput : bCapture ? _capture.getPresentTexture()
                                                                              : mainOutput;
        target._width                     = bOwnOutput ? activeView._outputWidth : _state._outputWidth;
        target._height                    = bOwnOutput ? activeView._outputHeight : _state._outputHeight;
        // 백버퍼는 디바이스가 채택한 포맷(Vulkan 은 서피스 협상 결과)이고, 텍스처는 그 텍스처가 기록한 포맷이다.
        target._format             = ( target._texture == 0 ) ? _pDevice->getBackBufferFormat() : _pDevice->getResourceFactory()->getTextureFormat( target._texture );
        target._bOwnOutput         = bOwnOutput ? SW_TRUE : SW_FALSE;
        target._bCapture           = bCapture ? SW_TRUE : SW_FALSE;
        target._bCaptureToBack     = bCaptureToBack ? SW_TRUE : SW_FALSE;
        target._bCaptureFromOutput = bCaptureFromOutput ? SW_TRUE : SW_FALSE;
        return target;
    }

    bool FramePassExecutor::isLastSwapchainWriter( const RenderGraphPassDesc& passDesc ) const
    {
        return _pLastSwapchainWriter == nullptr || &passDesc == _pLastSwapchainWriter;
    }

    const RenderGraphPassDesc* FramePassExecutor::findPassDescByType( RenderPassType passType ) const
    {
        // 값만 비교한다 — 표기는 읽을 때 enum 으로 검사했다.
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._type == passType )
                return &pass;
        }
        return nullptr;
    }

    void FramePassExecutor::registerInstanceBuffer( FramePassContext& context )
    {
        if ( _state._gpuScene.getInstanceBuffer() == 0 || _state._gpuScene.getInstanceSRV() == kInvalidDescriptorIndex )
            return;
        // 표면 변형 층 — 씬 지오메트리 패스(그림자 포함)의 정점 셰이더가 같은 텍스처 · 창을 읽는다. 올라가기 전에는 걸지 않고 창도 0 이다(FrameRenderer::updatePassConstants).
        if ( _state._surfaceDeform._texture.isReady() )
        {
            const Texture2D* pDeform = _state._surfaceDeform._texture.getOperation()->getTexture();
            context._resourceRegistry.registerTexture( attachmentNames()._surfaceDeform, pDeform->getHandle(), pDeform->getSRV() );
        }
        // 이름 "SwInstances" ↔ binding.hlsli 의 g_SwInstances / PassCB g_SwInstancesIndex (canonical 매칭).
        context._resourceRegistry.registerBuffer( passConstantNames()._swInstances,
                                                  _state._gpuScene.getInstanceBuffer(), _state._gpuScene.getInstanceSRV() );
        context._passValues.setUint( passConstantNames()._swInstanceCount, static_cast<uint32>( _state._gpuScene.getInstances().size() ) );

        // 배치 표. **패스당 한 번** 건다. 배치마다 다른 값(인스턴스 시작 · 모프 풀 시작 · 정점 풀 시작)이 모두 여기 있어
        // 드로우는 배치 번호만 실어 나른다. 그래서 같은 PSO 의 배치들을 멀티 드로우 하나로 낼 수 있다.
        if ( _state._gpuScene.getBatchInfoBuffer() != 0 && _state._gpuScene.getBatchInfoSRV() != kInvalidDescriptorIndex )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swBatches, _state._gpuScene.getBatchInfoBuffer(), _state._gpuScene.getBatchInfoSRV() );
            context._passValues.setUint( passConstantNames()._swBatchCount, _state._gpuScene.getIndirectCommandCount() );
        }

        // 모프 결과 풀. **패스당 한 번** 건다. 배치는 시작 오프셋을 배치 표에 싣는다(드로우 사이에
        // 바인딩이 바뀌지 않는다는 이 엔진의 규약). 안 걸리면 셰이더가 g_SwMorphVerticesIndex 로 알아채고
        // 입력 스트림을 그대로 쓴다.
        const RHIStructuredBuffer& morphBuffer = _state._deform._meshMorphPool.getMorphBuffer();
        if ( morphBuffer._buffer != 0 && morphBuffer._srv != kInvalidDescriptorIndex )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swMorphVertices, morphBuffer._buffer, morphBuffer._srv );
            context._passValues.setUint( passConstantNames()._swMorphVertexCount, _state._deform._meshMorphPool.getResultVertexCapacity() );
        }
        // 스킨 칸 표 — 패스당 한 번. 스키닝을 디스패치할 수 없으면 걸지 않는다 — 칸이 있는 인스턴스는 그리지 않는다(레스트 포즈로 대신 그리지 않는다, 풀이 오류를 남겼다).
        // 진단(SkipDispatch)이면 스킨 결과가 없어 걸지 않는다 — 칸 있는 인스턴스는 그 진단에서 그려지지 않는다.
        const RHIStructuredBuffer& skinResultOffsetBuffer = _state._deform._meshMorphPool.getSkinResultOffsetBuffer();
        const bool                 bSkinResultOffsets     = _state._deform._bDeformDispatchSkipped == SW_FALSE && _state._deform._meshMorphPool.isSkinDispatchable() && skinResultOffsetBuffer._buffer != 0 &&
                                        skinResultOffsetBuffer._srv != kInvalidDescriptorIndex;
        if ( bSkinResultOffsets )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swSkinResultOffsets, skinResultOffsetBuffer._buffer, skinResultOffsetBuffer._srv );
            context._passValues.setUint( passConstantNames()._swSkinResultOffsetCount, _state._deform._meshMorphPool.getSkinResultOffsetCount() );
            // 스킨 결과는 두 벌이다 — 이번 벌의 이동과 지난 프레임 결과가 든 벌의 이동(없으면 무효 — 셰이더가 이번 위치를 지난 위치로 쓴다).
            context._passValues.setUint( passConstantNames()._swSkinResultShift, _state._deform._meshMorphPool.getSkinCurrentShift() );
            context._passValues.setUint( passConstantNames()._swSkinPreviousShift, _state._deform._meshMorphPool.getSkinPreviousShift() );
        }

        // 지난 월드 행렬 — 패스당 한 번. 화면 속도를 쓰는 패스의 정점 셰이더만 읽는다. 안 걸리면 셰이더가 이번 행렬을 쓴다(속도는 카메라 움직임뿐).
        const RHIStructuredBuffer& prevWorldBuffer = _state._gpuScene.getPrevInstanceWorldBuffer();
        if ( prevWorldBuffer._buffer != 0 && prevWorldBuffer._srv != kInvalidDescriptorIndex )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swPrevInstanceWorlds, prevWorldBuffer._buffer, prevWorldBuffer._srv );
            context._passValues.setUint( passConstantNames()._swPrevInstanceWorldCount, _state._gpuScene.getPrevInstanceWorldCount() );
        }

        // 정점 애니메이션(VAT) 표 — 패스당 한 번. 시계는 게임 스레드의 군중 시계(스냅샷)라 인스턴스 시각 오프셋과 더하면 CPU 의 클립 시각이다.
        const RHIStructuredBuffer& vertexAnimationBuffer = _state._deform._vertexAnimationPool.getBuffer();
        if ( vertexAnimationBuffer._buffer != 0 && vertexAnimationBuffer._srv != kInvalidDescriptorIndex )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swVertexAnimation, vertexAnimationBuffer._buffer, vertexAnimationBuffer._srv );
            context._passValues.setUint( passConstantNames()._swVertexAnimationCount, _state._deform._vertexAnimationPool.getWordCount() );
            context._passValues.setFloat( passConstantNames()._swVertexAnimationTime, _state._gpuScene.getVertexAnimationTime() );
        }
        // 머티리얼 애니메이션 시계(툰 UV 애니메이션) — VAT 와 같은 장면 애니메이션 시계지만 VAT 표와 무관하게 늘 건다.
        context._passValues.setFloat( passConstantNames()._swMaterialTime, _state._gpuScene.getVertexAnimationTime() );

        // 컬링이 실제로 목록을 만들었을 때만 건다. 안 걸리면 셰이더가 g_SwVisibleInstanceIDsIndex 로 알아채고
        // 배치 시작 + 서수를 쓴다(컬링 없음 경로). 반대로 목록만 걸고 컬링을 안 돌리면 **비어 있는
        // 목록**을 읽어 모두 0 번 인스턴스를 그린다. 그래서 둘은 반드시 같이 켜지고 같이 꺼진다.
        const GPUCullViewResources& cullView                  = _state._gpuScene.getCullView( context._cullViewIndex );
        const bool                  bVisibleInstanceListReady = _frameGraphExecutor.isGPUCullingActive() != SW_FALSE && cullView._visibleInstances._buffer != 0 &&
                                               cullView._visibleInstances._srv != kInvalidDescriptorIndex;
        if ( bVisibleInstanceListReady )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swVisibleInstanceIDs,
                                                      cullView._visibleInstances._buffer, cullView._visibleInstances._srv );
            context._passValues.setUint( passConstantNames()._swVisibleSlotCount, _state._gpuScene.getVisibleSlotCount() );
        }
    }

    void FramePassExecutor::registerPostProcessTextures( FramePassContext& context )
    {
        // 뷰의 후처리 텍스처는 셋업이 이번 프레임에 읽힌 것만 골라 두었다(`applyViewPassConstants`). 걸린 것이 없으면 셰이더는 그 효과를 끈 상수를 받았다.
        const FrameViewTarget& view = *_pActiveView;
        if ( view._pColorGradingLUT != nullptr )
            context._resourceRegistry.registerTexture( attachmentNames()._colorGradingLUT, view._pColorGradingLUT->getHandle(), view._pColorGradingLUT->getSRV() );
        if ( view._pBloomDirtMask != nullptr )
            context._resourceRegistry.registerTexture( attachmentNames()._bloomDirtMask, view._pBloomDirtMask->getHandle(), view._pBloomDirtMask->getSRV() );
    }

    void FramePassExecutor::registerLightBuffer( FramePassContext& context )
    {
        // 2D 빛 쿠키 아틀라스 — 쿠키를 쓴 빛이 있는 프레임에만 역할 텍스처로 건다(셰이더는 g_Light2DCookieAtlasIndex 로 걸렸는지 본다).
        const Texture2D* pCookieAtlas = _state._cookieAtlas.hasCookies() ? _state._cookieAtlas.findAtlas() : nullptr;
        if ( pCookieAtlas != nullptr )
            context._resourceRegistry.registerTexture( attachmentNames()._light2DCookieAtlas, pCookieAtlas->getHandle(), pCookieAtlas->getSRV() );
        // 개수는 버퍼가 없어도 채운다. 셰이더는 0 이면 빛을 돌지 않는다(앰비언트만).
        context._passValues.setUint( passConstantNames()._swLightCount, _state._lightBuffer.isBindable() ? _state._lightBuffer.getCount() : 0u );
        // 지금 그리는 뷰의 라이트 그리드(t12). 없으면(생산자 None, 투영이 깨진 뷰) 셰이더는 목록 전체를 돈다.
        // 그리드 첫 단어와 버퍼는 같이 켜지고 같이 꺼진다 — 첫 단어만 있고 버퍼가 없으면 셰이더가 빈 슬롯을 읽는다(LightGrid::hasGrid 가 둘 다 본다).
        const RHIStructuredBuffer* pGridBuffer = _state._lightBuffer.isBindable() ? _state._lightBuffer.findGridBuffer() : nullptr;
        const bool                 bGridReady  = pGridBuffer != nullptr && pGridBuffer->_buffer != 0 && pGridBuffer->_srv != kInvalidDescriptorIndex;
        const uint32               gridBase    = bGridReady ? _state._lightBuffer.getGridBase( _pActiveView->_lightGridIndex ) : constant::invalid_index::kUint32;
        context._passValues.setUint( passConstantNames()._swLightGridBase, gridBase );
        if ( _state._lightBuffer.isBindable() == false )
            return;
        const RHIStructuredBuffer& buffer = _state._lightBuffer.getBuffer();
        context._resourceRegistry.registerBuffer( passConstantNames()._swLights, buffer._buffer, buffer._srv );
        if ( bGridReady )
            context._resourceRegistry.registerBuffer( passConstantNames()._swLightGrid, pGridBuffer->_buffer, pGridBuffer->_srv );
        // 라이트 프로브 SH9(t13) — 볼륨 원소가 라이트 목록에 있는 프레임에만 있다. 셰이더는 g_SwLightProbesIndex 로 걸렸는지 본다.
        const RHIStructuredBuffer* pProbeBuffer = _state._lightBuffer.findProbeBuffer();
        if ( pProbeBuffer != nullptr )
            context._resourceRegistry.registerBuffer( passConstantNames()._swLightProbes, pProbeBuffer->_buffer, pProbeBuffer->_srv );
    }

    void FramePassExecutor::registerMaterialBuffer( FramePassContext& context, const GPUMeshBatchBinding& binding, RHIPipelineStateHandle pso )
    {
        // 배치의 셰이더 타입에 해당하는 머티리얼 데이터 버퍼. 이름 "SwMaterials" ↔ binding.hlsli 의 g_SwMaterials(t5).
        // 바인더가 리플렉션 슬롯에 걸고, 셰이더는 인스턴스의 materialIndex 로 원소를 읽는다(드로우별 CB 바인딩 없음).
        if ( binding._materialBuffer == 0 || binding._materialSRV == kInvalidDescriptorIndex )
        {
            // 머티리얼 없는 배치. 빈 슬롯으로 그리지 않는다(Vulkan 은 partially-bound 슬롯을 실제로 읽으면 정의되지 않는다).
            // 폴백은 **이 PSO 셰이더의 레이아웃**(stride · 텍스처 인덱스 필드)으로 만든 것을 고른다. 공용 256 바이트를 걸면 DX11 이 드로우마다
            // "structure stride 256 vs 24" 를 낸다(SRV 의 구조 stride 는 셰이더 선언과 같아야 한다).
            const ShaderBindingLayout* pLayout = ( context._lastLayoutPSO == pso ) ? context._pLastLayout : _psoManager.findLayout( pso );
            const ShaderBindingSlot*   pSlot   = ( pLayout != nullptr ) ? pLayout->find( passConstantNames()._swMaterials ) : nullptr;
            if ( pSlot == nullptr || pSlot->_elementStride == 0 )
                return; // 셰이더가 머티리얼 버퍼를 선언하지 않았다. 걸 것도 없다.
            const RHIStructuredBuffer* pFallback            = _psoManager.findMaterialFallback( pLayout );
            const bool                 bUseFallbackMaterial = pFallback != nullptr && pFallback->isValid() && pFallback->_srv != kInvalidDescriptorIndex;
            if ( bUseFallbackMaterial )
            {
                context._resourceRegistry.registerBuffer( passConstantNames()._swMaterials, pFallback->_buffer, pFallback->_srv );
                context._drawMaterialCount = 1u;
            }
            else if ( _bMaterialFallbackMissingLogged.exchange( 1, std::memory_order_relaxed ) == 0 )
            {
                // 여기서 조용히 나가면 t5 가 빈 채로 드로우가 나가고 Vulkan 은 디바이스를 잃는다.
                // 막는 것은 ensureMaterialPSOs 쪽이고, 그래도 새면 원인을 알 수 있게 남긴다.
                SW_LOG_ERROR_ID( "FramePassExecutor.NoMaterialFallbackBuffer", "No material fallback buffer for stride %# - this draw goes out with g_SwMaterials empty.",
                                 pSlot->_elementStride );
            }
            return;
        }
        context._resourceRegistry.registerBuffer( passConstantNames()._swMaterials, binding._materialBuffer, binding._materialSRV );
        context._drawMaterialCount = binding._materialCount;
    }

    bool FramePassExecutor::bindForDraw( FramePassContext& context, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
                                         const RHIDescriptorIndex* pMaterialTexSRV )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return false;

        context._passValues.setMatrix( passConstantNames()._world, context._world );

        // 같은 PSO 로 연속 드로우하는 것이 흔한 패턴이라, 패스 로컬 1칸 캐시로
        // FramePSOManager::findLayout() 의 뮤텍스 + 해시맵 조회를 매 드로우 반복하지 않게 한다.
        if ( context._lastLayoutPSO != pso )
        {
            context._pLastLayout   = _psoManager.findLayout( pso );
            context._lastLayoutPSO = pso;
        }
        const ShaderBindingLayout* pLayout = context._pLastLayout;
        if ( pLayout == nullptr || pLayout->isEmpty() )
            return true; // 걸 것이 없다(바인딩 없는 셰이더) — 그대로 그린다

        // 배치마다 바뀌는 값은 **루트/푸시 상수**로 싣는다. 커맨드 리스트에 값이 그대로 들어가므로 드로우끼리
        // 덮어쓸 수 없다. 그래서 PassCB 는 패스당 하나면 충분하다(PassCB 에 넣으면 드로우마다 버퍼를 새로 잡아야
        // 한다). 언리얼의 드로우별 느슨한 파라미터와 같은 자리다.
        const uint32 arrDrawRootConstant[] = { context._drawMaterialCount };
        context._pCmd->setGraphicsRootConstants( 0, static_cast<uint32>( sizeof( arrDrawRootConstant ) / sizeof( arrDrawRootConstant[0] ) ),
                                                 arrDrawRootConstant );

        // 앞 드로우가 이미 올린 슬롯에 다른 값을 다시 쓰지 않는다 — GPU 가 나중에 읽는 DX12 · Vulkan 에서는 그 패스의 모든 드로우가 마지막 값을 본다
        // (데칼 여럿이 마지막 데칼의 상자로 그려졌다). 그리기마다 값이 바뀌는 반복(데칼 · 후처리 머티리얼 · 그림자 타일)은 여기서 새 슬롯을 받는다.
        const bool bSlotAlreadyDrawn = context._lastCbBuffer == context._passCb && context._passCb != 0;
        const bool bValuesChanged    = context._lastCbValuesVersion != context._passValues.getVersion() || context._lastCbRegistryVersion != context._resourceRegistry.getVersion();
        if ( bSlotAlreadyDrawn && bValuesChanged && acquirePassCb( context ) == false )
            return false; // 링 고갈 — 이 드로우는 그리지 않는다(앞 드로우의 슬롯을 덮지 않는다, 링이 이유를 남겼다)
        const RHIConstantBuffer engineCb{ context._passCb, context._passCbIndex };

        // 엔진 상수버퍼를 이 드로우에서 다시 만들 필요가 있나. 값 · 레지스트리 버전과 버퍼가 모두 그대로면 없다.
        // **PSO 도 같아야 한다.** 이 플래그는 상수버퍼 재업로드만이 아니라 리소스 재바인딩까지 건너뛰게 하는데,
        // 슬롯 상태는 PSO 가 바뀌는 순간 백엔드가 비우기 때문이다(FramePassContext::_lastBindPSO 참고).
        const uint32 valuesVersion   = context._passValues.getVersion();
        const uint32 registryVersion = context._resourceRegistry.getVersion();
        const bool   bUpToDate       = ( context._lastBindPSO == pso ) && ( context._lastCbBuffer == engineCb._buffer ) &&
                               ( context._lastCbValuesVersion == valuesVersion ) && ( context._lastCbRegistryVersion == registryVersion );

        const BindlessIndexCheckSite  indexCheck{ _pDevice, context._passName };
        const BindlessIndexCheckSite* pIndexCheck = &indexCheck;
        ShaderParameterBinder::bindGraphics( *context._pCmd, *pLayout, context._resourceRegistry, context._passValues, engineCb, materialCb,
                                             _pDevice->supportsNativeBindlessSampling(), pMaterialTexSRV, bUpToDate, pIndexCheck );

        context._lastBindPSO           = pso;
        context._lastCbBuffer          = engineCb._buffer;
        context._lastCbValuesVersion   = valuesVersion;
        context._lastCbRegistryVersion = registryVersion;

        // 셰이더 메시지 변형 패스(표의 kGPUMessageTarget — Present 의 fullscreen_blit)는 드로우마다 링을 u4 에 건다. 링이 없으면(-gv_gpuMessages 꺼짐) 아무것도 하지 않는다.
        if ( getRenderPassTypeInfo( context._passType ).hasFlag( RenderPassTraitFlag::kGPUMessageTarget ) )
            _state._gpuMessages.bindGraphics( *context._pCmd );
        return true;
    }

    void FramePassExecutor::drawSceneMeshes( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;

        if ( _state._gpuScene.isUploaded() )
        {
            drawGPUBatches( context, pso, bTransparentPass, nullptr );
            return;
        }

        // 그릴 메시가 하나도 없다. 상태만 맞춰 두고 나간다.
        //
        // 주의: 씬을 다시 순회하는 폴백이나 배치마다 drawInstanced 를 부르는 두 번째 드로우 루프를 두지 않는다.
        // 네 백엔드가 모두 인다이렉트 드로우를 지원하고, 경로가 둘이면 새 기능(컬링 · 정렬 · 인스턴스 애니메이션)이
        // 한쪽에만 들어가 다른 쪽이 조용히 다른 그림을 낸다.
        if ( pso != 0 )
            context._pCmd->setPipelineState( pso );

        setIdentityWorld( context );
        commitBindlessTextureBindings( context );
    }

    void FramePassExecutor::drawTransparentQueue( FramePassContext& context, RHIPipelineStateHandle pso )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;
        const vector<GPUTransparentParticleSlot>& listParticleSlot = findActiveParticleSlots();
        if ( _state._gpuScene.isUploaded() )
        {
            drawGPUBatches( context, pso, true, &listParticleSlot );
            return;
        }
        // 메시가 없으면 입자만 이 뷰의 순서로 그린다.
        drawSceneMeshes( context, pso, true );
        (void)drawParticleSlots( context, vector_reference<const GPUTransparentParticleSlot>( listParticleSlot ) );
    }

    const vector<GPUTransparentParticleSlot>& FramePassExecutor::findActiveParticleSlots() const
    {
        if ( isRenderingExtraView() && _pActiveView->_bHasParticleOrder == SW_TRUE )
            return _pActiveView->_listParticleSlot;
        return _state._gpuScene.getParticleSlots();
    }

    uint32 FramePassExecutor::countDrawGroup( const BatchDrawState& state, RenderViewMode drawMode, uint32 orderIndex, bool bDrawsParticles ) const
    {
        const uint32 batchCount = static_cast<uint32>( state._batches.size() );
        const uint32 batchIndex = ( state._pOrder != nullptr ) ? ( *state._pOrder )[orderIndex] : orderIndex;
        uint32       groupCount = 1;
        if ( state._bMerge == false )
            return groupCount;
        const GPUMeshBatch&        head        = state._batches[batchIndex];
        const GPUMeshBatchBinding& headBinding = state._bindings[batchIndex];
        while ( orderIndex + groupCount < batchCount )
        {
            const uint32 nextBatchIndex = ( state._pOrder != nullptr ) ? ( *state._pOrder )[orderIndex + groupCount] : orderIndex + groupCount;
            // 다음 입자의 자리가 묶음 안에 들면 멀티 드로우 묶음을 그 자리에서 끊는다.
            const bool bParticleInside = bDrawsParticles && state._particleSlotCursor < state._listParticleSlot.size() &&
                                         state._listParticleSlot[state._particleSlotCursor]._drawPosition <= orderIndex + groupCount;
            const bool bGroupEnds = nextBatchIndex != batchIndex + groupCount || bParticleInside;
            if ( bGroupEnds || drawsCasterBatch( state._casterFilter, state._batches[nextBatchIndex] ) == false ||
                 isSameDrawGroup( state._pso, state._passType, drawMode, head, headBinding, state._batches[nextBatchIndex], state._bindings[nextBatchIndex] ) == false )
                break;
            ++groupCount;
        }
        return groupCount;
    }

    void FramePassExecutor::drawBatchRound( FramePassContext& context, BatchDrawState& inoutState, RenderViewMode drawMode, bool bDrawsParticles )
    {
        const uint32 batchCount = static_cast<uint32>( inoutState._batches.size() );
        uint32       orderIndex{ 0 };
        while ( orderIndex < batchCount )
        {
            if ( bDrawsParticles && drawParticleRun( context, inoutState._listParticleSlot, inoutState._particleSlotCursor, orderIndex ) )
            {
                inoutState._boundPSO          = 0;
                inoutState._boundVertexBuffer = 0;
            }
            const uint32               batchIndex       = ( inoutState._pOrder != nullptr ) ? ( *inoutState._pOrder )[orderIndex] : orderIndex;
            const GPUMeshBatch&        head             = inoutState._batches[batchIndex];
            const GPUMeshBatchBinding& headBinding      = inoutState._bindings[batchIndex];
            const bool                 bHeadNotDrawable = headBinding.hasGeometry() == false || head.hasDrawSlots() == false || drawsBatchInPass( inoutState._passType, head, headBinding ) == false ||
                                          drawsCasterBatch( inoutState._casterFilter, head ) == false;
            if ( bHeadNotDrawable )
            {
                ++orderIndex;
                continue;
            }
            const uint32 groupCount = countDrawGroup( inoutState, drawMode, orderIndex, bDrawsParticles );
            // **이 패스의 뷰**가 만든 인자를 쓴다. 그림자 패스가 메인 카메라 인자를 쓰면 화면 밖에서
            // 화면 안으로 그림자를 드리우는 물체가 사라진다.
            if ( drawBatchGroup( context, inoutState, drawMode, head, headBinding, inoutState._batchOffset + batchIndex, groupCount ) )
                ++inoutState._drawCallCount;
            orderIndex += groupCount;
        }
    }

    void FramePassExecutor::drawGPUBatches( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass,
                                            const vector<GPUTransparentParticleSlot>* pParticleSlot )
    {
        SW_PROFILE_SCOPE( "RT.Draw.gpuBatches" );

        // 패스 PSO 가 없으면(셋업이 만들지 못하고 알렸다) 앞서 묶인 파이프라인으로 대신 그리지 않는다.
        const bool bCannotDraw = _pDevice == nullptr || context._pCmd == nullptr || _state._gpuScene.isUploaded() == false || pso == 0;
        if ( bCannotDraw )
            return;

        context._pCmd->setPipelineState( pso );

        setIdentityWorld( context );
        commitBindlessTextureBindings( context );

        const bool bInstanced = _pDevice->supportsInstancedSceneDraw() &&
                                _state._gpuScene.getInstanceSRV() != kInvalidDescriptorIndex;
        if ( bInstanced )
            registerInstanceBuffer( context );

        BatchDrawState state{};
        state._pso          = pso;
        state._boundPSO     = pso;
        state._casterFilter = context._shadowCasterFilter;
        state._batches      = bTransparentPass ? _state._gpuScene.getTransparentBatches() : _state._gpuScene.getOpaqueBatches();
        state._bindings     = bTransparentPass ? _state._gpuScene.getTransparentBatchBindings() : _state._gpuScene.getOpaqueBatchBindings();
        SW_ASSERT( state._bindings.size() == state._batches.size() );
        // 시간만 보면 무엇이 비싼지 알 수 없다. 배치가 몇 개로 묶였는지가 해석의 전제다.
        SW_PROFILE_COUNT( "RT.Draw.gpuBatchCount", state._batches.size() );

        // 간접 인자 슬롯은 업로드 순서대로 불투명 다음 투명으로 놓인다.
        state._batchOffset = bTransparentPass ? static_cast<uint32>( _state._gpuScene.getOpaqueBatches().size() ) : 0u;

        // **같은 PSO · 정점 버퍼 · 머티리얼(버퍼 · CB · 텍스처 · 원소 수)의 연속 배치는 drawIndexedIndirect 한 번(멀티 드로우)이다.** 배치마다
        // 다른 값은 배치 표(g_SwBatches)와 인스턴스 슬롯 스트림(슬롯 1)이 준다. 간접 인자의 startInstance 가 배치 시작이라 입력
        // 어셈블러가 인스턴스마다 자기 전역 자리를 넘기고, 정점은 그 인스턴스의 meshBatchIndex 로 표를 읽는다. 드로우 ID 도 루트
        // 상수 주입도 없다(DX12 커맨드 시그니처에 루트 상수를 넣으면 ExecuteIndirect 가 두 배 느려진다. 실측이다). 멀티 드로우가 없는
        // 백엔드(DX11)는 하나씩 부른다. 드로우 루프의 비용은 호출 수라, 배치가 많은 씬에서는 RT 프레임의 큰 몫이 된다.
        state._bMerge = isDrawMergeEnabled() && _pDevice->getCapabilities()._bMultiDrawIndirect != SW_FALSE;
        // 패스가 머티리얼로 배치를 거르면(메시 외곽선 — 외곽선을 켠 머티리얼만) 그 밖의 배치는 그리지도 묶지도 않는다.
        state._passType = context._passType;

        state._argsBuffer = _state._gpuScene.getCullView( context._cullViewIndex )._indirectArgs._buffer;
        // 추가 뷰의 투명 패스는 그 뷰의 순서로 배치를 돈다(GPUViewTransparentOrder). 묶기는 그 순서에서 이웃이고 번호도 이어진 것만 — 간접 인자는 번호 순으로 놓여 있다.
        const bool bExtraView     = isRenderingExtraView();
        const bool bUsesViewOrder = bTransparentPass && bExtraView && _pActiveView->_listTransparentBatchOrder.size() == state._batches.size();
        if ( bUsesViewOrder )
            state._pOrder = &_pActiveView->_listTransparentBatchOrder;
        // 인스턴스 슬롯 스트림(슬롯 1). 씬 드로우는 모두 이 스트림에서 자기 자리를 읽는다 — PSO 를 걸 때마다 메시 스트림과 함께 다시 건다(drawBatchGroup).
        // 컬링 없는 백엔드의 추가 뷰는 꼬리를 그 뷰 순서로 다시 놓은 자기 스트림을 쓴다(GPU 정렬 백엔드는 늘 공용 스트림).
        state._slotStream = ( bExtraView && _pActiveView->_bUsesViewSlotStream == SW_TRUE ) ? _pActiveView->_instanceSlotStream : _state._gpuScene.getInstanceSlotStream();
        // 투명 큐의 입자는 배치 순서의 자기 자리(`GPUTransparentParticleSlot::_drawPosition`) 앞에 끼운다. 입자 그리기가 PSO 를 바꾸므로
        // 메시로 돌아올 때 PSO 와 정점 스트림을 다시 건다(drawBatchRound 가 걸린 PSO 를 잊고, `drawParticleRun` 이 에뮬 텍스처 슬롯을 다시 건다).
        if ( pParticleSlot != nullptr )
            state._listParticleSlot = vector_reference<const GPUTransparentParticleSlot>( *pParticleSlot );

        // 겹침 모드(ShadedWireframe)는 배치를 두 번 그린다 — Lit PSO 로 한 번, 그 모드의 PSO(선 · 깊이 바이어스)로 한 번. 입자는 첫 번째에만 끼운다.
        // 보기 모드는 지금 그리는 뷰의 설정이다(씬 뷰만 ShadedWireframe 이고 게임 뷰는 Lit 일 수 있다).
        const RenderViewMode viewMode       = _pActiveView->_settings._viewMode;
        const bool           bOverlayDraw   = getRenderViewModeInfo( viewMode )._bOverlay && FrameRendererUtil::appliesViewMode( state._passType );
        const RenderViewMode arrDrawMode[]  = { bOverlayDraw ? RenderViewMode::Lit : viewMode, viewMode };
        const uint32         drawRoundCount = bOverlayDraw ? 2u : 1u;
        for ( uint32 drawRound = 0; drawRound < drawRoundCount; ++drawRound )
        {
            drawBatchRound( context, state, arrDrawMode[drawRound], drawRound == 0 );
        }
        // 모든 배치보다 가까운 입자(자리 = 배치 수)를 마저 그린다.
        (void)drawParticleRun( context, state._listParticleSlot, state._particleSlotCursor, MathUtil::kMaxUInt32 );
        SW_PROFILE_COUNT( "RT.Draw.indirectCalls", state._drawCallCount );
        _indirectDrawCallCount.fetch_add( state._drawCallCount, std::memory_order_relaxed );
    }

    bool FramePassExecutor::layoutBindsMaterialCb( RHIPipelineStateHandle batchPSO ) const
    {
        // 머티리얼 CB 는 그 슬롯을 실제로 거는 셰이더에서만 병합 키다(`GPUMeshBatchBinding::canShareMaterialBinding`).
        const ShaderBindingLayout* pLayout = _psoManager.findLayout( batchPSO );
        if ( pLayout == nullptr )
            return false;
        static const hashed_string s_materialCbName{ shaderslot::cbname::kMaterial };
        for ( const ShaderBindingSlot& slot : pLayout->getSlots() )
        {
            if ( slot._name == s_materialCbName )
                return true;
        }
        return false;
    }

    bool FramePassExecutor::isSameDrawGroup( RHIPipelineStateHandle pso, RenderPassType passType, RenderViewMode drawMode, const GPUMeshBatch& head,
                                             const GPUMeshBatchBinding& headBinding, const GPUMeshBatch& other, const GPUMeshBatchBinding& otherBinding ) const
    {
        const bool bOtherBatchNotDrawable = otherBinding.hasGeometry() == false || other.hasDrawSlots() == false || drawsBatchInPass( passType, other, otherBinding ) == false;
        if ( bOtherBatchNotDrawable )
            return false;
        // 정점 · 인덱스 버퍼는 짝이다(풀의 둘, 아니면 메시의 둘) — 정점 버퍼가 같으면 인덱스 버퍼도 같다.
        if ( otherBinding._vertexBuffer != headBinding._vertexBuffer || _psoManager.psoForBatch( pso, other, drawMode ) != _psoManager.psoForBatch( pso, head, drawMode ) )
            return false;
        // 레이아웃을 보는 것은 CB 가 다를 때만(대부분 같다).
        const bool bCbDiffers = otherBinding._materialCb != headBinding._materialCb;
        if ( bCbDiffers )
            SW_PROFILE_COUNT( "RT.PSO.batchLookups", 1 ); // W-R2 계측: 배치를 만들 때 PSO 를 한 번 풀어 두면 없어질 조회 수(`RT.Draw.gpuBatches` · `RT.PSO.ensureMaterial` 와 함께 본다)
        return GPUMeshBatchBinding::canShareMaterialBinding( headBinding, otherBinding, bCbDiffers && layoutBindsMaterialCb( _psoManager.psoForBatch( pso, head, drawMode ) ) );
    }

    bool FramePassExecutor::drawBatchGroup( FramePassContext& context, BatchDrawState& inoutState, RenderViewMode drawMode, const GPUMeshBatch& head,
                                            const GPUMeshBatchBinding& headBinding, uint32 argIndex, uint32 groupCount )
    {
        // **이 머티리얼의 퍼뮤테이션**으로 그린다. 주의: 패스 PSO 하나로 모두 그리면 머티리얼이 선언한
        // 정적 스위치(유리의 MATERIAL_BLEND_TRANSLUCENT 같은)가 쿠킹되기만 하고 한 번도 걸리지 않는다.
        // 캐시는 ensureMaterialPSOs 가 기록 전에 채운다. 여기서는 조회만 한다.
        RenderPSOCache::MaterialPSOState psoState = RenderPSOCache::MaterialPSOState::Ready;
        SW_PROFILE_COUNT( "RT.PSO.batchLookups", 1 );
        const RHIPipelineStateHandle batchPSO = _psoManager.psoForBatch( inoutState._pso, head, drawMode, &psoState );
        // 변형이 `Ready` 가 아닌 묶음(0)은 그리지 않는다 — 앞 묶음의 PSO 로 그리면 틀린 그림이다. 실패는 만들 때 오류를 남겼고, 만드는 중이면 이번 프레임만 건너뛴다.
        if ( batchPSO == 0 )
        {
            if ( psoState == RenderPSOCache::MaterialPSOState::Building )
            {
                SW_PROFILE_COUNT( "RT.PSO.pendingBatches", 1 );
                _pendingPSOBatchCount.fetch_add( 1, std::memory_order_relaxed );
            }
            return false;
        }
        if ( batchPSO != inoutState._boundPSO )
        {
            context._pCmd->setPipelineState( batchPSO ); // 정점 슬롯을 비운다 — 스트림은 아래에서 다시 건다
            inoutState._boundPSO          = batchPSO;
            inoutState._boundVertexBuffer = 0;
            if ( inoutState._slotStream != 0 )
                context._pCmd->setVertexBuffer( constant::render::kInstanceSlotStreamSlot, inoutState._slotStream, constant::render::kInstanceSlotStreamStride, 0 );
        }
        // 정점 · 인덱스 풀 하나라 PSO 가 그대로면 다시 걸지 않는다. 풀 밖 메시(예산 초과)만 자기 버퍼를 건다. 둘은 짝이라 정점 버퍼로만 바뀜을 본다.
        if ( headBinding._vertexBuffer != inoutState._boundVertexBuffer )
        {
            RHIMeshVertexStreams::bind( *context._pCmd, headBinding._vertexBuffer, headBinding._attributeOffset, head._mesh->getVertexFormat() );
            context._pCmd->setIndexBuffer( headBinding._indexBuffer, sizeof( uint32 ), 0 );
            inoutState._boundVertexBuffer = headBinding._vertexBuffer;
        }

        // 루트 상수 = { 머티리얼 원소 수 }. 그룹 안에서 같다.
        registerMaterialBuffer( context, headBinding, batchPSO );
        if ( bindForDraw( context, batchPSO, headBinding._materialCb, headBinding._arrMaterialTexSRV ) == false )
            return false;
        context._pCmd->drawIndexedIndirect( inoutState._argsBuffer, argIndex * static_cast<uint32>( sizeof( RHIDrawIndexedIndirectCommand ) ), groupCount );
        return true;
    }

    bool FramePassExecutor::drawParticleRun( FramePassContext& context, vector_reference<const GPUTransparentParticleSlot> listSlot, uint32& inOutCursor, uint32 position )
    {
        uint32 slotEnd = inOutCursor;
        while ( slotEnd < listSlot.size() && listSlot[slotEnd]._drawPosition <= position )
        {
            ++slotEnd;
        }
        if ( slotEnd == inOutCursor )
            return false;
        const vector_reference<const GPUTransparentParticleSlot> listRun = listSlot.subspan( inOutCursor, slotEnd - inOutCursor );
        inOutCursor                                                      = slotEnd;
        if ( drawParticleSlots( context, listRun ) == false )
            return false;
        // 메시 드로우가 읽는 패스 단위 상태를 되돌린다 — PSO 가 바뀌어 에뮬 텍스처 슬롯이 비었다(정점 스트림은 메시 묶음이 PSO 를 걸며 다시 건다).
        commitBindlessTextureBindings( context );
        return true;
    }

    bool FramePassExecutor::drawParticleSlots( FramePassContext& context, vector_reference<const GPUTransparentParticleSlot> listSlot )
    {
        const vector<GPUParticleBatch>& listBatch   = _state._gpuScene.getParticleBatches();
        const RHIPipelineStateHandle    pso         = _psoManager.findParticlePSO();
        const bool                      bCannotDraw = _pDevice == nullptr || context._pCmd == nullptr || listBatch.empty() || listSlot.empty() || pso == 0;
        if ( bCannotDraw )
            return false;
        // 정점은 셰이더가 SV_VertexID 로 만든다 — PSO 의 입력 레이아웃이 비어 정점 버퍼를 걸지 않는다.
        context._pCmd->setPipelineState( pso );
        setIdentityWorld( context );
        // PassCB(g_ViewProj · g_InvViewProj)를 건다. 입자 버퍼 · 텍스처 · 루트 상수는 그 뒤에 입자 렌더러가 건다.
        if ( bindForDraw( context, pso, kInvalidDescriptorIndex ) == false )
            return false;
        (void)_state._particleRenderer.drawBatches( *context._pCmd, listBatch, listSlot, _pDevice->supportsNativeBindlessSampling() );
        return true;
    }

    void FramePassExecutor::drawLight2DCookieAtlas()
    {
        const vector<Light2DCookieAtlas::CellDraw>& listDraw       = _state._cookieAtlas.getPendingDraws();
        const Texture2D*                            pAtlas         = _state._cookieAtlas.findAtlas();
        const bool                                  bNothingToDraw = listDraw.empty() || pAtlas == nullptr || _pDevice == nullptr || _pCmd == nullptr;
        if ( bNothingToDraw )
        {
            _state._cookieAtlas.clearPendingDraws();
            return;
        }

        // 칸에 옮겨 그리는 PSO(전체 화면 블릿 셰이더, 아틀라스 포맷)는 PSO 관리자가 저장소에 둔다 — 디바이스를 바꾸면 저장소가 비고 다시 만든다.
        const RHIPipelineStateHandle pso = _psoManager.findOrCreateCookieAtlasPSO( pAtlas->getFormat() );
        if ( pso == 0 )
        {
            _state._cookieAtlas.clearPendingDraws();
            return;
        }

        // 상태 전이는 렌더 패스 밖에서 — 쿠키는 읽기, 아틀라스는 그리기.
        for ( const Light2DCookieAtlas::CellDraw& draw : listDraw )
        {
            _pCmd->prepareTextureForShaderRead( draw._pTexture->getHandle() );
        }
        _pCmd->prepareTextureForRenderTarget( pAtlas->getHandle() );

        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor        = SW_TRUE;
        beginInfo._colorTargetCount  = 1;
        beginInfo._arrColorTarget[0] = pAtlas->getHandle();
        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Load; // 다른 칸은 지난 프레임에 그린 그대로다
        beginInfo._depthTarget       = 0;
        beginInfo._depthLoadOp       = RHIRenderPassLoadOp::DontCare;
        beginInfo._width             = shaderslot::kLight2DCookieAtlasSize;
        beginInfo._height            = shaderslot::kLight2DCookieAtlasSize;
        _pCmd->beginRenderPass( beginInfo );

        // 칸마다 뷰포트를 그 칸으로 잡고 쿠키를 원본(SourceColor)으로 걸어 블릿한다. 원본이 칸마다 달라 상수 슬롯도 칸마다 새로 잡는다(그림자 타일과 같은 이유).
        FramePassContext context = _state._frameContext;
        context._pCmd            = _pCmd;
        for ( const Light2DCookieAtlas::CellDraw& draw : listDraw )
        {
            uint32 cellX{ 0 };
            uint32 cellY{ 0 };
            uint32 cellSize{ 0 };
            Light2DCookieAtlas::computeCellRect( draw._cell, cellX, cellY, cellSize );
            RHIViewport viewport{};
            viewport._x      = static_cast<float32>( cellX );
            viewport._y      = static_cast<float32>( cellY );
            viewport._width  = static_cast<float32>( cellSize );
            viewport._height = static_cast<float32>( cellSize );
            _pCmd->setViewport( viewport );
            context._resourceRegistry.reset();
            context._resourceRegistry.registerTexture( attachmentNames()._sourceColor, draw._pTexture->getHandle(), draw._pTexture->getSRV() );
            if ( acquirePassCb( context ) == false ) // 링 고갈 — 이 쿠키 칸은 그리지 않는다
                continue;
            drawFullscreen( context, pso );
        }
        _pCmd->endRenderPass();
        _pCmd->prepareTextureForShaderRead( pAtlas->getHandle() );
        _state._cookieAtlas.clearPendingDraws();
    }

    void FramePassExecutor::drawTextureLayerBakes()
    {
        if ( _pDevice == nullptr || _pCmd == nullptr || engine::areEngineServicesBound() == false )
            return;
        TextureCache&                   textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
        vector<TextureLayerBakeRequest> listRequest;
        textures.takeLayerBakes( listRequest );
        if ( listRequest.empty() )
            return;
        SW_PROFILE_SCOPE( "RT.LayerBake" );
        const bool bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        // 텍스처 칸을 루트 상수로 채운다 — 네이티브 bindless 는 전역 번호, 에뮬 백엔드는 머티리얼 칸 t16.. 에 서수로 걸고 그 서수(캔버스와 같은 길).
        const auto bindTexture = [this, bNativeBindless]( const Texture2D* pTexture, uint32 slot, uint32& outRoot )
        {
            if ( pTexture == nullptr )
            {
                outRoot = constant::invalid_index::kUint32;
                return;
            }
            if ( bNativeBindless )
            {
                outRoot = pTexture->getSRV();
                return;
            }
            _pCmd->bindShaderResource( pTexture->getSRV(), shaderslot::kMaterialTexture0 + slot );
            outRoot = slot;
        };
        const auto findReady = [&textures]( const string& path ) -> const Texture2D*
        {
            const Texture2D* pTexture = path.empty() ? nullptr : textures.find( path );
            return pTexture != nullptr && pTexture->isRHIValid() ? pTexture : nullptr;
        };

        for ( TextureLayerBakeRequest& request : listRequest )
        {
            if ( textures.find( request._albedoTarget ) == nullptr )
                continue; // 요청한 쪽이 결과를 놓았다 — 그릴 곳이 없다
            // [0] 작업 타깃 · [1] 결과 타깃, 각 (알베도, 표면).
            const Texture2D* arrTarget[2][2] = {
                {findReady( request._albedoScratch ), findReady( request._surfaceScratch )},
                { findReady( request._albedoTarget ),  findReady( request._surfaceTarget )}
            };
            const Texture2D* pBase        = findReady( request._base );
            bool             bInputsReady = pBase != nullptr && arrTarget[1][0] != nullptr && arrTarget[1][1] != nullptr;
            bInputsReady                  = bInputsReady && ( request.getDrawCount() < 2u || ( arrTarget[0][0] != nullptr && arrTarget[0][1] != nullptr ) );
            for ( const TextureLayerBakeLayer& layer : request._listLayer )
            {
                bInputsReady = bInputsReady && findReady( layer._mask ) != nullptr && ( layer._smear.empty() || findReady( layer._smear ) != nullptr );
            }
            if ( bInputsReady == false )
            {
                textures.deferLayerBake( std::move( request ) ); // 읽는 중이거나 디바이스 교체로 다시 만드는 중 — 다음 프레임에 다시
                continue;
            }
            const RHIPipelineStateHandle pso = _psoManager.findOrCreateLayerBakePSO( arrTarget[1][0]->getFormat() );
            if ( pso == 0 )
                continue; // 못 만든 까닭은 PSO 관리자가 남겼다

            const uint32     width          = arrTarget[1][0]->getWidth();
            const uint32     height         = arrTarget[1][0]->getHeight();
            const Texture2D* pSourceAlbedo  = pBase;
            const Texture2D* pSourceSurface = nullptr;
            for ( uint32 drawIndex = 0; drawIndex < request.getDrawCount(); ++drawIndex )
            {
                const uint32                 targetSet = request.writesResult( drawIndex ) ? 1u : 0u;
                const TextureLayerBakeLayer* pLayer    = request._listLayer.empty() ? nullptr : &request._listLayer[drawIndex];
                const Texture2D*             pMask     = pLayer != nullptr ? findReady( pLayer->_mask ) : nullptr;
                const Texture2D*             pSmear    = pLayer != nullptr ? findReady( pLayer->_smear ) : nullptr;
                // 상태 전이는 렌더 패스 밖에서 — 읽는 것은 읽기, 이번 타깃은 그리기.
                const Texture2D* const arrRead[TextureLayerBakeConstants::kTextureCount] = { pSourceAlbedo, pSourceSurface, pMask, pSmear };
                for ( const Texture2D* pRead : arrRead )
                {
                    if ( pRead != nullptr )
                        _pCmd->prepareTextureForShaderRead( pRead->getHandle() );
                }
                _pCmd->prepareTextureForRenderTarget( arrTarget[targetSet][0]->getHandle() );
                _pCmd->prepareTextureForRenderTarget( arrTarget[targetSet][1]->getHandle() );

                RHIRenderPassBeginInfo beginInfo{};
                beginInfo._bBindColor        = SW_TRUE;
                beginInfo._colorTargetCount  = 2;
                beginInfo._arrColorTarget[0] = arrTarget[targetSet][0]->getHandle();
                beginInfo._arrColorTarget[1] = arrTarget[targetSet][1]->getHandle();
                beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::DontCare; // 전체 화면이 모든 픽셀을 덮는다
                beginInfo._arrLoadOp[1]      = RHIRenderPassLoadOp::DontCare;
                beginInfo._depthTarget       = 0;
                beginInfo._depthLoadOp       = RHIRenderPassLoadOp::DontCare;
                beginInfo._width             = width;
                beginInfo._height            = height;
                _pCmd->beginRenderPass( beginInfo );
                RHIViewport viewport{};
                viewport._width  = static_cast<float32>( width );
                viewport._height = static_cast<float32>( height );
                _pCmd->setViewport( viewport );
                _pCmd->setScissorRect( RHIScissorRect{ 0, 0, width, height } );
                _pCmd->setPipelineState( pso );
                // setPipelineState 가 슬롯 상태를 비운다 — 텍스처는 PSO 뒤에 건다.
                TextureLayerBakeConstants constants{};
                if ( pLayer != nullptr )
                    constants.packLayer( *pLayer );
                for ( uint32 slot = 0; slot < TextureLayerBakeConstants::kTextureCount; ++slot )
                {
                    bindTexture( arrRead[slot], slot, constants._arrDword[TextureLayerBakeConstants::kRootSourceAlbedo + slot] );
                }
                _pCmd->setGraphicsRootConstants( 0, TextureLayerBakeConstants::kRootDwordCount, constants._arrDword );
                _pCmd->draw( 3, 0 );
                _pCmd->endRenderPass();
                pSourceAlbedo  = arrTarget[targetSet][0];
                pSourceSurface = arrTarget[targetSet][1];
            }
            // 결과는 머티리얼이 읽는다 — 읽기 상태로 둔다.
            _pCmd->prepareTextureForShaderRead( arrTarget[1][0]->getHandle() );
            _pCmd->prepareTextureForShaderRead( arrTarget[1][1]->getHandle() );
            textures.recordLayerBaked( request._albedoTarget );
        }
    }

    void FramePassExecutor::drawFullscreen( FramePassContext& context, RHIPipelineStateHandle pso )
    {
        // PSO 가 없으면 그리지 않는다 — 앞서 묶인 파이프라인이나 백엔드 기본 파이프라인으로 대신 그리지 않는다(없는 이유는 셋업이 알렸다).
        if ( context._pCmd == nullptr || pso == 0 )
            return;
        setIdentityWorld( context );
        commitBindlessTextureBindings( context );
        context._pCmd->setPipelineState( pso );
        if ( bindForDraw( context, pso, kInvalidDescriptorIndex ) )
            context._pCmd->draw( 3, 0 );
    }
} // namespace sw
