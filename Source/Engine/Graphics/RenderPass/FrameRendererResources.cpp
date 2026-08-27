#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Graphics/RenderPass/FrameRendererInternal.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
	void FrameRenderer::ensurePassResources()
	{
		if ( _pDevice == nullptr || _bPassResourcesReady != 0 )
			return;

		_passCb = _pDevice->getResource()->createConstantBuffer( sizeof( PassConstants ) );
		if ( _passCb != 0 )
			_passCbIndex = _pDevice->getResource()->registerBindlessResource( _passCb );

		struct GpuCullParams
		{
			float32 _planes[6][4]{};
			uint32	_instanceCount{ 0 };
			uint32	_batchCount{ 0 };
			uint32	_pad[2]{};
		};
		_gpuCullCb = _pDevice->getResource()->createConstantBuffer( sizeof( GpuCullParams ) );
		if ( _gpuCullCb != 0 )
			_gpuCullCbIndex = _pDevice->getResource()->registerBindlessResource( _gpuCullCb );

		constexpr RHIFormat arrGbufferFormats[] = { RHIFormat::R8G8B8A8_UNORM, RHIFormat::R16G16B16A16_FLOAT };
		const EngineData&	engineData			= engine::getEngineData();
		// Shader paths prefer pipeline XML pass recipes; EngineData paths are last-resort fallbacks only.

		const RHIPipelineStateHandle psoShadow =
			createPsoForPassType( PassType::kShadow, engineData._shaderShadowDepth.c_str(), true, 0, nullptr, false, true );
		if ( psoShadow != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kShadow, psoShadow );

		const RHIPipelineStateHandle psoDepthPrepass =
			createPsoForPassType( PassType::kDepthPrepass, engineData._shaderShadowDepth.c_str(), true, 0, nullptr, false, true );
		if ( psoDepthPrepass != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kDepthPrepass, psoDepthPrepass );

		const RHIPipelineStateHandle psoForward = createPsoForPassType( PassType::kForwardOpaque, engineData._shaderForwardLit.c_str(), true );
		if ( psoForward != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kForwardOpaque, psoForward );

		const RHIPipelineStateHandle psoForwardNoDepthWrite =
			createPsoForPassType( PassType::kForwardOpaque, engineData._shaderForwardLit.c_str(), true, 1, nullptr, false, false );
		if ( psoForwardNoDepthWrite != 0 )
			_mapEnginePsos.insert_or_assign( "ForwardOpaqueNoDepthWrite", psoForwardNoDepthWrite );

		const RHIPipelineStateHandle psoTransparent =
			createPsoForPassType( PassType::kTransparent, engineData._shaderForwardLit.c_str(), true, 1, nullptr, true, false );
		if ( psoTransparent != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kTransparent, psoTransparent );

		const RHIPipelineStateHandle psoGBuffer =
			createPsoForPassType( PassType::kGBuffer, engineData._shaderGBuffer.c_str(), true, 2, arrGbufferFormats );
		if ( psoGBuffer != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kGBuffer, psoGBuffer );

		const RHIPipelineStateHandle psoGBufferAlbedo =
			createPsoForPassType( PassType::kGBufferAlbedo, engineData._shaderGBufferAlbedo.c_str(), true );
		if ( psoGBufferAlbedo != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kGBufferAlbedo, psoGBufferAlbedo );

		const RHIPipelineStateHandle psoGBufferNrm =
			createPsoForPassType( PassType::kGBufferNormal, engineData._shaderGBufferNormal.c_str(), true );
		if ( psoGBufferNrm != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kGBufferNormal, psoGBufferNrm );

		const RHIPipelineStateHandle psoDeferred =
			createPsoForPassType( PassType::kLighting, engineData._shaderDeferredLighting.c_str(), false );
		if ( psoDeferred != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kLighting, psoDeferred );

		const RHIPipelineStateHandle psoBloom =
			createPsoForPassType( PassType::kPostBloom, engineData._shaderPostBloom.c_str(), false );
		if ( psoBloom != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kPostBloom, psoBloom );

		{
			RHIPipelineStateHandle psoOutline = createPsoForPassType( PassType::kOutline, engineData._shaderPostOutlineCommon.c_str(), false );
			if ( psoOutline == 0 )
				psoOutline = createEnginePso( engineData._shaderPostOutlineEngine.c_str(), false );
			if ( psoOutline != 0 )
				_mapEnginePsos.insert_or_assign( PassType::kOutline, psoOutline );
		}

		const RHIPipelineStateHandle psoBlit =
			createPsoForPassType( PassType::kPresent, engineData._shaderFullscreenBlit.c_str(), false );
		if ( psoBlit != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kPresent, psoBlit );

		const RHIPipelineStateHandle psoSsao =
			createPsoForPassType( PassType::kSsao, engineData._shaderSsao.c_str(), false );
		if ( psoSsao != 0 )
		{
			_mapEnginePsos.insert_or_assign( PassType::kSsao, psoSsao );
			_mapEnginePsos.insert_or_assign( PassType::kHbao, psoSsao );
		}

		const RHIPipelineStateHandle psoTaa =
			createPsoForPassType( PassType::kTaa, engineData._shaderTaa.c_str(), false );
		if ( psoTaa != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kTaa, psoTaa );

		const RHIPipelineStateHandle psoTonemap =
			createPsoForPassType( PassType::kTonemap, engineData._shaderTonemap.c_str(), false );
		if ( psoTonemap != 0 )
			_mapEnginePsos.insert_or_assign( PassType::kTonemap, psoTonemap );

		// Compute PSO: GpuCull — capabilities + 실제 PSO 생성 성공 시에만 GPU-driven.
		const RHICapabilities caps = _pDevice->getCapabilities();
		if ( caps._bGpuCulling != 0 )
		{
			const RHIPipelineStateHandle psoGpuCull =
				_pDevice->getResource()->createComputePipelineState( engineData._shaderGpuCull.c_str(), Entry::kCSMain );
			if ( psoGpuCull != 0 )
				_mapEnginePsos.insert_or_assign( "GpuCull", psoGpuCull );
		}

		_bUseGpuDriven = ( caps._bIndirectDraw != 0 && caps._bGpuCulling != 0 && getEnginePso( "GpuCull" ) != 0 ) ? 1 : 0;

		_bPassResourcesReady = 1;
		SW_LOG_INFO( "[FrameRenderer] Pass PSOs/CB ready (shadow=%# forward=%# transparent=%# deferred=%# bloom=%# outline=%# gpuDriven=%#)",
					 getEnginePso( PassType::kShadow ), getEnginePso( PassType::kForwardOpaque ),
					 getEnginePso( PassType::kTransparent ), getEnginePso( PassType::kLighting ),
					 getEnginePso( PassType::kPostBloom ), getEnginePso( PassType::kOutline ), static_cast<uint32>( _bUseGpuDriven ) );
	}

	void FrameRenderer::releasePassResources()
	{
		if ( _pDevice == nullptr )
		{
			_passCb			= 0;
			_passCbIndex	= kInvalidDescriptorIndex;
			_gpuCullCb		= 0;
			_gpuCullCbIndex = kInvalidDescriptorIndex;
			_taaHistory		= 0;
			_taaHistorySrv	= kInvalidDescriptorIndex;
			_mapEnginePsos.clear();
			_mapMaterialPassPsos.clear();
			_bPassResourcesReady = 0;
			return;
		}

		for ( auto& [name, pso] : _mapEnginePsos )
		{
			if ( pso != 0 )
			{
				_pDevice->getResource()->destroyPipelineState( pso );
				pso = 0;
			}
		}
		_mapEnginePsos.clear();

		for ( auto& [pass, pso] : _mapMaterialPassPsos )
		{
			if ( pso != 0 )
			{
				_pDevice->getResource()->destroyPipelineState( pso );
				pso = 0;
			}
		}
		_mapMaterialPassPsos.clear();
		_gpuScene.releaseGpu( _pDevice );

		if ( _passCbIndex != kInvalidDescriptorIndex )
		{
			_pDevice->getResource()->unregisterBindlessResource( _passCbIndex );
			_passCbIndex = kInvalidDescriptorIndex;
		}
		if ( _passCb != 0 )
		{
			_pDevice->getResource()->destroyBuffer( _passCb );
			_passCb = 0;
		}
		if ( _taaHistorySrv != kInvalidDescriptorIndex )
		{
			_pDevice->getResource()->unregisterBindlessResource( _taaHistorySrv );
			_taaHistorySrv = kInvalidDescriptorIndex;
		}
		if ( _taaHistory != 0 )
		{
			_pDevice->getResource()->destroyTexture( _taaHistory );
			_taaHistory = 0;
		}
		if ( _gpuCullCbIndex != kInvalidDescriptorIndex )
		{
			_pDevice->getResource()->unregisterBindlessResource( _gpuCullCbIndex );
			_gpuCullCbIndex = kInvalidDescriptorIndex;
		}
		if ( _gpuCullCb != 0 )
		{
			_pDevice->getResource()->destroyBuffer( _gpuCullCb );
			_gpuCullCb = 0;
		}
		_bPassResourcesReady = 0;
	}

	void FrameRenderer::ensureTransientResources( uint32 overrideWidth, uint32 overrideHeight )
	{
		if ( _pDevice == nullptr )
			return;

		uint32 width  = kDefaultTransientSize;
		uint32 height = kDefaultTransientSize;
		if ( overrideWidth > 0 && overrideHeight > 0 )
		{
			width  = overrideWidth;
			height = overrideHeight;
		}
		else
		{
			IWindow* pWindow = IWindow::getActiveWindow();
			if ( pWindow != nullptr )
			{
				if ( pWindow->getWidth() > 0 )
					width = pWindow->getWidth();
				if ( pWindow->getHeight() > 0 )
					height = pWindow->getHeight();
			}
		}

		if ( width == _transientWidth && height == _transientHeight && _mapTransients.empty() == false )
			return;

		releaseTransientResources();
		_transientWidth	 = width;
		_transientHeight = height;

		for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachments )
		{
			const RHIFormat format = parseAttachmentFormat( att._format );
			allocTransient( att._name, format, isDepthFormat( format ), att._arrClearColor );
		}

		auto ensureNamed = [&]( string_view name )
		{
			const string key( name );
			if ( _mapTransients.find( key ) != _mapTransients.end() || name == Attachment::kSwapchain )
				return;

			float32	   clearColor[4]{};
			const bool bHasClear = tryGetAttachmentClearColor( name, clearColor );

			if ( name == Attachment::kShadowMap || name == Attachment::kSceneDepth )
				allocTransient( name, RHIFormat::D24_UNORM_S8_UINT, true, bHasClear ? clearColor : kDepthClear );
			else if ( name == Attachment::kGBufferNormal || name == Attachment::kLitColor || name == Attachment::kBloomColor || name == Attachment::kBloomBright )
				allocTransient( name, RHIFormat::R16G16B16A16_FLOAT, false, bHasClear ? clearColor : kBloomClear );
			else if ( name == Attachment::kSceneColor )
				allocTransient( name, RHIFormat::R8G8B8A8_UNORM, false, bHasClear ? clearColor : kSceneClear );
			else
				allocTransient( name, RHIFormat::R8G8B8A8_UNORM, false, bHasClear ? clearColor : kBlackClear );
		};

		for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPasses() )
		{
			for ( const string& in : pass._listInputs )
			{
				ensureNamed( in );
			}
			for ( const string& out : pass._listOutputs )
			{
				ensureNamed( out );
			}
		}
	}

	void FrameRenderer::releaseTransientResources()
	{
		if ( _pDevice == nullptr )
		{
			_mapTransients.clear();
			_mapTransientSrvs.clear();
			_transientWidth	 = 0;
			_transientHeight = 0;
			return;
		}
		for ( auto& [name, srv] : _mapTransientSrvs )
		{
			if ( srv != kInvalidDescriptorIndex )
				_pDevice->getResource()->unregisterBindlessResource( srv );
		}
		for ( auto& [name, tex] : _mapTransients )
		{
			if ( tex != 0 )
				_pDevice->getResource()->destroyTexture( tex );
		}
		_mapTransients.clear();
		_mapTransientSrvs.clear();
		_transientWidth	 = 0;
		_transientHeight = 0;
	}

	void FrameRenderer::allocTransient( string_view name, RHIFormat format, bool bDepth, const float32 clearColor[4] )
	{
		if ( _mapTransients.find( string( name ) ) != _mapTransients.end() || _pDevice == nullptr )
			return;

		RHITextureDesc desc{};
		desc._width				= _transientWidth;
		desc._height			= _transientHeight;
		desc._format			= format;
		desc._bIsRenderTarget	= bDepth ? 0 : 1;
		desc._bIsDepthStencil	= bDepth ? 1 : 0;
		desc._bIsShaderResource = 1;
		desc._clearDepth		= clearColor[0];
		Memory::copy( desc._arrClearColor, clearColor, sizeof( desc._arrClearColor ) );
		const RHITextureHandle handle = _pDevice->getResource()->createTexture2D( desc );
		if ( handle == 0 )
		{
			SW_LOG_WARNING( "[FrameRenderer] Failed to allocate transient '%#'", name );
			return;
		}
		_mapTransients.emplace( name, handle );
		const RHIDescriptorIndex srv = _pDevice->getResource()->registerBindlessTexture( handle );
		if ( srv != kInvalidDescriptorIndex )
			_mapTransientSrvs.emplace( name, srv );
	}

	bool FrameRenderer::tryGetAttachmentClearColor( string_view attachmentName, float32 outClearColor[4] ) const
	{
		for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachments )
		{
			if ( att._name == attachmentName )
			{
				Memory::copy( outClearColor, att._arrClearColor, sizeof( att._arrClearColor ) );
				return att._bClear;
			}
		}
		return false;
	}

	RHITextureHandle FrameRenderer::findTransient( string_view name ) const
	{
		const auto it = _mapTransients.find( string( name ) );
		return it != _mapTransients.end() ? it->second : 0;
	}

	RHIDescriptorIndex FrameRenderer::findTransientSrv( string_view name ) const
	{
		const auto it = _mapTransientSrvs.find( string( name ) );
		return it != _mapTransientSrvs.end() ? it->second : kInvalidDescriptorIndex;
	}

	RHIFormat FrameRenderer::parseAttachmentFormat( string_view formatName ) const
	{
		const string formatNt( formatName );
		const string f = StringUtil::toUpper( formatNt.c_str() );
		if ( f == "D24_UNORM_S8_UINT" || f == "D24S8" )
			return RHIFormat::D24_UNORM_S8_UINT;
		if ( f == "R16G16B16A16_FLOAT" )
			return RHIFormat::R16G16B16A16_FLOAT;
		if ( f == "B8G8R8A8_UNORM" )
			return RHIFormat::B8G8R8A8_UNORM;
		return RHIFormat::R8G8B8A8_UNORM;
	}

	string FrameRenderer::resolvePresentSource() const
	{
		const utf8* pName = pickFirstExisting(
			_mapTransients,
			{ "TonemapColor", "OutlineColor", "BloomColor", "TaaColor",
			  "TransparentColor", "LitColor", "SceneColor", "GBufferAlbedo" } );
		return pName != nullptr ? string( pName ) : string{};
	}

	RHIPipelineStateHandle FrameRenderer::getEnginePso( string_view passType ) const
	{
		unordered_map<string, RHIPipelineStateHandle>::const_iterator it =
			_mapEnginePsos.find( string( passType ) );
		return ( it != _mapEnginePsos.end() ) ? it->second : 0;
	}
} // namespace sw
