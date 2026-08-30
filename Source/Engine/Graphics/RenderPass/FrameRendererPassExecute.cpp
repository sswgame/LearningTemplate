#include "pch.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Graphics/RenderPass/FrameRendererUtil.h"

namespace sw
{
	SW_LOG_CALLER( "FrameRenderer" );

	void FrameRenderer::bindPassCallbacks()
	{
		const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
		_graph.clear();

		for ( const RenderGraphPassDesc& pass : listPass )
		{
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

			_graph.addPass( hashed_string( pass._name.c_str() ), std::move( listInput ), std::move( listOutput ), std::move( execute ) );
		}

		if ( _graph.compile() == false )
			SW_LOG_ERROR( "Callback bind compile failed" );
		else
			_bCallbacksBound = 1;
	}

	void FrameRenderer::onGraphPassExecute( const RenderGraphPassContext& ctx )
	{
		if ( _pDevice == nullptr )
			return;

		IRHICommandList* pPrevCmd = _pCmd;
		if ( ctx._pCmdList != nullptr )
			_pCmd = ctx._pCmdList;

		const utf8* pPassType = "";
		const utf8* pPassName = ctx._passName.c_str() != nullptr ? ctx._passName.c_str() : "";
		for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
		{
			if ( hashed_string( pass._name.c_str() ) == ctx._passName )
			{
				pPassType = pass._type.c_str();
				pPassName = pass._name.c_str();
				break;
			}
		}
		executePass( pPassType, pPassName, _pBoundMaterial );

		if ( ctx._pCmdList != nullptr )
			_pCmd = pPrevCmd;
	}

