#include "SceneManager.h"
#include "Core/Common/CoreServices.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Task/TaskManager.h"

namespace sw
{
	bool SceneManager::initialize()
	{
		_bPendingReady	= false;
		_bLoadInFlight	= false;
		_pendingDescriptor = {};
		SW_LOG_INFO( "[SceneManager] Initialized." );
		return true;
	}

	void SceneManager::shutdown()
	{
		_loadedScenes.clear();
		_activeScene = nullptr;
		_bPendingReady = false;
		_bLoadInFlight = false;
		_pendingDescriptor = {};
		_loadHandle		   = {};
		SW_LOG_INFO( "[SceneManager] Shut down." );
	}

	Scene* SceneManager::createScene( const std::string& name )
	{
		auto   scene	= std::make_unique<Scene>( name );
		Scene* scenePtr = scene.get();

		_loadedScenes.push_back( std::move( scene ) );

		if ( _activeScene == nullptr )
		{
			_activeScene = scenePtr;
		}

		return scenePtr;
	}

	bool SceneManager::requestLoadAsync( const std::string& path )
	{
		if ( path.empty() )
		{
			SW_LOG_WARNING( "[SceneManager] requestLoadAsync: empty path" );
			return false;
		}

		bool expected = false;
		if ( _bLoadInFlight.compare_exchange_strong( expected, true ) == false )
		{
			SW_LOG_WARNING( "[SceneManager] Async load already in flight — ignoring '%#'", path );
			return false;
		}

		_bPendingReady = false;
		SW_LOG_INFO( "[SceneManager] requestLoadAsync: %#", path );

		_loadHandle = core::getTaskManager().emplaceTask(
			"SceneLoadAsync",
			SW_DELEGATE_LAMBDA( TaskDelegate, [this, path]()
			{
				SceneDescriptor desc{};
				const bool		ok = loadSceneDescriptorFromXml( path, desc );
				{
					std::lock_guard<std::mutex> lock( _pendingMutex );
					_pendingDescriptor = ok ? desc : SceneDescriptor{};
					if ( ok == false )
						_pendingDescriptor._sourcePath = path;
					_pendingDescriptor._bValid = ok;
				}
				_bPendingReady.store( true, std::memory_order_release );
			} ) );

		core::getTaskManager().dispatch();
		return _loadHandle.isValid();
	}

	void SceneManager::applyPendingDescriptor( const SceneDescriptor& desc )
	{
		if ( desc._bValid == false )
		{
			SW_LOG_ERROR( "[SceneManager] Async load failed for '%#'", desc._sourcePath );
			return;
		}

		Scene* scene = createScene( desc._name.empty() ? "LoadedScene" : desc._name );
		if ( scene == nullptr )
			return;

		_activeScene = scene;
		if ( _rhiDevice != nullptr )
			scene->initialize( _rhiDevice );
		if ( _frameRenderer != nullptr )
			scene->setFrameRenderer( _frameRenderer );

		if ( GameObjectManager* objects = scene->getObjectManager() )
		{
			for ( const SceneEntityPlaceholder& ent : desc._entities )
			{
				GameObject* go = objects->createGameObject( hashed_string( ent._name.c_str() ) );
				if ( go != nullptr && ent._prefab.empty() == false )
					SW_LOG_INFO( "[SceneManager] Placeholder '%#' (prefab=%#) — spawn deferred",
								 ent._name, ent._prefab );
				(void)go;
			}
		}

		SW_LOG_INFO( "[SceneManager] Active scene swapped to '%#' (%# placeholders)",
					 scene->getName(), desc._entities.size() );
	}

	void SceneManager::tickTransitions()
	{
		if ( _bPendingReady.load( std::memory_order_acquire ) == false )
			return;

		SceneDescriptor desc{};
		{
			std::lock_guard<std::mutex> lock( _pendingMutex );
			desc			   = _pendingDescriptor;
			_pendingDescriptor = {};
		}
		_bPendingReady.store( false, std::memory_order_release );
		_bLoadInFlight.store( false, std::memory_order_release );
		_loadHandle = {};

		applyPendingDescriptor( desc );
	}

	bool SceneManager::isTransitioning() const
	{
		return _bLoadInFlight.load( std::memory_order_acquire )
			   || _bPendingReady.load( std::memory_order_acquire );
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
} // namespace sw
