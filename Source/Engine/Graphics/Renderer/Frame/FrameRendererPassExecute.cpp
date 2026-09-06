#include "pch.h"

#include "Core/Profile/FrameProfiler.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    void FrameRenderer::bindPassCallbacks()
    {
        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        _graph.clear();
        _mapPassNameToIndex.clear();

        for ( uint32 index = 0; index < static_cast<uint32>( listPass.size() ); ++index )
        {
            const RenderGraphPassDesc& pass = listPass[index];
            const hashed_string        nameHash( pass._name.c_str() );
            _mapPassNameToIndex[nameHash] = index;

            vector<hashed_string> listInput;
            vector<hashed_string> listOutput;
            for ( const string& in : pass._listInput )
            {
                listInput.emplace_back( in.c_str() );
            }
            for ( const string& out : pass._listOutput )
            {
                listOutput.emplace_back( out.c_str() );
            }

            RenderGraphPassExecuteFn execute =
                SW_DELEGATE_METHOD( RenderGraphPassExecuteFn, &FrameRenderer::onGraphPassExecute, this );

            _graph.addPass( nameHash, std::move( listInput ), std::move( listOutput ), std::move( execute ) );
        }

        _graph.setWavePrologue( SW_DELEGATE_METHOD( RenderGraphWavePrologueFn, &FrameRenderer::onGraphWavePrologue, this ) );

        if ( _graph.compile() == false )
            SW_LOG_ERROR( "Callback bind compile failed" );
        else
            _bCallbacksBound = 1;
    }

    void FrameRenderer::onGraphWavePrologue( const RenderGraphWaveContext& waveCtx )
    {
        if ( _pDevice == nullptr || waveCtx._pCmdList == nullptr )
            return;

        // 이 웨이브가 읽을 것들 — 샘플링 가능 상태로.
        if ( waveCtx._pListReadResource != nullptr )
        {
            for ( const hashed_string& name : *waveCtx._pListReadResource )
            {
                const RHITextureHandle texture = findTransient( name.c_str() );
                if ( texture != 0 )
                    waveCtx._pCmdList->prepareTextureForShaderRead( texture );
            }
        }

        // 이 웨이브가 쓸 것들 — 렌더타깃(또는 뎁스) 상태로. 컬러/뎁스 구분은 백엔드가 한다.
        if ( waveCtx._pListWriteResource != nullptr )
        {
            for ( const hashed_string& name : *waveCtx._pListWriteResource )
            {
                // 스왑체인은 전용 경로가 있다(핸들 0). 이름으로는 트랜지언트에 없다.
                if ( name == attachmentNames()._swapchain )
                {
                    waveCtx._pCmdList->prepareTextureForRenderTarget( 0 );
                    continue;
                }
                const RHITextureHandle texture = findTransient( name.c_str() );
                if ( texture != 0 )
                    waveCtx._pCmdList->prepareTextureForRenderTarget( texture );
            }
        }
    }

    void FrameRenderer::onGraphPassExecute( const RenderGraphPassContext& graphCtx )
    {
        if ( _pDevice == nullptr )
            return;

        // 패스 로컬 상태를 새로 만든다. 예전에는 멤버 _pCmd 를 저장/복원했는데, 그건
        // "한 번에 한 패스만 돈다" 는 전제라 병렬 기록에서 서로를 덮어썼다.
        // 프레임 시드에서 복사해 뷰/조명 등 프레임 공통값을 물려받는다.
        FramePassContext passCtx = _frameCtx;
        if ( graphCtx._pCmdList != nullptr )
            passCtx._pCmd = graphCtx._pCmdList;
        // 상수 버퍼도 패스마다 따로 잡는다. 커맨드 기록은 지연인데 상수 쓰기는 즉시라,
        // 하나를 공유하면 재생 시점에 마지막 패스 값만 남는다.
        acquirePassCb( passCtx );

        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        // 타입은 로드 시점에 한 번 해석해 둔 값을 쓴다 — 여기서 문자열을 다시 비교하면 디스패치와
        // PSO 생성이 서로 다른 표기를 받아줄 여지가 생긴다(그게 `ae7fb078` 의 원인이었다).
        RenderPassType passType  = RenderPassType::Invalid;
        const utf8*    pPassName = graphCtx._passName.c_str() != nullptr ? graphCtx._passName.c_str() : "";
        hashed_string  depthAttachment;
        const auto     iter = _mapPassNameToIndex.find( graphCtx._passName );
        if ( iter != _mapPassNameToIndex.end() && iter->second < listPass.size() )
        {
            const RenderGraphPassDesc& pass = listPass[iter->second];
            passType                        = pass._resolvedType;
            pPassName                       = pass._name.c_str();
            depthAttachment                 = pass._resolvedDepthAttachment;
        }
        executePass( passCtx, passType, pPassName, depthAttachment );
    }

    void FrameRenderer::executePass( FramePassContext& ctx, RenderPassType passType, string_view passName, const hashed_string& depthAttachment )
    {
        SW_PROFILE_SCOPE( "RT.Pass.execute" );

        if ( ctx._pCmd == nullptr )
        {
            SW_LOG_ERROR( "executePass: no active IRHICommandList" );
            return;
        }

        if ( passName.empty() == false )
        {
            utf8         arrPassName[constant::kMaxBuffer64];
            const size_t copyLen = ( passName.size() < sizeof( arrPassName ) - 1 ) ? passName.size() : ( sizeof( arrPassName ) - 1 );
            Memory::copy( arrPassName, passName.data(), copyLen );
            arrPassName[copyLen] = '\0';
            ctx._pCmd->beginEventMarker( arrPassName );
        }
        ctx._resourceRegistry.reset();

        // 이름은 **이미 intern 된 것**만 받는다. string_view 를 받던 시절엔 패스마다 여기서
        // 다시 intern 했다(FNV + 32-way 샤드 뮤텍스). 타깃 이름은 전부 코드 리터럴이라
        // attachmentNames() 캐시로 충분하다.
        auto colorLoadFor = [this]( const hashed_string& name, bool bForceLoad ) -> RHIRenderPassLoadOp
        {
            if ( bForceLoad )
                return RHIRenderPassLoadOp::Load;
            return markAttachmentCleared( name ) ? RHIRenderPassLoadOp::Clear : RHIRenderPassLoadOp::Load;
        };

        // b0 에 들어갈 패스 상수. 머티리얼 CB 로 폴백하면 안 된다 — 레이아웃이 다르다.
        // 머티리얼 상수는 드로우마다 메시/배치에서 직접 넘긴다.
        const RHIDescriptorIndex passCb = ctx._passCbIndex;

        auto executeFullscreenPass = [&]( RHIPipelineStateHandle pso, const hashed_string& targetName, const float4& passClearColor )
        {
            beginColorPass( ctx, targetName.view(), "", passClearColor, colorLoadFor( targetName, false ), RHIRenderPassLoadOp::Load );
            drawFullscreen( ctx, pso, passCb );
            ctx._pCmd->endRenderPass();
        };

        // 이 패스가 바인딩할 뎁스. 파이프라인 XML 의 `_depthAttachment` 가 정본이고, 비어 있으면
        // 뎁스 없이 연다 — "이 패스는 일부러 뎁스를 안 쓴다" 를 선언으로 표현할 수 있어야 한다.
        // 선언한 이름이 이번 프레임에 실제로 없으면(트랜지언트 미할당) 뎁스 없이 진행한다.
        const hashed_string passDepth = ( depthAttachment.empty() == false && findTransient( depthAttachment.view() ) != 0 )
                                          ? depthAttachment
                                          : hashed_string{};

        if ( passType == RenderPassType::Shadow )
        {
            const float4 clearVal = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kShadowMap, float4{ 1.0f, 0.0f, 0.0f, 0.0f } );
            beginDepthOnlyPass( ctx, passDepth.view(), clearVal._x, colorLoadFor( passDepth, false ) );
            drawSceneMeshes( ctx, getEnginePso( RenderPassType::Shadow ), passCb, false );
            ctx._pCmd->endRenderPass();
        }
        else if ( passType == RenderPassType::DepthPrepass )
        {
            const float4 clearVal = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kSceneDepth, float4{ 1.0f, 0.0f, 0.0f, 0.0f } );
            beginDepthOnlyPass( ctx, passDepth.view(), clearVal._x, colorLoadFor( passDepth, false ) );
            const RHIPipelineStateHandle depthPso = getEnginePso( RenderPassType::DepthPrepass ) != 0
                                                      ? getEnginePso( RenderPassType::DepthPrepass )
                                                      : getEnginePso( RenderPassType::Shadow );
            drawSceneMeshes( ctx, depthPso, passCb, false );
            ctx._pCmd->endRenderPass();
            _bHasExecutedDepthPrepass.store( 1 );
        }
        else if ( passType == RenderPassType::ForwardOpaque )
        {
            registerPassTexture( ctx, attachmentNames()._shadowMap, FrameRendererUtil::Attachment::kShadowMap );
            const float4           sceneClear = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kSceneColor, _clearColor );
            const AttachmentNames& names      = attachmentNames();
            beginColorPass( ctx, names._sceneColor.view(), passDepth.view(), sceneClear,
                            colorLoadFor( names._sceneColor, false ), colorLoadFor( names._sceneDepth, false ) );

            const RHIPipelineStateHandle psoForward = ( _bHasExecutedDepthPrepass.load() != 0 && getEnginePso( RenderPassType::ForwardOpaqueNoDepthWrite ) != 0 )
                                                        ? getEnginePso( RenderPassType::ForwardOpaqueNoDepthWrite )
                                                        : getEnginePso( RenderPassType::ForwardOpaque );
            drawSceneMeshes( ctx, psoForward, passCb, false );
            ctx._pCmd->endRenderPass();
        }
        else if ( passType == RenderPassType::GBuffer )
        {
            const float4 clearColor = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kGBufferAlbedo, float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
            const bool   bHasNormal = findTransient( FrameRendererUtil::Attachment::kGBufferNormal ) != 0;
            const bool   bUseMrt    = bHasNormal && _pDevice->supportsMultiRenderTarget() &&
                                      getEnginePso( RenderPassType::GBuffer ) != 0;
            if ( bUseMrt )
            {
                const float4              normalClear  = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kGBufferNormal, FrameRendererUtil::kNormalClear );
                const string_view         arrNames[]   = { FrameRendererUtil::Attachment::kGBufferAlbedo, FrameRendererUtil::Attachment::kGBufferNormal };
                const float4              arrClears[2] = { clearColor, normalClear };
                const RHIRenderPassLoadOp arrLoads[]   = { colorLoadFor( attachmentNames()._gbufferAlbedo, false ),
                                                           colorLoadFor( attachmentNames()._gbufferNormal, false ) };
                beginColorPassMRT( ctx, arrNames, arrClears, arrLoads, 2, passDepth.view(), colorLoadFor( passDepth, false ) );
                drawSceneMeshes( ctx, getEnginePso( RenderPassType::GBuffer ), passCb, false );
                ctx._pCmd->endRenderPass();
            }
            else
            {
                const RHIPipelineStateHandle albedoPso =
                    getEnginePso( RenderPassType::GBufferAlbedo ) != 0
                        ? getEnginePso( RenderPassType::GBufferAlbedo )
                        : getEnginePso( RenderPassType::GBuffer );
                beginColorPass( ctx, attachmentNames()._gbufferAlbedo.view(), passDepth.view(), clearColor,
                                colorLoadFor( attachmentNames()._gbufferAlbedo, false ), colorLoadFor( attachmentNames()._sceneDepth, false ) );
                drawSceneMeshes( ctx, albedoPso != 0 ? albedoPso : 0, passCb, false );
                ctx._pCmd->endRenderPass();

                if ( bHasNormal )
                {
                    const float4 normalClear = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kGBufferNormal, FrameRendererUtil::kNormalClear );
                    beginColorPass( ctx, attachmentNames()._gbufferNormal.view(), passDepth.view(), normalClear,
                                    colorLoadFor( attachmentNames()._gbufferNormal, false ), RHIRenderPassLoadOp::Load );
                    drawSceneMeshes( ctx, getEnginePso( RenderPassType::GBufferNormal ), passCb, false );
                    ctx._pCmd->endRenderPass();
                }
            }
        }
        else if ( passType == RenderPassType::Lighting )
        {
            registerPassTexture( ctx, attachmentNames()._gbufferAlbedo, FrameRendererUtil::Attachment::kGBufferAlbedo );
            registerPassTexture( ctx, attachmentNames()._gbufferNormal, FrameRendererUtil::Attachment::kGBufferNormal );
            registerPassTexture( ctx, attachmentNames()._sceneDepth, FrameRendererUtil::Attachment::kSceneDepth );
            registerPassTexture( ctx, attachmentNames()._shadowMap, FrameRendererUtil::Attachment::kShadowMap );
            const AttachmentNames& names     = attachmentNames();
            const hashed_string&   litTarget = findTransient( names._litColor.view() ) != 0 ? names._litColor : names._sceneColor;
            executeFullscreenPass( getEnginePso( RenderPassType::Lighting ), litTarget,
                                   getAttachmentClearColorOrDefault( litTarget.view(), _clearColor ) );
        }
        else if ( passType == RenderPassType::Transparent )
        {
            const AttachmentNames& names = attachmentNames();
            const hashed_string&   colorTarget =
                findTransient( names._transparentColor.view() ) != 0
                    ? names._transparentColor
                    : ( findTransient( names._litColor.view() ) != 0 ? names._litColor : names._sceneColor );

            if ( colorTarget == names._transparentColor )
            {
                const RHITextureHandle src = findTransient( FrameRendererUtil::Attachment::kLitColor ) != 0 ? findTransient( FrameRendererUtil::Attachment::kLitColor ) : findTransient( FrameRendererUtil::Attachment::kSceneColor );
                if ( src != 0 )
                    ctx._pCmd->blitTexture( src, findTransient( FrameRendererUtil::Attachment::kTransparentColor ) );
                markAttachmentCleared( attachmentNames()._transparentColor );
            }

            beginColorPass( ctx, colorTarget.view(), passDepth.view(), _clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load );
            const RHIPipelineStateHandle transparentPso =
                getEnginePso( RenderPassType::Transparent ) != 0
                    ? getEnginePso( RenderPassType::Transparent )
                    : getEnginePso( RenderPassType::ForwardOpaque );
            drawSceneMeshes( ctx, transparentPso, passCb, true );
            ctx._pCmd->endRenderPass();
        }
        else if ( passType == RenderPassType::SSAO )
        {
            const AttachmentNames& names    = attachmentNames();
            const hashed_string&   aoTarget = findTransient( names._aoColor.view() ) != 0 ? names._aoColor : names._sceneColor;
            registerPassTexture( ctx, attachmentNames()._sceneDepth, FrameRendererUtil::Attachment::kSceneDepth );
            registerPassTexture( ctx, attachmentNames()._gbufferNormal, FrameRendererUtil::Attachment::kGBufferNormal );
            const RHIPipelineStateHandle aoPso =
                getEnginePso( RenderPassType::SSAO ) != 0 ? getEnginePso( RenderPassType::SSAO ) : getEnginePso( RenderPassType::SSAO );
            executeFullscreenPass( aoPso, aoTarget, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
        }
        else if ( passType == RenderPassType::Bloom )
        {
            const AttachmentNames& names       = attachmentNames();
            const hashed_string&   bloomTarget = findTransient( names._bloomColor.view() ) != 0 ? names._bloomColor : names._sceneColor;
            const utf8*            pSrcName    = FrameRendererUtil::pickFirstExisting( _mapTransient, { "TransparentColor", "LitColor", "SceneColor", "GBufferAlbedo" } );
            if ( pSrcName != nullptr )
                registerPassTexture( ctx, attachmentNames()._sourceColor, pSrcName );
            executeFullscreenPass( getEnginePso( RenderPassType::Bloom ), bloomTarget,
                                   getAttachmentClearColorOrDefault( bloomTarget.view(), _clearColor ) );
        }
        else if ( passType == RenderPassType::Outline )
        {
            const AttachmentNames& names         = attachmentNames();
            const hashed_string&   outlineTarget = findTransient( names._outlineColor.view() ) != 0 ? names._outlineColor : names._sceneColor;
            const utf8*            pSrcName      = FrameRendererUtil::pickFirstExisting( _mapTransient, { "BloomColor", "TransparentColor", "LitColor", "SceneColor" } );
            if ( pSrcName != nullptr )
                registerPassTexture( ctx, attachmentNames()._sourceColor, pSrcName );
            registerPassTexture( ctx, attachmentNames()._sourceDepth, FrameRendererUtil::Attachment::kSceneDepth );
            executeFullscreenPass( getEnginePso( RenderPassType::Outline ), outlineTarget, _clearColor );
        }
        else if ( passType == RenderPassType::TAA )
        {
            const AttachmentNames& names     = attachmentNames();
            const hashed_string&   taaTarget = findTransient( names._taaColor.view() ) != 0 ? names._taaColor : names._sceneColor;
            const utf8*            pSrcName  = FrameRendererUtil::pickFirstExisting( _mapTransient, { "BloomColor", "OutlineColor", "TransparentColor", "LitColor", "SceneColor" } );
            if ( pSrcName != nullptr )
                registerPassTexture( ctx, attachmentNames()._sourceColor, pSrcName );
            // 히스토리 생성·bindless 등록은 ensureTaaHistory() 가 셋업 단계에서 끝냈다 — 이 콜백은
            // 병렬 기록에서 태스크 스레드가 돌리므로 여기서 레지스트리를 건드리면 안 된다.
            if ( _taaHistory != 0 )
                ctx._resourceRegistry.registerTexture( attachmentNames()._gbufferAlbedo, _taaHistory, _taaHistorySrv );
            beginColorPass( ctx, taaTarget.view(), "", _clearColor, colorLoadFor( taaTarget, false ), RHIRenderPassLoadOp::Load );
            const RHIPipelineStateHandle taaPso = getEnginePso( RenderPassType::TAA );
            if ( taaPso != 0 )
                drawFullscreen( ctx, taaPso, passCb );
            else if ( pSrcName != nullptr && taaTarget.view() != pSrcName )
                ctx._pCmd->blitTexture( findTransient( pSrcName ), findTransient( taaTarget.view() ) );
            ctx._pCmd->endRenderPass();

            const RHITextureHandle taaOut = findTransient( taaTarget.view() );
            if ( taaOut != 0 && _taaHistory != 0 )
                ctx._pCmd->blitTexture( taaOut, _taaHistory );
        }
        else if ( passType == RenderPassType::Tonemap )
        {
            const AttachmentNames& names = attachmentNames();
            const hashed_string&   tonemapTarget =
                findTransient( names._tonemapColor.view() ) != 0 ? names._tonemapColor : names._sceneColor;
            const utf8* pSrcName = FrameRendererUtil::pickFirstExisting( _mapTransient, { "TaaColor", "OutlineColor", "BloomColor", "TransparentColor", "LitColor", "SceneColor" } );
            if ( pSrcName != nullptr )
                registerPassTexture( ctx, attachmentNames()._sourceColor, pSrcName );
            const RHIPipelineStateHandle tonemapPso =
                getEnginePso( RenderPassType::Tonemap ) != 0 ? getEnginePso( RenderPassType::Tonemap ) : getEnginePso( RenderPassType::Present );
            executeFullscreenPass( tonemapPso, tonemapTarget, _clearColor );
        }
        else if ( passType == RenderPassType::Present )
        {
            const string                 srcName   = resolvePresentSource();
            const RHITextureHandle       src       = srcName.empty() ? 0 : findTransient( srcName );
            const RHITextureHandle       dstTarget = _outputRenderTarget;
            const RHIPipelineStateHandle psoBlit   = getEnginePso( RenderPassType::Present );
            if ( src != 0 && psoBlit != 0 )
            {
                registerPassTexture( ctx, attachmentNames()._sourceColor, srcName );
                RHIRenderPassBeginInfo beginInfo{};
                beginInfo._bBindColor        = 1;
                beginInfo._arrColorTarget[0] = dstTarget;
                beginInfo._colorTargetCount  = 1;
                beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::DontCare;
                beginInfo._width             = _transientWidth;
                beginInfo._height            = _transientHeight;
                ctx._pCmd->beginRenderPass( beginInfo );
                drawFullscreen( ctx, psoBlit, passCb );
                ctx._pCmd->endRenderPass();
            }
            else if ( src != 0 )
                ctx._pCmd->blitTexture( src, dstTarget );
            else
            {
                RHIRenderPassBeginInfo beginInfo{};
                beginInfo.setColorTarget( dstTarget, _clearColor, RHIRenderPassLoadOp::Load );
                beginInfo._bBindColor = 1;
                ctx._pCmd->beginRenderPass( beginInfo );
                drawFullscreen( ctx, 0, passCb );
                ctx._pCmd->endRenderPass();
            }
        }
        else
            SW_LOG_WARNING( "Unknown pass type '%#' in '%#'", passType, passName );

        ctx._pCmd->endEventMarker();
    }

    void FrameRenderer::beginColorPass( FramePassContext& ctx, string_view colorName, string_view depthName, const float4& clearColor,
                                        RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad )
    {
        const string_view         arrName[]  = { colorName };
        const float4              arrClear[] = { clearColor };
        const RHIRenderPassLoadOp arrLoad[]  = { colorLoad };
        beginColorPassMRT( ctx, arrName, arrClear, arrLoad, 1, depthName, depthLoad );
    }

    void FrameRenderer::beginColorPassMRT( FramePassContext& ctx, const string_view* pColorNames, const float4* pTargetClearColor, const RHIRenderPassLoadOp* pColorLoad,
                                           uint32 colorCount, string_view depthName, RHIRenderPassLoadOp depthLoad )
    {
        if ( _pDevice == nullptr || ctx._pCmd == nullptr || pColorNames == nullptr || colorCount == 0 )
            return;

        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor       = 1;
        beginInfo._depthLoadOp      = depthLoad;
        beginInfo._clearDepth       = 1.0f;
        beginInfo._depthTarget      = depthName.empty() ? 0 : findTransient( depthName );
        beginInfo._width            = _transientWidth;
        beginInfo._height           = _transientHeight;
        beginInfo._colorTargetCount = colorCount > kMaxColorAttachments ? kMaxColorAttachments : colorCount;
        for ( uint32 colorTargetIndex = 0; colorTargetIndex < beginInfo._colorTargetCount; ++colorTargetIndex )
        {
            beginInfo._arrColorTarget[colorTargetIndex] = findTransient( pColorNames[colorTargetIndex] );
            beginInfo._arrLoadOp[colorTargetIndex]      = pColorLoad != nullptr ? pColorLoad[colorTargetIndex] : RHIRenderPassLoadOp::Clear;
            if ( pTargetClearColor != nullptr )
                beginInfo._arrClearColor[colorTargetIndex] = pTargetClearColor[colorTargetIndex];
        }
        ctx._pCmd->beginRenderPass( beginInfo );
    }

    void FrameRenderer::beginDepthOnlyPass( FramePassContext& ctx, string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad )
    {
        if ( ctx._pCmd == nullptr )
            return;
        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor       = 0;
        beginInfo._colorTargetCount = 0;
        beginInfo._depthTarget      = findTransient( depthName );
        beginInfo._depthLoadOp      = depthLoad;
        beginInfo._clearDepth       = clearDepth;
        beginInfo._width            = _transientWidth;
        beginInfo._height           = _transientHeight;
        ctx._pCmd->beginRenderPass( beginInfo );
    }

    void FrameRenderer::registerPassTexture( FramePassContext& ctx, const hashed_string& canonicalName, string_view attachmentName )
    {
        const RHITextureHandle tex = findTransient( attachmentName );
        if ( tex != 0 && ctx._pCmd != nullptr )
            ctx._pCmd->prepareTextureForShaderRead( tex );
        const RHIDescriptorIndex srv = findTransientSrv( attachmentName );
        ctx._resourceRegistry.registerTexture( canonicalName, tex, srv );
    }

    void FrameRenderer::commitBindlessTextureBindings( FramePassContext& ctx )
    {
        if ( _pDevice == nullptr || ctx._pCmd == nullptr )
            return;

        // 예전엔 여기서 updatePassConstants 를 불렀다. 이 함수는 드로우 루프 안에서 호출되므로
        // 드로우마다 라이트/뷰 행렬을 다시 만들고(정규화·외적·4x4 곱 두 번) 카메라를 다시 찾고
        // hashed_string 을 여덟 개씩 intern 했다. 그 값들은 전부 프레임 상수라 execute/executePacket
        // 이 프레임 시드(_frameCtx)에 한 번만 채우면 되고, 패스 컨텍스트는 그 시드를 복사해 간다.
        // 드로우마다 바뀌는 건 g_World 하나뿐이고 그건 bindForDraw 가 넣는다.

        // DX11/GL: bind PassCB indices into t0..t3 (emulated bindless).
        // DX12/VK: shaders index the heap/array directly — 리플렉션 바인더가 g_<Name>Index 를 채운다.
        if ( _pDevice->supportsNativeBindlessSampling() )
            return;

        // 이 함수는 드로우 루프 안에서 불린다. 예전엔 여기서 이름 네 개를 **드로우마다** intern 했다
        // (FNV 해시 + 32-way 샤드 뮤텍스 x 4). 바로 위 주석이 같은 문제를 한 번 고쳤다고 적어 두었는데,
        // 정작 이 람다가 남아 있었다 — 이름을 문자열로 들고 다니는 한 계속 재발한다.
        auto srvOf = [&ctx]( const hashed_string& name ) -> RHIDescriptorIndex
        {
            const RegisteredTexture* pTex = ctx._resourceRegistry.findTexture( name );
            return pTex != nullptr ? pTex->_srv : kInvalidDescriptorIndex;
        };

        const AttachmentNames&   names       = attachmentNames();
        const RHIDescriptorIndex shadow      = srvOf( names._shadowMap );
        const RHIDescriptorIndex albedo      = srvOf( names._gbufferAlbedo );
        const RHIDescriptorIndex normal      = srvOf( names._gbufferNormal );
        const RHIDescriptorIndex depth       = srvOf( names._sceneDepth );
        const RHIDescriptorIndex source      = srvOf( names._sourceColor );
        const RHIDescriptorIndex sourceDepth = srvOf( names._sourceDepth );

        const RHIDescriptorIndex slot0 = ( shadow != kInvalidDescriptorIndex ) ? shadow : source;
        const RHIDescriptorIndex slot1 = ( albedo != kInvalidDescriptorIndex ) ? albedo : sourceDepth;
        const RHIDescriptorIndex slot2 = normal;
        const RHIDescriptorIndex slot3 = ( depth != kInvalidDescriptorIndex ) ? depth : shadow;

        if ( shadow != kInvalidDescriptorIndex || source != kInvalidDescriptorIndex )
            ctx._pCmd->bindShaderResource( slot0, 0 );
        if ( albedo != kInvalidDescriptorIndex || sourceDepth != kInvalidDescriptorIndex )
            ctx._pCmd->bindShaderResource( slot1, 1 );
        if ( normal != kInvalidDescriptorIndex )
            ctx._pCmd->bindShaderResource( slot2, 2 );
        if ( depth != kInvalidDescriptorIndex || shadow != kInvalidDescriptorIndex )
            ctx._pCmd->bindShaderResource( slot3, 3 );
    }

    RHIFormat FrameRenderer::attachmentFormatOrDefault( string_view attachmentName, RHIFormat fallback ) const
    {
        for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
        {
            if ( att._name == attachmentName )
                return parseAttachmentFormat( att._format );
        }
        return fallback;
    }

    const RenderGraphPassDesc* FrameRenderer::findPassDescByType( RenderPassType passType ) const
    {
        // 표기 흔들림은 로드 시점의 _resolvedType 이 이미 흡수했다 — 여기서는 값만 비교하면 된다.
        // 예전엔 문자열을 비교하느라 별칭(`Shading` vs `Lighting`)을 놓쳤다.
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._resolvedType == passType )
                return &pass;
        }
        return nullptr;
    }
} // namespace sw