	void FrameRenderer::executePass( string_view passType, string_view passName, Material* pMaterial )
	{
		if ( _pCmd == nullptr )
		{
			SW_LOG_ERROR( "executePass: no active IRHICommandList" );
			return;
		}

		_pCmd->beginEventMarker( string( passName ).c_str() );
		clearPassTextureIndices();

		auto colorLoadFor = [this]( string_view name, bool bForceLoad ) -> RHIRenderPassLoadOp
		{
			const hashed_string key( name.data(), static_cast<uint32>( name.length() ) );
			if ( bForceLoad || std::find( _listClearedThisFrame.begin(), _listClearedThisFrame.end(), key ) != _listClearedThisFrame.end() )
				return RHIRenderPassLoadOp::Load;
			_listClearedThisFrame.push_back( key );
			return RHIRenderPassLoadOp::Clear;
		};

		const RHIDescriptorIndex passCb = _passCbIndex != kInvalidDescriptorIndex
											? _passCbIndex
											: ( pMaterial != nullptr ? pMaterial->getDescriptorIndex() : 0 );
		const RHIDescriptorIndex matCb	= pMaterial != nullptr ? pMaterial->getDescriptorIndex() : passCb;

		auto executeFullscreenPass = [&]( RHIPipelineStateHandle pso, string_view targetName, const float4& passClearColor )
		{
			beginColorPass( targetName, "", passClearColor, colorLoadFor( targetName, false ), RHIRenderPassLoadOp::Load );
			drawFullscreen( pso, passCb );
			_pCmd->endRenderPass();
		};

		if ( passType == FrameRendererUtil::PassType::kShadow )
		{
			const float4 clearVal = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kShadowMap, float4{ 1.0f, 0.0f, 0.0f, 0.0f } );
			beginDepthOnlyPass( FrameRendererUtil::Attachment::kShadowMap, clearVal._x, colorLoadFor( FrameRendererUtil::Attachment::kShadowMap, false ) );
			drawSceneMeshes( getEnginePso( FrameRendererUtil::PassType::kShadow ), passCb, false );
			_pCmd->endRenderPass();
		}
		else if ( passType == FrameRendererUtil::PassType::kDepthPrepass || passType == "DepthPrepass" || passType == "Depth" || passType == "PrePass" )
		{
			const float4 clearVal = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kSceneDepth, float4{ 1.0f, 0.0f, 0.0f, 0.0f } );
			beginDepthOnlyPass( FrameRendererUtil::Attachment::kSceneDepth, clearVal._x, colorLoadFor( FrameRendererUtil::Attachment::kSceneDepth, false ) );
			const RHIPipelineStateHandle depthPso = getEnginePso( FrameRendererUtil::PassType::kDepthPrepass ) != 0
													  ? getEnginePso( FrameRendererUtil::PassType::kDepthPrepass )
													  : getEnginePso( FrameRendererUtil::PassType::kShadow );
			drawSceneMeshes( depthPso, passCb, false );
			_pCmd->endRenderPass();
			_bHasExecutedDepthPrepass = 1;
		}
		else if ( passType == FrameRendererUtil::PassType::kForwardOpaque )
		{
			setPassTexture( _passConstants._texShadow, FrameRendererUtil::Attachment::kShadowMap );
			const float4 sceneClear = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kSceneColor, _clearColor );
			beginColorPass( FrameRendererUtil::Attachment::kSceneColor, FrameRendererUtil::Attachment::kSceneDepth, sceneClear,
							colorLoadFor( FrameRendererUtil::Attachment::kSceneColor, false ), colorLoadFor( FrameRendererUtil::Attachment::kSceneDepth, false ) );

			const RHIPipelineStateHandle psoForward = ( _bHasExecutedDepthPrepass != 0 && getEnginePso( "ForwardOpaqueNoDepthWrite" ) != 0 )
														? getEnginePso( "ForwardOpaqueNoDepthWrite" )
														: getEnginePso( FrameRendererUtil::PassType::kForwardOpaque );
			drawSceneMeshes( psoForward, psoForward != 0 ? passCb : matCb, false );
			_pCmd->endRenderPass();
		}
		else if ( passType == FrameRendererUtil::PassType::kGBuffer )
		{
			const float4 clearColor = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kGBufferAlbedo, float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
			const bool	 bHasNormal = findTransient( FrameRendererUtil::Attachment::kGBufferNormal ) != 0;
			const bool	 bUseMrt	= bHasNormal && _pDevice->supportsMultiRenderTarget() &&
									  getEnginePso( FrameRendererUtil::PassType::kGBuffer ) != 0;
			if ( bUseMrt )
			{
				const float4			  normalClear  = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kGBufferNormal, FrameRendererUtil::kNormalClear );
				const string			  arrNames[]   = { FrameRendererUtil::Attachment::kGBufferAlbedo, FrameRendererUtil::Attachment::kGBufferNormal };
				const float4			  arrClears[2] = { clearColor, normalClear };
				const RHIRenderPassLoadOp arrLoads[]   = { colorLoadFor( FrameRendererUtil::Attachment::kGBufferAlbedo, false ), colorLoadFor( FrameRendererUtil::Attachment::kGBufferNormal, false ) };
				beginColorPassMRT( arrNames, arrClears, arrLoads, 2, FrameRendererUtil::Attachment::kSceneDepth, colorLoadFor( FrameRendererUtil::Attachment::kSceneDepth, false ) );
				drawSceneMeshes( getEnginePso( FrameRendererUtil::PassType::kGBuffer ), passCb, false );
				_pCmd->endRenderPass();
			}
			else
			{
				const RHIPipelineStateHandle albedoPso =
					getEnginePso( FrameRendererUtil::PassType::kGBufferAlbedo ) != 0
						? getEnginePso( FrameRendererUtil::PassType::kGBufferAlbedo )
						: getEnginePso( FrameRendererUtil::PassType::kGBuffer );
				beginColorPass( FrameRendererUtil::Attachment::kGBufferAlbedo, FrameRendererUtil::Attachment::kSceneDepth, clearColor,
								colorLoadFor( FrameRendererUtil::Attachment::kGBufferAlbedo, false ), colorLoadFor( FrameRendererUtil::Attachment::kSceneDepth, false ) );
				drawSceneMeshes( albedoPso != 0 ? albedoPso : 0, albedoPso != 0 ? passCb : matCb, false );
				_pCmd->endRenderPass();

				if ( bHasNormal )
				{
					const float4 normalClear = getAttachmentClearColorOrDefault( FrameRendererUtil::Attachment::kGBufferNormal, FrameRendererUtil::kNormalClear );
					beginColorPass( FrameRendererUtil::Attachment::kGBufferNormal, FrameRendererUtil::Attachment::kSceneDepth, normalClear,
									colorLoadFor( FrameRendererUtil::Attachment::kGBufferNormal, false ), RHIRenderPassLoadOp::Load );
					drawSceneMeshes( getEnginePso( FrameRendererUtil::PassType::kGBufferNormal ), passCb, false );
					_pCmd->endRenderPass();
				}
			}
		}
		else if ( passType == FrameRendererUtil::PassType::kLighting || passType == FrameRendererUtil::PassType::kShading )
		{
			setPassTexture( _passConstants._texAlbedo, FrameRendererUtil::Attachment::kGBufferAlbedo );
			setPassTexture( _passConstants._texNormal, FrameRendererUtil::Attachment::kGBufferNormal );
			setPassTexture( _passConstants._texDepth, FrameRendererUtil::Attachment::kSceneDepth );
			setPassTexture( _passConstants._texShadow, FrameRendererUtil::Attachment::kShadowMap );
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
					_pCmd->blitTexture( src, findTransient( FrameRendererUtil::Attachment::kTransparentColor ) );
				_listClearedThisFrame.push_back( hashed_string( colorTarget ) );
			}

			beginColorPass( colorTarget, depthTarget, _clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load );
			const RHIPipelineStateHandle transparentPso =
				getEnginePso( FrameRendererUtil::PassType::kTransparent ) != 0
					? getEnginePso( FrameRendererUtil::PassType::kTransparent )
					: getEnginePso( FrameRendererUtil::PassType::kForwardOpaque );
			drawSceneMeshes( transparentPso, matCb, true );
			_pCmd->endRenderPass();
		}
		else if ( passType == "SSAO" || passType == "HBAO" )
		{
			const string_view aoTarget = findTransient( "AOColor" ) != 0 ? "AOColor" : FrameRendererUtil::Attachment::kSceneColor;
			setPassTexture( _passConstants._texDepth, FrameRendererUtil::Attachment::kSceneDepth );
			setPassTexture( _passConstants._texNormal, FrameRendererUtil::Attachment::kGBufferNormal );
			const RHIPipelineStateHandle aoPso =
				getEnginePso( FrameRendererUtil::PassType::kSsao ) != 0 ? getEnginePso( FrameRendererUtil::PassType::kSsao ) : getEnginePso( FrameRendererUtil::PassType::kHbao );
			executeFullscreenPass( aoPso, aoTarget, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
		}
		else if ( passType == FrameRendererUtil::PassType::kPostBloom || passType == "Bloom" || passType == "Post" )
		{
			const string_view bloomTarget = findTransient( FrameRendererUtil::Attachment::kBloomColor ) != 0 ? "BloomColor" : "SceneColor";
			const utf8*		  pSrcName	  = FrameRendererUtil::pickFirstExisting( _mapTransient, { "TransparentColor", "LitColor", "SceneColor", "GBufferAlbedo" } );
			if ( pSrcName != nullptr )
				setPassTexture( _passConstants._texSource, pSrcName );
			executeFullscreenPass( getEnginePso( FrameRendererUtil::PassType::kPostBloom ), bloomTarget, getAttachmentClearColorOrDefault( bloomTarget, _clearColor ) );
		}
		else if ( passType == FrameRendererUtil::PassType::kOutline || passType == FrameRendererUtil::PassType::kPostOutline )
		{
			const string_view outlineTarget = findTransient( FrameRendererUtil::Attachment::kOutlineColor ) != 0 ? "OutlineColor" : "SceneColor";
			const utf8*		  pSrcName		= FrameRendererUtil::pickFirstExisting( _mapTransient, { "BloomColor", "TransparentColor", "LitColor", "SceneColor" } );
			if ( pSrcName != nullptr )
				setPassTexture( _passConstants._texSource, pSrcName );
			setPassTexture( _passConstants._texSourceDepth, FrameRendererUtil::Attachment::kSceneDepth );
			executeFullscreenPass( getEnginePso( FrameRendererUtil::PassType::kOutline ), outlineTarget, _clearColor );
		}
		else if ( passType == FrameRendererUtil::PassType::kTaa )
		{
			const string_view taaTarget = findTransient( FrameRendererUtil::Attachment::kTaaColor ) != 0 ? "TaaColor" : "SceneColor";
			const utf8*		  pSrcName	= FrameRendererUtil::pickFirstExisting( _mapTransient, { "BloomColor", "OutlineColor", "TransparentColor", "LitColor", "SceneColor" } );
			if ( pSrcName != nullptr )
				setPassTexture( _passConstants._texSource, pSrcName );
			if ( _taaHistory != 0 )
			{
				if ( _taaHistorySrv == kInvalidDescriptorIndex )
					_taaHistorySrv = _pDevice->getResource()->registerBindlessTexture( _taaHistory );
				_passConstants._texAlbedo = _taaHistorySrv;
			}
			beginColorPass( taaTarget, "", _clearColor, colorLoadFor( taaTarget, false ), RHIRenderPassLoadOp::Load );
			const RHIPipelineStateHandle taaPso = getEnginePso( FrameRendererUtil::PassType::kTaa );
			if ( taaPso != 0 )
				drawFullscreen( taaPso, passCb );
			else if ( pSrcName != nullptr && taaTarget != pSrcName )
				_pCmd->blitTexture( findTransient( pSrcName ), findTransient( taaTarget ) );
			_pCmd->endRenderPass();

			const RHITextureHandle taaOut = findTransient( taaTarget );
			if ( taaOut != 0 )
			{
				if ( _taaHistory == 0 )
				{
					RHITextureDesc histDesc{};
					histDesc._width				= _transientWidth != 0 ? _transientWidth : FrameRendererUtil::kDefaultTransientSize;
					histDesc._height			= _transientHeight != 0 ? _transientHeight : FrameRendererUtil::kDefaultTransientSize;
					histDesc._format			= RHIFormat::R8G8B8A8_UNORM;
					histDesc._bIsRenderTarget	= 1;
					histDesc._bIsShaderResource = 1;
					_taaHistory					= _pDevice->getResource()->createTexture2D( histDesc );
					if ( _taaHistory != 0 )
						_taaHistorySrv = _pDevice->getResource()->registerBindlessTexture( _taaHistory );
				}
				if ( _taaHistory != 0 )
					_pCmd->blitTexture( taaOut, _taaHistory );
			}
		}
		else if ( passType == "Tonemap" || passType == "ToneMap" )
		{
			const string_view tonemapTarget =
				findTransient( "TonemapColor" ) != 0 ? "TonemapColor" : FrameRendererUtil::Attachment::kSceneColor;
			const utf8* pSrcName = FrameRendererUtil::pickFirstExisting( _mapTransient, { "TaaColor", "OutlineColor", "BloomColor", "TransparentColor", "LitColor", "SceneColor" } );
			if ( pSrcName != nullptr )
				setPassTexture( _passConstants._texSource, pSrcName );
			const RHIPipelineStateHandle tonemapPso =
				getEnginePso( FrameRendererUtil::PassType::kTonemap ) != 0 ? getEnginePso( FrameRendererUtil::PassType::kTonemap ) : getEnginePso( FrameRendererUtil::PassType::kPresent );
			executeFullscreenPass( tonemapPso, tonemapTarget, _clearColor );
		}
		else if ( passType == FrameRendererUtil::PassType::kPresent )
		{
			const string				 srcName   = resolvePresentSource();
			const RHITextureHandle		 src	   = srcName.empty() ? 0 : findTransient( srcName );
			const RHITextureHandle		 dstTarget = _outputRenderTarget;
			const RHIPipelineStateHandle psoBlit   = getEnginePso( FrameRendererUtil::PassType::kPresent );
			if ( src != 0 && psoBlit != 0 )
			{
				setPassTexture( _passConstants._texSource, srcName );
				RHIRenderPassBeginInfo beginInfo{};
				beginInfo._bBindColor		 = 1;
				beginInfo._arrColorTarget[0] = dstTarget;
				beginInfo._colorTargetCount	 = 1;
				beginInfo._arrLoadOp[0]		 = RHIRenderPassLoadOp::DontCare;
				beginInfo._width			 = _transientWidth;
				beginInfo._height			 = _transientHeight;
				_pCmd->beginRenderPass( beginInfo );
				drawFullscreen( psoBlit, passCb );
				_pCmd->endRenderPass();
			}
			else if ( src != 0 )
				_pCmd->blitTexture( src, dstTarget );
			else
			{
				RHIRenderPassBeginInfo beginInfo{};
				beginInfo.setColorTarget( dstTarget, _clearColor, RHIRenderPassLoadOp::Load );
				beginInfo._bBindColor = 1;
				_pCmd->beginRenderPass( beginInfo );
				drawFullscreen( 0, matCb );
				_pCmd->endRenderPass();
			}
		}
		else
			SW_LOG_WARNING( "Unknown pass type '%#' in '%#'", passType, passName );

		_pCmd->endEventMarker();
	}

	void FrameRenderer::beginColorPass( string_view colorName, string_view depthName, const float4& clearColor,
										RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad )
	{
		const string			  arrName[]	 = { string( colorName ) };
		const float4			  arrClear[] = { clearColor };
		const RHIRenderPassLoadOp arrLoad[]	 = { colorLoad };
		beginColorPassMRT( arrName, arrClear, arrLoad, 1, depthName, depthLoad );
	}

	void FrameRenderer::beginColorPassMRT( const string* pColorNames, const float4* pTargetClearColor, const RHIRenderPassLoadOp* pColorLoad,
										   uint32 colorCount, string_view depthName, RHIRenderPassLoadOp depthLoad )
	{
		if ( _pDevice == nullptr || _pCmd == nullptr || pColorNames == nullptr || colorCount == 0 )
			return;

		RHIRenderPassBeginInfo beginInfo{};
		beginInfo._bBindColor		= 1;
		beginInfo._depthLoadOp		= depthLoad;
		beginInfo._clearDepth		= 1.0f;
		beginInfo._depthTarget		= depthName.empty() ? 0 : findTransient( depthName );
		beginInfo._width			= _transientWidth;
		beginInfo._height			= _transientHeight;
		beginInfo._colorTargetCount = colorCount > kMaxColorAttachments ? kMaxColorAttachments : colorCount;
		for ( uint32 colorTargetIndex = 0; colorTargetIndex < beginInfo._colorTargetCount; ++colorTargetIndex )
		{
			beginInfo._arrColorTarget[colorTargetIndex] = findTransient( pColorNames[colorTargetIndex] );
			beginInfo._arrLoadOp[colorTargetIndex]		= pColorLoad != nullptr ? pColorLoad[colorTargetIndex] : RHIRenderPassLoadOp::Clear;
			if ( pTargetClearColor != nullptr )
				beginInfo._arrClearColor[colorTargetIndex] = pTargetClearColor[colorTargetIndex];
		}
		_pCmd->beginRenderPass( beginInfo );
	}

	void FrameRenderer::beginDepthOnlyPass( string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad )
	{
		if ( _pCmd == nullptr )
			return;
		RHIRenderPassBeginInfo beginInfo{};
		beginInfo._bBindColor		= 0;
		beginInfo._colorTargetCount = 0;
		beginInfo._depthTarget		= findTransient( depthName );
		beginInfo._depthLoadOp		= depthLoad;
		beginInfo._clearDepth		= clearDepth;
		beginInfo._width			= _transientWidth;
		beginInfo._height			= _transientHeight;
		_pCmd->beginRenderPass( beginInfo );
	}

	void FrameRenderer::setPassTexture( uint32& outIndex, string_view name )
	{
		const RHITextureHandle tex = findTransient( name );
		if ( tex != 0 && _pCmd != nullptr )
			_pCmd->prepareTextureForShaderRead( tex );
		const RHIDescriptorIndex srv = findTransientSrv( name );
		outIndex					 = ( srv != kInvalidDescriptorIndex ) ? static_cast<uint32>( srv ) : kInvalidDescriptorIndex;
	}

	void FrameRenderer::commitBindlessTextureBindings()
	{
		if ( _pDevice == nullptr || _pCmd == nullptr )
			return;

		updatePassConstants();

		// DX11/GL: bind PassCB indices into t0..t3 (emulated bindless).
		// DX12/VK: shaders index the heap/array directly — skip slot binds.
		if ( _pDevice->supportsNativeBindlessSampling() )
			return;

		const RHIDescriptorIndex slot0 = ( _passConstants._texShadow != kInvalidDescriptorIndex )
										   ? _passConstants._texShadow
										   : _passConstants._texSource;
		const RHIDescriptorIndex slot1 = ( _passConstants._texAlbedo != kInvalidDescriptorIndex )
										   ? _passConstants._texAlbedo
										   : _passConstants._texSourceDepth;
		const RHIDescriptorIndex slot2 = _passConstants._texNormal;
		const RHIDescriptorIndex slot3 = ( _passConstants._texDepth != kInvalidDescriptorIndex )
										   ? _passConstants._texDepth
										   : _passConstants._texShadow;

		if ( _passConstants._texShadow != kInvalidDescriptorIndex || _passConstants._texSource != kInvalidDescriptorIndex )
			_pCmd->bindShaderResource( slot0, 0 );
		if ( _passConstants._texAlbedo != kInvalidDescriptorIndex || _passConstants._texSourceDepth != kInvalidDescriptorIndex )
			_pCmd->bindShaderResource( slot1, 1 );
		if ( _passConstants._texNormal != kInvalidDescriptorIndex )
			_pCmd->bindShaderResource( slot2, 2 );
		if ( _passConstants._texDepth != kInvalidDescriptorIndex || ( _passConstants._texShadow != kInvalidDescriptorIndex && _passConstants._texDepth == kInvalidDescriptorIndex ) )
			_pCmd->bindShaderResource( slot3, 3 );
	}

	void FrameRenderer::clearPassTextureIndices()
	{
		_passConstants._texShadow	   = kInvalidDescriptorIndex;
		_passConstants._texAlbedo	   = kInvalidDescriptorIndex;
		_passConstants._texNormal	   = kInvalidDescriptorIndex;
		_passConstants._texDepth	   = kInvalidDescriptorIndex;
		_passConstants._texSource	   = kInvalidDescriptorIndex;
		_passConstants._texSourceDepth = kInvalidDescriptorIndex;
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
