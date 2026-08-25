#include "pch.h"

#include "Engine/Graphics/RenderPass/RenderPassManager.h"

#include "Engine/Graphics/RenderPass/RenderPassResource.h"
#include "Engine/Graphics/RenderPass/RenderPipelineResource.h"

namespace sw
{
	RenderPassManager::RenderPassManager()	= default;
	RenderPassManager::~RenderPassManager() = default;

	bool RenderPassManager::initialize()
	{
		SW_LOG_INFO( "[RenderPassManager] Subsystem Initialized." );
		return true;
	}

	void RenderPassManager::shutdown()
	{
		clearCache();
		SW_LOG_INFO( "[RenderPassManager] Subsystem Shutdown Cleanly." );
	}

	RenderPassResource* RenderPassManager::loadRenderPass( string_view assetRelativePath )
	{
		if ( assetRelativePath.empty() )
			return nullptr;

		unique_ptr<RenderPassResource> res = make_unique<RenderPassResource>();
		if ( res->loadFromXmlFile( string{ assetRelativePath } ) == false )
			return nullptr;

		hashed_string		key( res->getDesc()._name.c_str() );
		RenderPassResource* pExisting = findRenderPass( key );
		if ( pExisting != nullptr )
			return pExisting;

		RenderPassResource* ptr = res.get();
		_mapRenderPasses.try_emplace( key, std::move( res ) );
		return ptr;
	}

	RenderPipelineResource* RenderPassManager::loadPipeline( string_view assetRelativePath )
	{
		if ( assetRelativePath.empty() )
			return nullptr;

		unique_ptr<RenderPipelineResource> res = make_unique<RenderPipelineResource>();
		if ( res->loadFromXmlFile( string{ assetRelativePath } ) == false )
			return nullptr;

		hashed_string			key( res->getDesc()._name.c_str() );
		RenderPipelineResource* pExisting = findPipeline( key );
		if ( pExisting != nullptr )
			return pExisting;

		RenderPipelineResource* ptr = res.get();
		_mapPipelines.try_emplace( key, std::move( res ) );
		return ptr;
	}

	void RenderPassManager::clearCache()
	{
		_mapPipelines.clear();
		_mapRenderPasses.clear();
	}

	RenderPassResource* RenderPassManager::findRenderPass( hashed_string name )
	{
		auto it = _mapRenderPasses.find( name );
		if ( it != _mapRenderPasses.end() )
			return it->second.get();
		return nullptr;
	}

	RenderPipelineResource* RenderPassManager::findPipeline( hashed_string name )
	{
		auto it = _mapPipelines.find( name );
		if ( it != _mapPipelines.end() )
			return it->second.get();
		return nullptr;
	}
} // namespace sw
