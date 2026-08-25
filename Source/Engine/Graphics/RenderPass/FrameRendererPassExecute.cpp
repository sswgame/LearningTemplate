#include "pch.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Graphics/RenderPass/FrameRendererInternal.h"

namespace sw
{
	void FrameRenderer::bindPassCallbacks()
	{
		const vector<RenderGraphPassDesc>& passes = _pipelineResource.getGraphPasses();
		_graph.clear();

		for ( const RenderGraphPassDesc& pass : passes )
		{
			vector<hashed_string> listInputs;
			vector<hashed_string> listOutputs;
			for ( const string& in : pass._listInputs )
			{
				listInputs.emplace_back( in.c_str() );
			}
			for ( const string& out : pass._listOutputs )
			{
				listOutputs.emplace_back( out.c_str() );
			}

			RenderGraphPassExecuteFn execute =
				SW_DELEGATE_METHOD( RenderGraphPassExecuteFn, &FrameRenderer::onGraphPassExecute, this );

			_graph.addPass( hashed_string( pass._name.c_str() ), std::move( listInputs ), std::move( listOutputs ), std::move( execute ) );
		}

		if ( _graph.compile() == false )
			SW_LOG_ERROR( "[FrameRenderer] Callback bind compile failed" );
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
		for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPasses() )
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
			SW_LOG_ERROR( "[FrameRenderer] executePass: no active IRHICommandList" );
			return;
		}

		_pCmd->beginEventMarker( string( passName ).c_str() );

		float32 arrClearColor[4];
		auto	colorLoadFor = [this]( string_view name, bool bForceLoad ) -> RHIRenderPassLoadOp
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

		if ( passType == PassType::kShadow )
		{
			clearPassTextureIndices();
			const float32 depthClear = tryGetAttachmentClearColor( Attachment::kShadowMap, arrClearColor ) ? arrClearColor[0] : 1.0f;
			beginDepthOnlyPass( Attachment::kShadowMap, depthClear, colorLoadFor( Attachment::kShadowMap, false ) );
			drawSceneMeshes( getEnginePso( PassType::kShadow ), passCb, false );
			_pCmd->endRenderPass();
		}
		else if ( passType == PassType::kDepthPrepass || passType == "DepthPrepass" || passType == "Depth" || passType == "PrePass" )
		{
			clearPassTextureIndices();
			const float32 depthClear = tryGetAttachmentClearColor( Attachment::kSceneDepth, arrClearColor ) ? arrClearColor[0] : 1.0f;
			beginDepthOnlyPass( Attachment::kSceneDepth, depthClear, colorLoadFor( Attachment::kSceneDepth, false ) );
			drawSceneMeshes( getEnginePso( PassType::kDepthPrepass ) != 0
								 ? getEnginePso( PassType::kDepthPrepass )
								 : getEnginePso( PassType::kShadow ),
							 passCb, false );
			_pCmd->endRenderPass();
			_bHasExecutedDepthPrepass = 1;
		}
		else if ( passType == PassType::kForwardOpaque )
		{
			clearPassTextureIndices();
			setPassTexture( _passConstants._texShadow, Attachment::kShadowMap );
			if ( tryGetAttachmentClearColor( Attachment::kSceneColor, arrClearColor ) == false )
				Memory::copy( arrClearColor, _arrClearColor, sizeof( arrClearColor ) );
			beginColorPass( Attachment::kSceneColor, Attachment::kSceneDepth, arrClearColor,
							colorLoadFor( Attachment::kSceneColor, false ), colorLoadFor( Attachment::kSceneDepth, false ) );

			const RHIPipelineStateHandle psoForward = ( _bHasExecutedDepthPrepass != 0 && getEnginePso( "ForwardOpaqueNoDepthWrite" ) != 0 )
														? getEnginePso( "ForwardOpaqueNoDepthWrite" )
														: getEnginePso( PassType::kForwardOpaque );
			drawSceneMeshes( psoForward, psoForward != 0 ? passCb : matCb, false );
			_pCmd->endRenderPass();
		}
		else if ( passType == PassType::kGBuffer )
		{
			clearPassTextureIndices();
			if ( tryGetAttachmentClearColor( Attachment::kGBufferAlbedo, arrClearColor ) == false )
			{
				arrClearColor[0] = 0.0f;
				arrClearColor[1] = 0.0f;
				arrClearColor[2] = 0.0f;
				arrClearColor[3] = 1.0f;
			}

			const bool bHasNormal = findTransient( Attachment::kGBufferNormal ) != 0;
			const bool bUseMrt	  = bHasNormal && _pDevice->supportsMultiRenderTarget() &&
									getEnginePso( PassType::kGBuffer ) != 0;
			if ( bUseMrt )
			{
				float32 arrNormalClear[4] = { kNormalClear[0], kNormalClear[1], kNormalClear[2], kNormalClear[3] };
				tryGetAttachmentClearColor( Attachment::kGBufferNormal, arrNormalClear );
				const string  arrNames[]	  = { Attachment::kGBufferAlbedo, Attachment::kGBufferNormal };
				const float32 arrClears[2][4] = {
					{ arrClearColor[0],	arrClearColor[1],  arrClearColor[2],	arrClearColor[3]},
					{arrNormalClear[0], arrNormalClear[1], arrNormalClear[2], arrNormalClear[3]}
				   };
				const RHIRenderPassLoadOp arrLoads[] = { colorLoadFor( Attachment::kGBufferAlbedo, false ), colorLoadFor( Attachment::kGBufferNormal, false ) };
				beginColorPassMRT( arrNames, arrClears, arrLoads, 2, Attachment::kSceneDepth, colorLoadFor( Attachment::kSceneDepth, false ) );
				drawSceneMeshes( getEnginePso( PassType::kGBuffer ), passCb, false );
				_pCmd->endRenderPass();
			}
			else
			{
				const RHIPipelineStateHandle albedoPso =
					getEnginePso( PassType::kGBufferAlbedo ) != 0
						? getEnginePso( PassType::kGBufferAlbedo )
						: getEnginePso( PassType::kGBuffer );
				beginColorPass( Attachment::kGBufferAlbedo, Attachment::kSceneDepth, arrClearColor,
								colorLoadFor( Attachment::kGBufferAlbedo, false ), colorLoadFor( Attachment::kSceneDepth, false ) );
				drawSceneMeshes( albedoPso != 0 ? albedoPso : 0, albedoPso != 0 ? passCb : matCb, false );
				_pCmd->endRenderPass();

				if ( bHasNormal )
				{
					float32 arrNormalClear[4] = { kNormalClear[0], kNormalClear[1], kNormalClear[2], kNormalClear[3] };
					tryGetAttachmentClearColor( Attachment::kGBufferNormal, arrNormalClear );
					beginColorPass( Attachment::kGBufferNormal, Attachment::kSceneDepth, arrNormalClear,
									colorLoadFor( Attachment::kGBufferNormal, false ), RHIRenderPassLoadOp::Load );
					drawSceneMeshes( getEnginePso( PassType::kGBufferNormal ), passCb, false );
					_pCmd->endRenderPass();
				}
			}
		}
		else if ( passType == PassType::kLighting || passType == PassType::kShading )
		{
			clearPassTextureIndices();
			setPassTexture( _passConstants._texAlbedo, Attachment::kGBufferAlbedo );
			setPassTexture( _passConstants._texNormal, Attachment::kGBufferNormal );
			setPassTexture( _passConstants._texDepth, Attachment::kSceneDepth );
			setPassTexture( _passConstants._texShadow, Attachment::kShadowMap );
			const string_view litTarget = findTransient( Attachment::kLitColor ) != 0 ? "LitColor" : "SceneColor";
			if ( tryGetAttachmentClearColor( litTarget, arrClearColor ) == false )
				Memory::copy( arrClearColor, _arrClearColor, sizeof( arrClearColor ) );
			beginColorPass( litTarget, "", arrClearColor, colorLoadFor( litTarget, false ), RHIRenderPassLoadOp::Load );
			drawFullscreen( getEnginePso( PassType::kLighting ), passCb );
			_pCmd->endRenderPass();
		}
		else if ( passType == PassType::kTransparent )
		{
			const string_view colorTarget = findTransient( Attachment::kTransparentColor ) != 0
											  ? "TransparentColor"
											  : ( findTransient( Attachment::kLitColor ) != 0 ? "LitColor" : "SceneColor" );
			const string_view depthTarget = findTransient( Attachment::kSceneDepth ) != 0 ? "SceneDepth" : "";

			if ( colorTarget == "TransparentColor" )
			{
				const RHITextureHandle src = findTransient( Attachment::kLitColor ) != 0 ? findTransient( Attachment::kLitColor ) : findTransient( Attachment::kSceneColor );
				if ( src != 0 )
					_pCmd->blitTexture( src, findTransient( Attachment::kTransparentColor ) );
				_listClearedThisFrame.push_back( hashed_string( colorTarget ) );
			}

			beginColorPass( colorTarget, depthTarget, _arrClearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load );
			const RHIPipelineStateHandle transparentPso =
				getEnginePso( PassType::kTransparent ) != 0
					? getEnginePso( PassType::kTransparent )
					: getEnginePso( PassType::kForwardOpaque );
			drawSceneMeshes( transparentPso, matCb, true );
			_pCmd->endRenderPass();
		}
		else if ( passType == "SSAO" || passType == "HBAO" )
		{
			clearPassTextureIndices();
			const string_view aoTarget		= findTransient( "AOColor" ) != 0 ? "AOColor" : Attachment::kSceneColor;
			float32			  arrAoClear[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			setPassTexture( _passConstants._texDepth, Attachment::kSceneDepth );
			setPassTexture( _passConstants._texNormal, Attachment::kGBufferNormal );
			beginColorPass( aoTarget, "", arrAoClear, colorLoadFor( aoTarget, false ), RHIRenderPassLoadOp::Load );
			const RHIPipelineStateHandle aoPso =
				getEnginePso( PassType::kSsao ) != 0 ? getEnginePso( PassType::kSsao ) : getEnginePso( PassType::kHbao );
			drawFullscreen( aoPso, passCb );
			_pCmd->endRenderPass();
		}
		else if ( passType == PassType::kPostBloom || passType == "Bloom" || passType == "Post" )
		{
			clearPassTextureIndices();
			const string_view bloomTarget = findTransient( Attachment::kBloomColor ) != 0 ? "BloomColor" : "SceneColor";
			const utf8*		  pSrcName	  = pickFirstExisting( _mapTransients, { "TransparentColor", "LitColor", "SceneColor", "GBufferAlbedo" } );
			if ( pSrcName != nullptr )
				setPassTexture( _passConstants._texSource, pSrcName );
			if ( tryGetAttachmentClearColor( bloomTarget, arrClearColor ) == false )
				Memory::copy( arrClearColor, _arrClearColor, sizeof( arrClearColor ) );
			beginColorPass( bloomTarget, "", arrClearColor, colorLoadFor( bloomTarget, false ), RHIRenderPassLoadOp::Load );
			drawFullscreen( getEnginePso( PassType::kPostBloom ), passCb );
			_pCmd->endRenderPass();
		}
		else if ( passType == PassType::kOutline || passType == PassType::kPostOutline )
		{
			clearPassTextureIndices();
			const string_view outlineTarget = findTransient( Attachment::kOutlineColor ) != 0 ? "OutlineColor" : "SceneColor";
			const utf8*		  pSrcName		= pickFirstExisting( _mapTransients, { "BloomColor", "TransparentColor", "LitColor", "SceneColor" } );
			if ( pSrcName != nullptr )
				setPassTexture( _passConstants._texSource, pSrcName );
			setPassTexture( _passConstants._texSourceDepth, Attachment::kSceneDepth );
			beginColorPass( outlineTarget, "", _arrClearColor, colorLoadFor( outlineTarget, false ), RHIRenderPassLoadOp::Load );
			drawFullscreen( getEnginePso( PassType::kOutline ), passCb );
			_pCmd->endRenderPass();
		}
		else if ( passType == PassType::kTaa )
		{
			clearPassTextureIndices();
			const string_view taaTarget = findTransient( Attachment::kTaaColor ) != 0 ? "TaaColor" : "SceneColor";
			const utf8*		  pSrcName	= pickFirstExisting( _mapTransients, { "BloomColor", "OutlineColor", "TransparentColor", "LitColor", "SceneColor" } );
			if ( pSrcName != nullptr )
				setPassTexture( _passConstants._texSource, pSrcName );
			if ( _taaHistory != 0 )
			{
				const RHIDescriptorIndex histIdx = _pDevice->getResource()->registerBindlessTexture( _taaHistory );
				_passConstants._texAlbedo		 = histIdx;
			}
			beginColorPass( taaTarget, "", _arrClearColor, colorLoadFor( taaTarget, false ), RHIRenderPassLoadOp::Load );
			const RHIPipelineStateHandle taaPso = getEnginePso( PassType::kTaa );
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
					histDesc._width				= _transientWidth != 0 ? _transientWidth : kDefaultTransientSize;
					histDesc._height			= _transientHeight != 0 ? _transientHeight : kDefaultTransientSize;
					histDesc._format			= RHIFormat::R8G8B8A8_UNORM;
					histDesc._bIsRenderTarget	= 1;
					histDesc._bIsShaderResource = 1;
					_taaHistory					= _pDevice->getResource()->createTexture2D( histDesc );
				}
				if ( _taaHistory != 0 )
					_pCmd->blitTexture( taaOut, _taaHistory );
			}
		}
		else if ( passType == "Tonemap" || passType == "ToneMap" )
		{
			clearPassTextureIndices();
			const string_view tonemapTarget =
				findTransient( "TonemapColor" ) != 0 ? "TonemapColor" : Attachment::kSceneColor;
			const utf8* pSrcName = pickFirstExisting( _mapTransients, { "TaaColor", "OutlineColor", "BloomColor", "TransparentColor", "LitColor", "SceneColor" } );
			if ( pSrcName != nullptr )
				setPassTexture( _passConstants._texSource, pSrcName );
			beginColorPass( tonemapTarget, "", _arrClearColor, colorLoadFor( tonemapTarget, false ), RHIRenderPassLoadOp::Load );
			const RHIPipelineStateHandle tonemapPso =
				getEnginePso( PassType::kTonemap ) != 0 ? getEnginePso( PassType::kTonemap ) : getEnginePso( PassType::kPresent );
			drawFullscreen( tonemapPso, passCb );
			_pCmd->endRenderPass();
		}
		else if ( passType == PassType::kPresent )
		{
			const string		   srcName	 = resolvePresentSource();
			const RHITextureHandle src		 = srcName.empty() ? 0 : findTransient( srcName );
			const RHITextureHandle dstTarget = _outputRenderTarget;
			clearPassTextureIndices();
			const RHIPipelineStateHandle psoBlit = getEnginePso( PassType::kPresent );
			if ( src != 0 && psoBlit != 0 )
			{
				setPassTexture( _passConstants._texSource, srcName );
				RHIRenderPassBeginInfo beginInfo{};
				beginInfo._bBindColor		  = 1;
				beginInfo._colorTarget		  = dstTarget;
				beginInfo._arrColorTargets[0] = dstTarget;
				beginInfo._colorTargetCount	  = 1;
				beginInfo._loadOp			  = RHIRenderPassLoadOp::DontCare;
				beginInfo._arrLoadOps[0]	  = RHIRenderPassLoadOp::DontCare;
				beginInfo._width			  = _transientWidth;
				beginInfo._height			  = _transientHeight;
				_pCmd->beginRenderPass( beginInfo );
				drawFullscreen( psoBlit, passCb );
				_pCmd->endRenderPass();
			}
			else if ( src != 0 )
				_pCmd->blitTexture( src, dstTarget );
			else
			{
				RHIRenderPassBeginInfo beginInfo{};
				Memory::copy( beginInfo._arrClearColor, _arrClearColor, sizeof( beginInfo._arrClearColor ) );
				beginInfo._loadOp			  = RHIRenderPassLoadOp::Load;
				beginInfo._bBindColor		  = 1;
				beginInfo._colorTarget		  = dstTarget;
				beginInfo._arrColorTargets[0] = dstTarget;
				beginInfo._colorTargetCount	  = 1;
				_pCmd->beginRenderPass( beginInfo );
				drawFullscreen( 0, matCb );
				_pCmd->endRenderPass();
			}
		}
		else
			SW_LOG_WARNING( "[FrameRenderer] Unknown pass type '%#' in '%#'", passType, passName );

		_pCmd->endEventMarker();
	}

	void FrameRenderer::beginColorPass( string_view colorName, string_view depthName, const float32 arrClearColor[4],
										RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad )
	{
		const string  arrNames[]	  = { string( colorName ) };
		const float32 arrClears[1][4] = {
			{ arrClearColor[0], arrClearColor[1], arrClearColor[2], arrClearColor[3] }
		   };
		const RHIRenderPassLoadOp arrLoads[] = { colorLoad };
		beginColorPassMRT( arrNames, arrClears, arrLoads, 1, depthName, depthLoad );
	}

	void FrameRenderer::beginColorPassMRT( const string* pColorNames, const float32 arrClearColors[][4], const RHIRenderPassLoadOp* pColorLoads,
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
			beginInfo._arrColorTargets[colorTargetIndex] = findTransient( pColorNames[colorTargetIndex] );
			beginInfo._arrLoadOps[colorTargetIndex]		 = pColorLoads != nullptr ? pColorLoads[colorTargetIndex] : RHIRenderPassLoadOp::Clear;
			if ( arrClearColors != nullptr )
				Memory::copy( beginInfo._arrClearColors[colorTargetIndex], arrClearColors[colorTargetIndex], sizeof( beginInfo._arrClearColors[colorTargetIndex] ) );
		}
		beginInfo._colorTarget = beginInfo._arrColorTargets[0];
		beginInfo._loadOp	   = beginInfo._arrLoadOps[0];
		Memory::copy( beginInfo._arrClearColor, beginInfo._arrClearColors[0], sizeof( beginInfo._arrClearColor ) );
		_pCmd->beginRenderPass( beginInfo );
	}

	void FrameRenderer::beginDepthOnlyPass( string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad )
	{
		if ( _pCmd == nullptr )
			return;
		RHIRenderPassBeginInfo beginInfo{};
		beginInfo._bBindColor  = 0;
		beginInfo._colorTarget = 0;
		beginInfo._depthTarget = findTransient( depthName );
		beginInfo._depthLoadOp = depthLoad;
		beginInfo._clearDepth  = clearDepth;
		beginInfo._width	   = _transientWidth;
		beginInfo._height	   = _transientHeight;
		_pCmd->beginRenderPass( beginInfo );
	}

	void FrameRenderer::setPassTexture( uint32& outIndex, string_view name )
	{
		const RHITextureHandle tex = findTransient( name );
		if ( tex != 0 && _pCmd != nullptr )
			_pCmd->prepareTextureForShaderRead( tex );
		const RHIDescriptorIndex srv = findTransientSrv( name );
		outIndex					 = ( srv != kInvalidDescriptorIndex ) ? static_cast<uint32>( srv ) : 0xFFFFFFFFu;
	}

	void FrameRenderer::commitBindlessTextureBindings()
	{
		if ( _pDevice == nullptr || _pCmd == nullptr )
			return;

		updatePassConstants();

		// DX11/GL: bind PassCB indices into t0..t3 (emulated bindless).
		// DX12/VK: shaders index the heap/array directly ??skip slot binds.
		if ( _pDevice->supportsNativeBindlessSampling() )
			return;

		const RHIDescriptorIndex slot0 = ( _passConstants._texShadow != 0xFFFFFFFFu )
										   ? _passConstants._texShadow
										   : _passConstants._texSource;
		const RHIDescriptorIndex slot1 = ( _passConstants._texAlbedo != 0xFFFFFFFFu )
										   ? _passConstants._texAlbedo
										   : _passConstants._texSourceDepth;
		const RHIDescriptorIndex slot2 = _passConstants._texNormal;
		const RHIDescriptorIndex slot3 = ( _passConstants._texDepth != 0xFFFFFFFFu )
										   ? _passConstants._texDepth
										   : _passConstants._texShadow;

		if ( _passConstants._texShadow != 0xFFFFFFFFu || _passConstants._texSource != 0xFFFFFFFFu )
			_pCmd->bindShaderResource( slot0, 0 );
		if ( _passConstants._texAlbedo != 0xFFFFFFFFu || _passConstants._texSourceDepth != 0xFFFFFFFFu )
			_pCmd->bindShaderResource( slot1, 1 );
		if ( _passConstants._texNormal != 0xFFFFFFFFu )
			_pCmd->bindShaderResource( slot2, 2 );
		if ( _passConstants._texDepth != 0xFFFFFFFFu || ( _passConstants._texShadow != 0xFFFFFFFFu && _passConstants._texDepth == 0xFFFFFFFFu ) )
			_pCmd->bindShaderResource( slot3, 3 );
	}

	void FrameRenderer::clearPassTextureIndices()
	{
		_passConstants._texShadow	   = 0xFFFFFFFFu;
		_passConstants._texAlbedo	   = 0xFFFFFFFFu;
		_passConstants._texNormal	   = 0xFFFFFFFFu;
		_passConstants._texDepth	   = 0xFFFFFFFFu;
		_passConstants._texSource	   = 0xFFFFFFFFu;
		_passConstants._texSourceDepth = 0xFFFFFFFFu;
	}

	const RenderGraphPassDesc* FrameRenderer::findPassDescByType( string_view passType ) const
	{
		for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPasses() )
		{
			if ( pass._type == passType )
				return &pass;
		}
		return nullptr;
	}
} // namespace sw
