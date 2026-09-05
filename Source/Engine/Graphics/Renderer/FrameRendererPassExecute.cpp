#include "pch.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/FrameRenderer.h"
#include "Engine/Graphics/Renderer/FrameRendererUtil.h"

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

        if ( _graph.compile() == false )
            SW_LOG_ERROR( "Callback bind compile failed" );
        else
            _bCallbacksBound = 1;
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

        const vector<RenderGraphPassDesc>& listPass  = _pipelineResource.getGraphPass();
        const utf8*                        pPassType = "";
        const utf8*                        pPassName = graphCtx._passName.c_str() != nullptr ? graphCtx._passName.c_str() : "";
        const auto                         iter      = _mapPassNameToIndex.find( graphCtx._passName );
        if ( iter != _mapPassNameToIndex.end() && iter->second < listPass.size() )
        {
            const RenderGraphPassDesc& pass = listPass[iter->second];
            pPassType                       = pass._type.c_str();
            pPassName                       = pass._name.c_str();
        }
        executePass( passCtx, pPassType, pPassName );
    }

    void FrameRenderer::executePass( FramePassContext& ctx, string_view passType, string_view passName )
    {
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

        auto colorLoadFor = [this]( string_view name, bool bForceLoad ) -> RHIRenderPassLoadOp
        {
            if ( bForceLoad )
                return RHIRenderPassLoadOp::Load;
            const hashed_string key( name.data(), static_cast<uint32>( name.length() ) );
            return markAttachmentCleared( key ) ? RHIRenderPassLoadOp::Clear : RHIRenderPassLoadOp::Load;
        };

        // b0 에 들어갈 패스 상수. 머티리얼 CB 로 폴백하면 안 된다 — 레이아웃이 다르다.
        // 머티리얼 상수는 드로우마다 메시/배치에서 직접 넘긴다.
        const RHIDescriptorIndex passCb = ctx._passCbIndex;

        auto executeFullscreenPass = [&]( RHIPipelineStateHandle pso, string_view targetName, const float4& passClearColor )
        {
            beginColorPass( ctx, targetName, "", passClearColor, colorLoadFor( targetName, false ), RHIRenderPassLoadOp::Load );
            drawFullscreen( ctx, pso, passCb );
            ctx._pCmd->endRenderPass();
        };

        if ( passType == FrameRendererUtil::PassType::kShadow )
        {
            const float4 clearVal = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kShadowMap, float4{ 1.0f, 0.0f, 0.0f, 0.0f } );
            beginDepthOnlyPass( ctx, FrameRendererUtil::Attachment::kShadowMap, clearVal._x, colorLoadFor( FrameRendererUtil::Attachment::kShadowMap, false ) );
            drawSceneMeshes( ctx, getEnginePso( FrameRendererUtil::PassType::kShadow ), passCb, false );
            ctx._pCmd->endRenderPass();
        }
        else if ( passType == FrameRendererUtil::PassType::kDepthPrepass || passType == "DepthPrepass" || passType == "Depth" || passType == "PrePass" )
        {
            const float4 clearVal = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kSceneDepth, float4{ 1.0f, 0.0f, 0.0f, 0.0f } );
            beginDepthOnlyPass( ctx, FrameRendererUtil::Attachment::kSceneDepth, clearVal._x, colorLoadFor( FrameRendererUtil::Attachment::kSceneDepth, false ) );
            const RHIPipelineStateHandle depthPso = getEnginePso( FrameRendererUtil::PassType::kDepthPrepass ) != 0
                                                      ? getEnginePso( FrameRendererUtil::PassType::kDepthPrepass )
                                                      : getEnginePso( FrameRendererUtil::PassType::kShadow );
            drawSceneMeshes( ctx, depthPso, passCb, false );
            ctx._pCmd->endRenderPass();
            _bHasExecutedDepthPrepass.store( 1 );
        }
        else if ( passType == FrameRendererUtil::PassType::kForwardOpaque )
        {
            registerPassTexture( ctx, "ShadowMap", FrameRendererUtil::Attachment::kShadowMap );
            const float4 sceneClear = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kSceneColor, _clearColor );
            beginColorPass( ctx, FrameRendererUtil::Attachment::kSceneColor, FrameRendererUtil::Attachment::kSceneDepth, sceneClear,
                            colorLoadFor( FrameRendererUtil::Attachment::kSceneColor, false ), colorLoadFor( FrameRendererUtil::Attachment::kSceneDepth, false ) );

            const RHIPipelineStateHandle psoForward = ( _bHasExecutedDepthPrepass.load() != 0 && getEnginePso( "ForwardOpaqueNoDepthWrite" ) != 0 )
                                                        ? getEnginePso( "ForwardOpaqueNoDepthWrite" )
                                                        : getEnginePso( FrameRendererUtil::PassType::kForwardOpaque );
            drawSceneMeshes( ctx, psoForward, passCb, false );
            ctx._pCmd->endRenderPass();
        }
        else if ( passType == FrameRendererUtil::PassType::kGBuffer )
        {
            const float4 clearColor = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kGBufferAlbedo, float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
            const bool   bHasNormal = findTransient( FrameRendererUtil::Attachment::kGBufferNormal ) != 0;
            const bool   bUseMrt    = bHasNormal && _pDevice->supportsMultiRenderTarget() &&
                                      getEnginePso( FrameRendererUtil::PassType::kGBuffer ) != 0;
            if ( bUseMrt )
            {
                const float4              normalClear  = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kGBufferNormal, FrameRendererUtil::kNormalClear );
                const string_view         arrNames[]   = { FrameRendererUtil::Attachment::kGBufferAlbedo, FrameRendererUtil::Attachment::kGBufferNormal };
                const float4              arrClears[2] = { clearColor, normalClear };
                const RHIRenderPassLoadOp arrLoads[]   = { colorLoadFor( FrameRendererUtil::Attachment::kGBufferAlbedo, false ), colorLoadFor( FrameRendererUtil::Attachment::kGBufferNormal, false ) };
                beginColorPassMRT( ctx, arrNames, arrClears, arrLoads, 2, FrameRendererUtil::Attachment::kSceneDepth, colorLoadFor( FrameRendererUtil::Attachment::kSceneDepth, false ) );
                drawSceneMeshes( ctx, getEnginePso( FrameRendererUtil::PassType::kGBuffer ), passCb, false );
                ctx._pCmd->endRenderPass();
            }
            else
            {
                const RHIPipelineStateHandle albedoPso =
                    getEnginePso( FrameRendererUtil::PassType::kGBufferAlbedo ) != 0
                        ? getEnginePso( FrameRendererUtil::PassType::kGBufferAlbedo )
                        : getEnginePso( FrameRendererUtil::PassType::kGBuffer );
                beginColorPass( ctx, FrameRendererUtil::Attachment::kGBufferAlbedo, FrameRendererUtil::Attachment::kSceneDepth, clearColor,
                                colorLoadFor( FrameRendererUtil::Attachment::kGBufferAlbedo, false ), colorLoadFor( FrameRendererUtil::Attachment::kSceneDepth, false ) );
                drawSceneMeshes( ctx, albedoPso != 0 ? albedoPso : 0, passCb, false );
                ctx._pCmd->endRenderPass();

                if ( bHasNormal )
                {
                    const float4 normalClear = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kGBufferNormal, FrameRendererUtil::kNormalClear );
                    beginColorPass( ctx, FrameRendererUtil::Attachment::kGBufferNormal, FrameRendererUtil::Attachment::kSceneDepth, normalClear,
                                    colorLoadFor( FrameRendererUtil::Attachment::kGBufferNormal, false ), RHIRenderPassLoadOp::Load );
                    drawSceneMeshes( ctx, getEnginePso( FrameRendererUtil::PassType::kGBufferNormal ), passCb, false );
                    ctx._pCmd->endRenderPass();
                }
            }
        }
        else if ( passType == FrameRendererUtil::PassType::kLighting || passType == FrameRendererUtil::PassType::kShading )
        {
            registerPassTexture( ctx, "GBufferAlbedo", FrameRendererUtil::Attachment::kGBufferAlbedo );
            registerPassTexture( ctx, "GBufferNormal", FrameRendererUtil::Attachment::kGBufferNormal );
            registerPassTexture( ctx, "SceneDepth", FrameRendererUtil::Attachment::kSceneDepth );
            registerPassTexture( ctx, "ShadowMap", FrameRendererUtil::Attachment::kShadowMap );
            const string_view litTarget = findTransient( FrameRendererUtil::Attachment::kLitColor ) != 0 ? "LitColor" : "SceneColor";
            executeFullscreenPass( getEnginePso( FrameRendererUtil::PassType::kLighting ), litTarget, getAttachmentClearColorOrDefault( litTarget, _clearColor ) );
        }
        else if ( passType == FrameRendererUtil::PassType::kTransparent )
        {
            const string_view colorTarget = findTransient( FrameRendererUtil::Attachment::kTransparentColor ) != 0
                                              ? "TransparentColor"
                                              : ( findTransient( FrameRendererUtil::Attachment::kLitColor ) != 0 ? "LitColor" : "SceneColor" );
            const string_view depthTarget = findTransient( FrameRendererUtil::Attachment::kSceneDepth ) != 0 ? "SceneDepth" : "";

            if ( colorTarget == "TransparentColor" )
            {
                const RHITextureHandle src = findTransient( FrameRendererUtil::Attachment::kLitColor ) != 0 ? findTransient( FrameRendererUtil::Attachment::kLitColor ) : findTransient( FrameRendererUtil::Attachment::kSceneColor );
                if ( src != 0 )
                    ctx._pCmd->blitTexture( src, findTransient( FrameRendererUtil::Attachment::kTransparentColor ) );
                markAttachmentCleared( hashed_string( colorTarget ) );
            }

            beginColorPass( ctx, colorTarget, depthTarget, _clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load );
            const RHIPipelineStateHandle transparentPso =
                getEnginePso( FrameRendererUtil::PassType::kTransparent ) != 0
                    ? getEnginePso( FrameRendererUtil::PassType::kTransparent )
                    : getEnginePso( FrameRendererUtil::PassType::kForwardOpaque );
            drawSceneMeshes( ctx, transparentPso, passCb, true );
            ctx._pCmd->endRenderPass();
        }
        else if ( passType == "SSAO" || passType == "HBAO" )
        {
            const string_view aoTarget = findTransient( "AOColor" ) != 0 ? "AOColor" : FrameRendererUtil::Attachment::kSceneColor;
            registerPassTexture( ctx, "SceneDepth", FrameRendererUtil::Attachment::kSceneDepth );
            registerPassTexture( ctx, "GBufferNormal", FrameRendererUtil::Attachment::kGBufferNormal );
            const RHIPipelineStateHandle aoPso =
                getEnginePso( FrameRendererUtil::PassType::kSsao ) != 0 ? getEnginePso( FrameRendererUtil::PassType::kSsao ) : getEnginePso( FrameRendererUtil::PassType::kHbao );
            executeFullscreenPass( aoPso, aoTarget, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
        }
        else if ( passType == FrameRendererUtil::PassType::kPostBloom || passType == "Bloom" || passType == "Post" )
        {
            const string_view bloomTarget = findTransient( FrameRendererUtil::Attachment::kBloomColor ) != 0 ? "BloomColor" : "SceneColor";
            const utf8*       pSrcName    = FrameRendererUtil::pickFirstExisting( _mapTransient, { "TransparentColor", "LitColor", "SceneColor", "GBufferAlbedo" } );
            if ( pSrcName != nullptr )
                registerPassTexture( ctx, "SourceColor", pSrcName );
            executeFullscreenPass( getEnginePso( FrameRendererUtil::PassType::kPostBloom ), bloomTarget, getAttachmentClearColorOrDefault( bloomTarget, _clearColor ) );
        }
        else if ( passType == FrameRendererUtil::PassType::kOutline || passType == FrameRendererUtil::PassType::kPostOutline )
        {
            const string_view outlineTarget = findTransient( FrameRendererUtil::Attachment::kOutlineColor ) != 0 ? "OutlineColor" : "SceneColor";
            const utf8*       pSrcName      = FrameRendererUtil::pickFirstExisting( _mapTransient, { "BloomColor", "TransparentColor", "LitColor", "SceneColor" } );
            if ( pSrcName != nullptr )
                registerPassTexture( ctx, "SourceColor", pSrcName );
            registerPassTexture( ctx, "SourceDepth", FrameRendererUtil::Attachment::kSceneDepth );
            executeFullscreenPass( getEnginePso( FrameRendererUtil::PassType::kOutline ), outlineTarget, _clearColor );
        }
        else if ( passType == FrameRendererUtil::PassType::kTaa )
        {
            const string_view taaTarget = findTransient( FrameRendererUtil::Attachment::kTaaColor ) != 0 ? "TaaColor" : "SceneColor";
            const utf8*       pSrcName  = FrameRendererUtil::pickFirstExisting( _mapTransient, { "BloomColor", "OutlineColor", "TransparentColor", "LitColor", "SceneColor" } );
            if ( pSrcName != nullptr )
                registerPassTexture( ctx, "SourceColor", pSrcName );
            if ( _taaHistory != 0 )
            {
                if ( _taaHistorySrv == kInvalidDescriptorIndex )
                    _taaHistorySrv = _pDevice->getResource()->registerBindlessTexture( _taaHistory );
                ctx._resourceRegistry.registerTexture( hashed_string( framres::kGBufferAlbedo ), _taaHistory, _taaHistorySrv );
            }
            beginColorPass( ctx, taaTarget, "", _clearColor, colorLoadFor( taaTarget, false ), RHIRenderPassLoadOp::Load );
            const RHIPipelineStateHandle taaPso = getEnginePso( FrameRendererUtil::PassType::kTaa );
            if ( taaPso != 0 )
                drawFullscreen( ctx, taaPso, passCb );
            else if ( pSrcName != nullptr && taaTarget != pSrcName )
                ctx._pCmd->blitTexture( findTransient( pSrcName ), findTransient( taaTarget ) );
            ctx._pCmd->endRenderPass();

            const RHITextureHandle taaOut = findTransient( taaTarget );
            if ( taaOut != 0 )
            {
                if ( _taaHistory == 0 )
                {
                    RHITextureDesc histDesc{};
                    histDesc._width             = _transientWidth != 0 ? _transientWidth : FrameRendererUtil::kDefaultTransientSize;
                    histDesc._height            = _transientHeight != 0 ? _transientHeight : FrameRendererUtil::kDefaultTransientSize;
                    histDesc._format            = RHIFormat::R8G8B8A8_UNORM;
                    histDesc._bIsRenderTarget   = 1;
                    histDesc._bIsShaderResource = 1;
                    _taaHistory                 = _pDevice->getResource()->createTexture2D( histDesc );
                    if ( _taaHistory != 0 )
                        _taaHistorySrv = _pDevice->getResource()->registerBindlessTexture( _taaHistory );
                }
                if ( _taaHistory != 0 )
                    ctx._pCmd->blitTexture( taaOut, _taaHistory );
            }
        }
        else if ( passType == "Tonemap" || passType == "ToneMap" )
        {
            const string_view tonemapTarget =
                findTransient( "TonemapColor" ) != 0 ? "TonemapColor" : FrameRendererUtil::Attachment::kSceneColor;
            const utf8* pSrcName = FrameRendererUtil::pickFirstExisting( _mapTransient, { "TaaColor", "OutlineColor", "BloomColor", "TransparentColor", "LitColor", "SceneColor" } );
            if ( pSrcName != nullptr )
                registerPassTexture( ctx, "SourceColor", pSrcName );
            const RHIPipelineStateHandle tonemapPso =
                getEnginePso( FrameRendererUtil::PassType::kTonemap ) != 0 ? getEnginePso( FrameRendererUtil::PassType::kTonemap ) : getEnginePso( FrameRendererUtil::PassType::kPresent );
            executeFullscreenPass( tonemapPso, tonemapTarget, _clearColor );
        }
        else if ( passType == FrameRendererUtil::PassType::kPresent )
        {
            const string                 srcName   = resolvePresentSource();
            const RHITextureHandle       src       = srcName.empty() ? 0 : findTransient( srcName );
            const RHITextureHandle       dstTarget = _outputRenderTarget;
            const RHIPipelineStateHandle psoBlit   = getEnginePso( FrameRendererUtil::PassType::kPresent );
            if ( src != 0 && psoBlit != 0 )
            {
                registerPassTexture( ctx, "SourceColor", srcName );
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

    void FrameRenderer::registerPassTexture( FramePassContext& ctx, string_view canonicalName, string_view attachmentName )
    {
        const RHITextureHandle tex = findTransient( attachmentName );
        if ( tex != 0 && ctx._pCmd != nullptr )
            ctx._pCmd->prepareTextureForShaderRead( tex );
        const RHIDescriptorIndex srv = findTransientSrv( attachmentName );
        // hashed_string 은 string_view 생성자가 있다 — c_str() 하나 얻자고 힙 string 을 만들 필요가 없다.
        ctx._resourceRegistry.registerTexture( hashed_string( canonicalName ), tex, srv );
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

        auto srvOf = [&ctx]( const utf8* pName ) -> RHIDescriptorIndex
        {
            const RegisteredTexture* pTex = ctx._resourceRegistry.findTexture( hashed_string( pName ) );
            return pTex != nullptr ? pTex->_srv : kInvalidDescriptorIndex;
        };

        const RHIDescriptorIndex shadow      = srvOf( "ShadowMap" );
        const RHIDescriptorIndex albedo      = srvOf( "GBufferAlbedo" );
        const RHIDescriptorIndex normal      = srvOf( "GBufferNormal" );
        const RHIDescriptorIndex depth       = srvOf( "SceneDepth" );
        const RHIDescriptorIndex source      = srvOf( "SourceColor" );
        const RHIDescriptorIndex sourceDepth = srvOf( "SourceDepth" );

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

    const RenderGraphPassDesc* FrameRenderer::findPassDescByType( string_view passType ) const
    {
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._type == passType )
                return &pass;
        }
        return nullptr;
    }
} // namespace sw
