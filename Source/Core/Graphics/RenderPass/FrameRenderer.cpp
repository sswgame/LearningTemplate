/**
 * @file FrameRenderer.cpp
 */
#include "FrameRenderer.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/RHI/RHITypes.h"
#include "Core/Graphics/Material/Material.h"
#include "Core/Graphics/RenderPass/RenderPassManager.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	namespace
	{
		constexpr float32 kDefaultDepthClearColor[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

		RHIDescriptorIndex resolveMaterialIndex( Material* material )
		{
			return material != nullptr ? material->getDescriptorIndex() : 0;
		}
	} // namespace
	FrameRenderer::FrameRenderer()
		: _bReady{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	bool FrameRenderer::initialize( IRHIDevice* device, const std::string& pipelineXmlPath )
	{
		_device = device;
		if ( device == nullptr )
		{
			SW_LOG_ERROR( "[FrameRenderer] initialize: null IRHIDevice" );
			return false;
		}

		RenderPassManager& rpm = device->getRenderPassManager();
		if ( rpm.findRenderPass( hashed_string( "DefaultMainPass" ) ) == nullptr )
			rpm.loadRenderPass( "Engine/RenderPass/DefaultRenderPass.xml" );

		if ( loadPipeline( pipelineXmlPath ) == false )
		{
			SW_LOG_WARNING( "[FrameRenderer] Pipeline load failed — FrameRenderer not ready (Scene will fallback)." );
			_bReady = 0;
			return false;
		}

		_bReady = 1;
		SW_LOG_INFO( "[FrameRenderer] Ready with pipeline '%#'", _pipelinePath );
		return true;
	}

	void FrameRenderer::shutdown()
	{
		_graph.clear();
		_device		  = nullptr;
		_bReady		  = 0;
		_pipelinePath.clear();
		SW_LOG_INFO( "[FrameRenderer] Shut down." );
	}

	bool FrameRenderer::loadPipeline( const std::string& pipelineXmlPath )
	{
		_pipelinePath = pipelineXmlPath;
		_graph.clear();

		if ( _pipelineResource.loadFromXmlFile( pipelineXmlPath ) == false )
			return false;

		if ( _device != nullptr )
			_device->getRenderPassManager().loadRenderPass( pipelineXmlPath );

		const std::vector<RenderGraphPassDesc>& passes = _pipelineResource.getGraphPasses();
		if ( passes.empty() )
		{
			SW_LOG_WARNING( "[FrameRenderer] No graph passes in '%#'", pipelineXmlPath );
			return false;
		}

		for ( const RenderGraphPassDesc& pass : passes )
		{
			std::vector<hashed_string> inputs;
			std::vector<hashed_string> outputs;
			inputs.reserve( pass._inputs.size() );
			outputs.reserve( pass._outputs.size() );
			for ( const std::string& in : pass._inputs )
				inputs.emplace_back( in.c_str() );
			for ( const std::string& out : pass._outputs )
				outputs.emplace_back( out.c_str() );

			_graph.addPass( hashed_string( pass._name.c_str() ), std::move( inputs ), std::move( outputs ) );
		}

		if ( _graph.compile() == false )
		{
			SW_LOG_ERROR( "[FrameRenderer] RenderGraph compile failed for '%#'", pipelineXmlPath );
			return false;
		}

		float32 sceneColorClear[4];
		if ( tryGetAttachmentClearColor( "SceneColor", sceneColorClear ) )
			std::memcpy( _clearColor, sceneColorClear, sizeof( _clearColor ) );

		SW_LOG_INFO( "[FrameRenderer] Built graph '%#' (%# passes)",
					 _pipelineResource.getDesc()._name, passes.size() );
		return true;
	}

	bool FrameRenderer::tryGetAttachmentClearColor( const std::string& attachmentName, float32 outClearColor[4] ) const
	{
		for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._attachments )
		{
			if ( att._name == attachmentName )
			{
				std::memcpy( outClearColor, att._clearColor, sizeof( att._clearColor ) );
				return att._bClear;
			}
		}
		return false;
	}

	void FrameRenderer::executePass( const std::string& passType, const std::string& passName, Material* material )
	{
		_device->beginEventMarker( passName.c_str() );

		const RHIDescriptorIndex matIdx = resolveMaterialIndex( material );
		float32					 clearColor[4];

		auto clearAndDraw = [this, matIdx]( const float32 color[4], bool bDraw )
		{
			RHIRenderPassBeginInfo beginInfo{};
			std::memcpy( beginInfo._clearColor, color, sizeof( beginInfo._clearColor ) );
			_device->beginRenderPass( beginInfo );
			if ( bDraw )
				_device->drawTriangle( matIdx );
			_device->endRenderPass();
		};

		if ( passType == "Shadow" )
		{
			if ( tryGetAttachmentClearColor( "SceneDepth", clearColor ) == false )
				std::memcpy( clearColor, kDefaultDepthClearColor, sizeof( clearColor ) );
			clearAndDraw( clearColor, true );
		}
		else if ( passType == "ForwardOpaque" )
		{
			if ( tryGetAttachmentClearColor( "SceneColor", clearColor ) == false )
				std::memcpy( clearColor, _clearColor, sizeof( clearColor ) );
			clearAndDraw( clearColor, true );
		}
		else if ( passType == "GBuffer" )
		{
			if ( tryGetAttachmentClearColor( "GBufferAlbedo", clearColor ) == false )
				std::memcpy( clearColor, _clearColor, sizeof( clearColor ) );
			clearAndDraw( clearColor, true );
		}
		else if ( passType == "Lighting" || passType == "Shading" )
		{
			// Deferred / forward lighting pass — marker only until full-screen shading is wired.
		}
		else if ( passType == "Transparent" )
		{
			_device->drawTriangle( matIdx );
		}
		else if ( passType == "PostBloom" || passType == "Post" )
		{
			// Post-process stub — marker only until bloom/composite shaders are wired.
		}
		else if ( passType == "Present" )
		{
			_device->drawTriangle( matIdx );
		}
		else
		{
			SW_LOG_WARNING( "[FrameRenderer] Unknown pass type '%#' in '%#'", passType, passName );
		}

		_device->endEventMarker();
	}

	void FrameRenderer::bindPassCallbacks( Material* material )
	{
		const std::vector<RenderGraphPassDesc>& passes = _pipelineResource.getGraphPasses();
		_graph.clear();

		for ( const RenderGraphPassDesc& pass : passes )
		{
			std::vector<hashed_string> inputs;
			std::vector<hashed_string> outputs;
			for ( const std::string& in : pass._inputs )
				inputs.emplace_back( in.c_str() );
			for ( const std::string& out : pass._outputs )
				outputs.emplace_back( out.c_str() );

			const std::string passType = pass._type;
			const std::string passName = pass._name;

			RenderGraphPassExecuteFn execute = SW_DELEGATE_LAMBDA( RenderGraphPassExecuteFn,
																  [this, material, passType, passName]( const RenderGraphPassContext& ctx )
			{
				(void)ctx;
				if ( _device == nullptr )
					return;

				executePass( passType, passName, material );
			} );

			_graph.addPass( hashed_string( pass._name.c_str() ), std::move( inputs ), std::move( outputs ), std::move( execute ) );
		}

		if ( _graph.compile() == false )
			SW_LOG_ERROR( "[FrameRenderer] Rebind compile failed" );
	}

	bool FrameRenderer::execute( IRHIDevice* device, Material* material )
	{
		if ( _bReady == 0 || device == nullptr )
			return false;

		_device = device;
		bindPassCallbacks( material );
		return _graph.execute();
	}
} // namespace sw
