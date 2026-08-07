/**
 * @file RenderPassManager.cpp
 * @brief 렌더 패스 매니저 구현
 */
#include "RenderPassManager.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
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

	RenderPassResource* RenderPassManager::loadRenderPass( const std::string_view assetRelativePath )
	{
		auto res = std::make_unique<RenderPassResource>();
		if ( !res->loadFromXmlFile( std::string{ assetRelativePath } ) )
		{
			return nullptr;
		}

		hashed_string		key( res->getDesc()._name.c_str() );
		RenderPassResource* ptr = res.get();
		_renderPassMap.try_emplace( key, std::move( res ) );
		return ptr;
	}

	RenderPassResource* RenderPassManager::findRenderPass( hashed_string name )
	{
		auto it = _renderPassMap.find( name );
		if ( it != _renderPassMap.end() )
		{
			return it->second.get();
		}
		return nullptr;
	}

	void RenderPassManager::clearCache()
	{
		_renderPassMap.clear();
	}
} // namespace sw
