#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
	namespace
	{
		mutex& getModuleScriptSystemMutex()
		{
			static mutex s_mutex;
			return s_mutex;
		}

		unordered_map<string, ScriptSystemRegistrar*>& getModuleScriptSystemHeads()
		{
			static unordered_map<string, ScriptSystemRegistrar*> s_mapScriptSystemHeads;
			return s_mapScriptSystemHeads;
		}
	} // namespace

	static ScriptSystemRegistrar* _s_engineScriptHead{ nullptr };
	static bool					  _s_engineScriptHeadSealed{ false };

	ScriptSystemRegistrar*& ScriptSystemRegistrar::getHead()
	{
		static ScriptSystemRegistrar* s_head{ nullptr };
		return s_head;
	}

	ScriptSystemRegistrar::ScriptSystemRegistrar( RegisterFunc registerFunc )
		: ScriptSystemRegistrar( registerFunc, getHead() )
	{
	}

	ScriptSystemRegistrar::ScriptSystemRegistrar( RegisterFunc registerFunc, ScriptSystemRegistrar*& moduleHead )
		: _registerFunc{ registerFunc }
		, _pNext{ nullptr }
	{
		_pNext	   = moduleHead;
		moduleHead = this;
		if ( _s_engineScriptHeadSealed == false && &moduleHead == &getHead() )
			_s_engineScriptHead = this;
	}

	/**
	 * @brief GameObjectManager 생성자: 기본 엔진 및 모듈 스크립트 시스템들을 등록합니다.
	 */
	GameObjectManager::GameObjectChunk::GameObjectChunk()
		: _pMemory{ static_cast<GameObject*>( sw::Memory::allocMemory( sizeof( GameObject ) * kChunkSize ) ) }
	{
	}

	GameObjectManager::GameObjectChunk::~GameObjectChunk()
	{
		sw::Memory::freeMemory( _pMemory );
	}

	GameObjectManager::GameObjectManager()
		: _listGoChunks{}
		, _listGoFree{}
		, _listGameObjects{}
		, _mapNameToObject{}
		, _mapIdToObject{}
		, _mapEntityToObject{}
		, _listPendingAdds{}
		, _listPendingDestroyObjects{}
		, _listPendingDestroyComponents{}
		, _listProcessingDestroyObjects{}
		, _listProcessingDestroyComponents{}
		, _listRootSceneEntities{}
		, _mutex{}
		, _nextId{ 1 }
		, _registry{}
		, _physicsWorld{}
		, _scriptSystemTick{}
		, _scriptSystemBeginPlay{}
		, _scriptSystemEndPlay{}
		, _bParallelTransformReadOnly{ false }
		, _deferredTransformMutex{}
		, _listDeferredTransformUpdates{}
		, _listProcessingTransforms{}
		, _deferredPostTickMutex{}
		, _listDeferredPostTickUpdates{}
		, _listProcessingPostTicks{}
		, _dirtyTransformGeneration{ 1 }
		, _lastFlushedTransformGeneration{ 0 }
	{
		_listDeferredTransformUpdates.reserve( 128 );
		_listProcessingTransforms.reserve( 128 );
		_listDeferredPostTickUpdates.reserve( 128 );
		_listProcessingPostTicks.reserve( 128 );

		registerPendingScriptSystems( "Engine", ScriptSystemRegistrar::getHead() );
		{
			std::scoped_lock<mutex> lock{ getModuleScriptSystemMutex() };
			for ( const auto& [mod, head] : getModuleScriptSystemHeads() )
			{
				if ( head != nullptr )
					registerPendingScriptSystems( mod.c_str(), head );
			}
		}
	}

	GameObjectManager::~GameObjectManager()
	{
		clear();
	}

	/**
	 * @brief 새로운 게임 오브젝트를 생성하고 고유 이름을 부여하며 인덱스 사전에 등록합니다.
	 */
	GameObject* GameObjectManager::createGameObject( hashed_string name )
	{
		GameObject* pObj = nullptr;
		{
			std::unique_lock<std::shared_mutex> lock{ _mutex };
			if ( _listGoFree.empty() )
			{
				auto chunk = make_unique<GameObjectChunk>();
				for ( size_t slotIndex = 0; slotIndex < GameObjectChunk::kChunkSize; ++slotIndex )
				{
					_listGoFree.push_back( &chunk->_pMemory[GameObjectChunk::kChunkSize - 1 - slotIndex] );
				}
				_listGoChunks.push_back( std::move( chunk ) );
			}
			pObj = _listGoFree.back();
			_listGoFree.pop_back();

			const hashed_string uniqueName = makeUniqueNameUnlocked( name );
			sw_placement_new( pObj ) GameObject( uniqueName );

			const uint64 id		 = generateNewId();
			pObj->_objectId		 = id;
			pObj->_pOwnerManager = this;
			pObj->_entityId		 = _registry.create();
			_registry.emplace<EntityStateData>( pObj->_entityId );

			_mapNameToObject.insert_or_assign( uniqueName, pObj );
			_mapIdToObject.insert_or_assign( id, pObj );
			_mapEntityToObject.insert_or_assign( pObj->_entityId, pObj );

			_listPendingAdds.push_back( pObj );
		}
		return pObj;
	}

	/**
	 * @brief 게임 오브젝트 이름 변경 시 이름 검색 맵을 갱신합니다.
	 */
	void GameObjectManager::notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName )
	{
		if ( pObj == nullptr )
			return;

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		if ( _mapIdToObject.find( pObj->getObjectId() ) == _mapIdToObject.end() )
			return;

		const auto oldIt = _mapNameToObject.find( oldName );
		if ( oldIt != _mapNameToObject.end() && oldIt->second == pObj )
			_mapNameToObject.erase( oldIt );

		const hashed_string uniqueName = makeUniqueNameUnlocked( newName );
		if ( uniqueName != newName )
			pObj->_name = uniqueName;
		_mapNameToObject.insert_or_assign( uniqueName, pObj );
	}

	void GameObjectManager::notifyEntityCreated( GameObject* pObj )
	{
		if ( pObj == nullptr || pObj->getEntityId() == sw::kNullEntity )
			return;

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		_mapEntityToObject[pObj->getEntityId()] = pObj;
	}

	bool GameObjectManager::renameGameObject( GameObject* pObj, hashed_string newName )
	{
		if ( pObj == nullptr )
			return false;
		pObj->setName( newName );
		return true;
	}

	GameObject* GameObjectManager::findGameObjectByName( hashed_string name ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		auto								it = _mapNameToObject.find( name );
		if ( it == _mapNameToObject.end() )
			return nullptr;
		GameObject* pObj = it->second;
		return ( pObj != nullptr && pObj->isPendingKill() == false ) ? pObj : nullptr;
	}

	GameObject* GameObjectManager::findGameObjectById( uint64 objectId ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		auto								it = _mapIdToObject.find( objectId );
		if ( it == _mapIdToObject.end() )
			return nullptr;
		GameObject* pObj = it->second;
		return ( pObj != nullptr && pObj->isPendingKill() == false ) ? pObj : nullptr;
	}

	GameObject* GameObjectManager::findGameObjectByEntity( sw::Entity entityId ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		auto								it = _mapEntityToObject.find( entityId );
		if ( it == _mapEntityToObject.end() )
			return nullptr;
		GameObject* pObj = it->second;
		return ( pObj != nullptr && pObj->isPendingKill() == false ) ? pObj : nullptr;
	}

	vector<GameObject*> GameObjectManager::getAllGameObjects() const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		vector<GameObject*>					listOut;
		listOut.reserve( _listGameObjects.size() + _listPendingAdds.size() );
		listOut.insert( listOut.end(), _listGameObjects.begin(), _listGameObjects.end() );
		listOut.insert( listOut.end(), _listPendingAdds.begin(), _listPendingAdds.end() );
		return listOut;
	}

	GameObject* GameObjectManager::findGameObjectByTag( TagID tag ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		for ( GameObject* pObj : _listGameObjects )
		{
			if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
				return pObj;
		}
		for ( GameObject* pObj : _listPendingAdds )
		{
			if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
				return pObj;
		}
		return nullptr;
	}

	void GameObjectManager::findGameObjectsByTag( TagID tag, vector<GameObject*>& out ) const
	{
		out.clear();
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		for ( GameObject* pObj : _listGameObjects )
		{
			if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
				out.push_back( pObj );
		}
		for ( GameObject* pObj : _listPendingAdds )
		{
			if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
				out.push_back( pObj );
		}
	}

	void GameObjectManager::beginPlay()
	{
		mergePendingAdds();
		_scriptSystemBeginPlay.broadcast( this, _registry );
	}

	void GameObjectManager::endPlay()
	{
		_scriptSystemEndPlay.broadcast( this, _registry );
	}

	void GameObjectManager::tick( float32 deltaTime )
	{
		if ( engine::areEngineServicesBound() )
			engine::getTaskManager().dispatchMainThreadTasks();
		processDeferredDestruction();
		mergePendingAdds();

		if ( _listGameObjects.empty() )
			return;

		// Pass 1: publish a stable world-transform snapshot for this frame (dirty subtrees only).
		flushSceneTransforms();

		// Pass 2: parallel logic ticks via ECS Batch Tick + script systems.
		// Keep _bTicking true across both so packed-set erase stays deferred.
		_bParallelTransformReadOnly.store( true, std::memory_order_relaxed );

		_registry.beginTick();
		_registry.tickComponents( deltaTime );
		_scriptSystemTick.broadcast( this, _registry, deltaTime, true );

		if ( engine::areEngineServicesBound() )
			engine::getTaskManager().waitAll();

		_bParallelTransformReadOnly.store( false, std::memory_order_relaxed );
		_registry.finishTick();

		// Apply deferred transforms while instances still exist (CommandBuffer not flushed yet).
		{
			std::scoped_lock<mutex> lock{ _deferredTransformMutex };
			if ( _listDeferredTransformUpdates.empty() == false )
				_listProcessingTransforms.swap( _listDeferredTransformUpdates );
		}
		for ( auto& func : _listProcessingTransforms )
		{
			if ( func.isBound() )
				func();
		}
		_listProcessingTransforms.clear();

		// Spawns / damage / tags queued from parallel onTick.
		{
			std::scoped_lock<mutex> lock{ _deferredPostTickMutex };
			if ( _listDeferredPostTickUpdates.empty() == false )
				_listProcessingPostTicks.swap( _listDeferredPostTickUpdates );
		}
		for ( auto& func : _listProcessingPostTicks )
		{
			if ( func.isBound() )
				func();
		}
		_listProcessingPostTicks.clear();
		mergePendingAdds();

		_registry.flushCommands();

		// Pass 3: only re-flush if ticks dirtied any transform.
		if ( hasDirtySceneTransforms() )
			flushSceneTransforms();

		processDeferredDestruction();
	}

	void GameObjectManager::flushSceneTransforms()
	{
		const uint64 currentGen = _dirtyTransformGeneration.load( std::memory_order_relaxed );
		if ( currentGen == _lastFlushedTransformGeneration )
			return;

		std::shared_lock<std::shared_mutex> lock{ _mutex };
		for ( sw::Entity rootEnt : _listRootSceneEntities )
		{
			if ( rootEnt == sw::kNullEntity )
				continue;
			SceneComponent* pRoot = _registry.getPtr<SceneComponent>( rootEnt );
			if ( pRoot != nullptr )
				flushSceneComponentSubtree( pRoot, false );
		}
		_lastFlushedTransformGeneration = currentGen;
	}

	bool GameObjectManager::hasDirtySceneTransforms() const
	{
		if ( _dirtyTransformGeneration.load( std::memory_order_relaxed ) == _lastFlushedTransformGeneration )
			return false;

		std::shared_lock<std::shared_mutex> lock{ _mutex };
		for ( sw::Entity rootEnt : _listRootSceneEntities )
		{
			if ( rootEnt == sw::kNullEntity )
				continue;
			const SceneComponent* pRoot = _registry.getPtr<SceneComponent>( rootEnt );
			if ( pRoot != nullptr )
			{
				if ( pRoot->isTransformDirty() || pRoot->hasDirtyDescendant() )
					return true;
			}
		}
		return false;
	}

	void GameObjectManager::deferTransformUpdate( TransformUpdateDelegate func )
	{
		if ( func.isBound() == false )
			return;
		std::scoped_lock<mutex> lock{ _deferredTransformMutex };
		_listDeferredTransformUpdates.push_back( std::move( func ) );
	}

	void GameObjectManager::deferPostTick( PostTickDelegate func )
	{
		if ( func.isBound() == false )
			return;
		std::scoped_lock<mutex> lock{ _deferredPostTickMutex };
		_listDeferredPostTickUpdates.push_back( std::move( func ) );
	}

	void GameObjectManager::executeOrDeferPostTick( PostTickDelegate func )
	{
		if ( func.isBound() == false )
			return;
		if ( isStructuralMutationFrozen() )
			deferPostTick( std::move( func ) );
		else
			func();
	}

	void GameObjectManager::registerRootSceneComponent( SceneComponent* pComp )
	{
		if ( pComp == nullptr || pComp->getOwner() == nullptr )
			return;
		sw::Entity							ent = pComp->getOwner()->getEntityId();
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		auto								it = std::find( _listRootSceneEntities.begin(), _listRootSceneEntities.end(), ent );
		if ( it == _listRootSceneEntities.end() )
			_listRootSceneEntities.push_back( ent );
	}

	void GameObjectManager::unregisterRootSceneComponent( SceneComponent* pComp )
	{
		if ( pComp == nullptr || pComp->getOwner() == nullptr )
			return;
		sw::Entity							ent = pComp->getOwner()->getEntityId();
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		auto								it = std::find( _listRootSceneEntities.begin(), _listRootSceneEntities.end(), ent );
		if ( it != _listRootSceneEntities.end() )
		{
			*it = _listRootSceneEntities.back();
			_listRootSceneEntities.pop_back();
		}
	}

	Component* GameObjectManager::resolveComponent( sw::ComponentHandle handle )
	{
		return _registry.resolve( handle );
	}

	void GameObjectManager::destroyObject( GameObject* pObj, bool bDestroyChildren )
	{
		if ( pObj != nullptr && pObj->isPendingKill() == false )
		{
			pObj->markPendingKill();
			for ( Component* pComp : pObj->getAllComponents() )
			{
				pComp->markPendingKill();
			}
			pObj->refreshActiveInHierarchy();

			if ( bDestroyChildren )
			{
				for ( GameObject* pChild : pObj->getChildren() )
				{
					if ( pChild != nullptr && pChild->isPendingKill() == false )
						destroyObject( pChild, true );
				}
			}

			{
				std::unique_lock<std::shared_mutex> lock{ _mutex };
				_listPendingDestroyObjects.push_back( pObj );
			}
		}
	}

	void GameObjectManager::destroyComponent( Component* pComp )
	{
		if ( pComp != nullptr && pComp->isPendingKill() == false )
		{
			pComp->markPendingKill();
			const sw::ComponentHandle handle = pComp->getHandle();

			std::unique_lock<std::shared_mutex> lock{ _mutex };
			_listPendingDestroyComponents.push_back( handle );
		}
	}

	void GameObjectManager::processDeferredDestruction()
	{
		{
			std::unique_lock<std::shared_mutex> lock{ _mutex };
			if ( _listPendingDestroyObjects.empty() && _listPendingDestroyComponents.empty() )
				return;

			_listProcessingDestroyObjects.swap( _listPendingDestroyObjects );
			_listProcessingDestroyComponents.swap( _listPendingDestroyComponents );
		}

		for ( const sw::ComponentHandle handle : _listProcessingDestroyComponents )
		{
			Component* pComp = _registry.resolve( handle, true );
			if ( pComp == nullptr )
				continue;
			GameObject* pOwner = pComp->getOwner();
			if ( pOwner != nullptr )
				pOwner->removeComponent( pComp );
			else
				SW_LOG_WARNING( "[GameObjectManager] Deferred component destroy with no owner; skipping (ECS pool owns the instance)." );
		}

		if ( _listProcessingDestroyObjects.empty() == false )
		{
			std::unique_lock<std::shared_mutex> lock{ _mutex };
			for ( GameObject* pObj : _listProcessingDestroyObjects )
			{
				if ( pObj != nullptr )
				{
					auto pendingIt = std::find( _listPendingAdds.begin(), _listPendingAdds.end(), pObj );
					if ( pendingIt != _listPendingAdds.end() )
					{
						*pendingIt = _listPendingAdds.back();
						_listPendingAdds.pop_back();
					}

					uint32 idx = pObj->_managerIndex;
					if ( idx < _listGameObjects.size() && _listGameObjects[idx] == pObj )
					{
						GameObject* pBackObj	= _listGameObjects.back();
						_listGameObjects[idx]	= pBackObj;
						pBackObj->_managerIndex = idx;
						_listGameObjects.pop_back();
					}
					_mapNameToObject.erase( pObj->getName() );
					_mapIdToObject.erase( pObj->getObjectId() );
					if ( pObj->getEntityId() != sw::kNullEntity )
						_mapEntityToObject.erase( pObj->getEntityId() );
				}
			}
		}

		vector<GameObject*> listDying;
		{
			std::unique_lock<std::shared_mutex> lock{ _mutex };
			for ( GameObject* pObj : _listProcessingDestroyObjects )
			{
				if ( pObj == nullptr )
					continue;
				listDying.push_back( pObj );
			}
			_listProcessingDestroyObjects.clear();
			_listProcessingDestroyComponents.clear();
		}

		for ( GameObject* pObj : listDying )
		{
			pObj->~GameObject();
		}

		{
			std::unique_lock<std::shared_mutex> lock{ _mutex };
			for ( GameObject* pObj : listDying )
			{
				_listGoFree.push_back( pObj );
			}
		}
	}

	void GameObjectManager::clear()
	{
		vector<GameObject*> listDying;
		{
			std::unique_lock<std::shared_mutex> lock{ _mutex };

			listDying.reserve( _listGameObjects.size() + _listPendingAdds.size() );
			listDying.insert( listDying.end(), _listGameObjects.begin(), _listGameObjects.end() );
			listDying.insert( listDying.end(), _listPendingAdds.begin(), _listPendingAdds.end() );

			_listPendingDestroyObjects.clear();
			_listPendingDestroyComponents.clear();
			_listProcessingDestroyObjects.clear();
			_listProcessingDestroyComponents.clear();

			{
				std::scoped_lock<mutex> lockT{ _deferredTransformMutex };
				_listDeferredTransformUpdates.clear();
				_listProcessingTransforms.clear();
			}
			{
				std::scoped_lock<mutex> lockP{ _deferredPostTickMutex };
				_listDeferredPostTickUpdates.clear();
				_listProcessingPostTicks.clear();
			}

			_listGameObjects.clear();
			_listPendingAdds.clear();
			_mapNameToObject.clear();
			_mapIdToObject.clear();
			_mapEntityToObject.clear();
			_listRootSceneEntities.clear();
		}

		for ( GameObject* pObj : listDying )
		{
			if ( pObj != nullptr )
				pObj->~GameObject();
		}

		{
			std::unique_lock<std::shared_mutex> lock{ _mutex };
			_listGoChunks.clear();
			_listGoFree.clear();
		}

		_registry.clear();
	}

	void GameObjectManager::clearAllCachedTypeInfo()
	{
		_registry.clearAllCachedTypeInfo();
	}

	void GameObjectManager::rebindAllCachedTypeInfo()
	{
		_registry.rebindAllCachedTypeInfo();
	}

	void GameObjectManager::mergePendingAdds()
	{
		vector<GameObject*> listLocalPending;
		{
			std::unique_lock<std::shared_mutex> lock{ _mutex };
			if ( _listPendingAdds.empty() )
				return;
			listLocalPending = std::move( _listPendingAdds );
			_listPendingAdds.clear();
		}

		{
			std::unique_lock<std::shared_mutex> lock{ _mutex };
			_listGameObjects.reserve( _listGameObjects.size() + listLocalPending.size() );
			for ( GameObject* pObj : listLocalPending )
			{
				if ( pObj != nullptr )
				{
					pObj->_managerIndex = static_cast<uint32>( _listGameObjects.size() );
					_listGameObjects.push_back( pObj );
					_mapNameToObject[pObj->getName()]	= pObj;
					_mapIdToObject[pObj->getObjectId()] = pObj;
					if ( pObj->getEntityId() != sw::kNullEntity )
						_mapEntityToObject[pObj->getEntityId()] = pObj;
				}
			}
		}

		for ( GameObject* pObj : listLocalPending )
		{
			if ( pObj != nullptr )
				pObj->refreshActiveInHierarchy();
		}
	}

	void GameObjectManager::registerPendingFactories( string_view moduleName, sw::ComponentFactoryRegistrar* pHead )
	{
		_registry.registerPendingFactories( moduleName, pHead );
	}

	void GameObjectManager::unregisterFactoriesByModule( string_view moduleName )
	{
		_registry.unregisterFactoriesByModule( moduleName );
	}

	vector<hashed_string> GameObjectManager::getRegisteredComponentTypeNames() const
	{
		const auto&			  factories = _registry.getRegisteredFactories();
		vector<hashed_string> listNames;
		listNames.reserve( factories.size() );
		for ( const auto& [name, factory] : factories )
		{
			listNames.push_back( name );
		}
		return listNames;
	}

	void GameObjectManager::registerPendingScriptSystems( string_view moduleName, ScriptSystemRegistrar* pHead )
	{
		(void)moduleName;
		ScriptSystemRegistrar* pCurr = pHead;
		while ( pCurr != nullptr )
		{
			if ( pCurr->_registerFunc != nullptr )
				pCurr->_registerFunc( this );
			pCurr = pCurr->_pNext;
		}
		if ( pHead == ScriptSystemRegistrar::getHead() )
			ScriptSystemRegistrar::getHead() = nullptr;
	}

	void GameObjectManager::reinitScriptSystems()
	{
		clearScriptSystems();
		registerPendingScriptSystems( "Engine", ScriptSystemRegistrar::getHead() );
		{
			std::scoped_lock<mutex> lock{ getModuleScriptSystemMutex() };
			for ( const auto& [mod, head] : getModuleScriptSystemHeads() )
			{
				if ( head != nullptr )
					registerPendingScriptSystems( mod.c_str(), head );
			}
		}
	}

	void GameObjectManager::registerModuleScriptSystemHead( string_view moduleName, ScriptSystemRegistrar* pHead )
	{
		_s_engineScriptHeadSealed = true;
		std::scoped_lock<mutex> lock{ getModuleScriptSystemMutex() };
		if ( pHead == nullptr )
			getModuleScriptSystemHeads().erase( string( moduleName ) );
		else
			getModuleScriptSystemHeads()[string( moduleName )] = pHead;
	}

	void GameObjectManager::unregisterModuleScriptSystemHead( string_view moduleName )
	{
		std::scoped_lock<mutex> lock{ getModuleScriptSystemMutex() };
		getModuleScriptSystemHeads().erase( string( moduleName ) );
	}

	void GameObjectManager::registerGameObject( GameObject* pObj )
	{
		if ( pObj == nullptr )
			return;

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		const uint64						id = generateNewId();
		pObj->_objectId						   = id;
		pObj->_pOwnerManager				   = this;

		if ( _mapNameToObject.find( pObj->getName() ) != _mapNameToObject.end() )
		{
			pObj->_name = makeUniqueNameUnlocked( pObj->getName() );
		}

		_mapNameToObject.insert_or_assign( pObj->getName(), pObj );
		_mapIdToObject.insert_or_assign( id, pObj );

		if ( pObj->_entityId != sw::kNullEntity )
			_mapEntityToObject.insert_or_assign( pObj->_entityId, pObj );

		_listPendingAdds.push_back( pObj );
	}

	void GameObjectManager::flushSceneComponentSubtree( SceneComponent* pRoot, bool bParentChanged )
	{
		if ( pRoot == nullptr )
			return;

		// Iterative DFS using an explicit stack to avoid stack overflow on deep hierarchies.
		// Each entry: (node, parentChanged)
		vector<std::pair<SceneComponent*, bool>> stack;
		stack.reserve( 32 );
		stack.emplace_back( pRoot, bParentChanged );

		while ( stack.empty() == false )
		{
			auto [node, parentDirty] = stack.back();
			stack.pop_back();

			if ( node == nullptr )
				continue;

			const bool bNeedsUpdate = parentDirty || node->isTransformDirty();
			if ( bNeedsUpdate )
				node->updateWorldTransformFromParent();

			if ( bNeedsUpdate || node->hasDirtyDescendant() )
			{
				const auto& children = node->getChildren();
				for ( auto it = children.rbegin(); it != children.rend(); ++it )
				{
					stack.emplace_back( *it, bNeedsUpdate );
				}
			}
			node->clearDirtyDescendant();
		}
	}

	uint64 GameObjectManager::generateNewId()
	{
		return _nextId.fetch_add( 1, std::memory_order_relaxed );
	}

	hashed_string GameObjectManager::makeUniqueNameUnlocked( hashed_string requested ) const
	{
		if ( _mapNameToObject.find( requested ) == _mapNameToObject.end() )
			return requested;

		const utf8* pBase = requested.c_str();
		if ( pBase == nullptr || pBase[0] == '\0' )
			pBase = "GameObject";

		StringBuilder<constant::kMaxBuffer128> sb;
		for ( uint32 nameSuffix = 2; nameSuffix < 10000; ++nameSuffix )
		{
			sb.clear();
			sb.append( pBase ).append( '_' ).append( nameSuffix );
			const hashed_string candidate( sb.c_str(), sb.size() );
			if ( _mapNameToObject.find( candidate ) == _mapNameToObject.end() )
			{
				SW_LOG_WARNING( "[GameObjectManager] Duplicate name '%#' — using '%#'", requested.c_str(), candidate.c_str() );
				return candidate;
			}
		}

		static std::atomic<uint32> s_fallback{ 0 };
		for ( uint32 entityIndex = 0; entityIndex < 1024; ++entityIndex )
		{
			sb.clear();
			sb.append( pBase ).append( "_x" ).append( s_fallback.fetch_add( 1 ) );
			const hashed_string fallback( sb.c_str(), sb.size() );
			if ( _mapNameToObject.find( fallback ) == _mapNameToObject.end() )
			{
				SW_LOG_ERROR( "[GameObjectManager] Exhausted numeric suffixes for '%#' — using '%#'",
							  requested.c_str(), fallback.c_str() );
				return fallback;
			}
		}

		SW_LOG_ERROR( "[GameObjectManager] Failed to uniquify '%#' — creating unnamed object", requested.c_str() );
		sb.clear();
		sb.append( "GameObject_anon_" ).append( s_fallback.fetch_add( 1 ) );
		return hashed_string( sb.c_str(), sb.size() );
	}
} // namespace sw
