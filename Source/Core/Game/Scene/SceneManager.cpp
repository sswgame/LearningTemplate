#include "SceneManager.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	bool SceneManager::initialize()
	{
		SW_LOG_INFO( "[SceneManager] Initialized." );
		return true;
	}

	void SceneManager::shutdown()
	{
		_loadedScenes.clear();
		_activeScene = nullptr;
		SW_LOG_INFO( "[SceneManager] Shut down." );
	}

	Scene* SceneManager::createScene( const std::string& name )
	{
		auto scene = std::make_unique<Scene>( name );
		Scene* scenePtr = scene.get();

		_loadedScenes.push_back( std::move( scene ) );

		if ( _activeScene == nullptr )
		{
			_activeScene = scenePtr;
		}

		return scenePtr;
	}

	void SceneManager::update( float32 deltaTime )
	{
		for ( auto& scene : _loadedScenes )
		{
			if ( scene )
			{
				scene->update( deltaTime );
			}
		}
	}
}
