#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Core/Container/array.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCore.h"

#include <unordered_set>

namespace sw
{
	namespace
	{
		mutex& getModuleFactoryHeadsMutex()
		{
			static mutex s_mutex;
			return s_mutex;
		}

		unordered_map<string, ComponentFactoryRegistrar*>& getModuleFactoryHeads()
		{
			static unordered_map<string, ComponentFactoryRegistrar*> s_mapFactoryHeads;
			return s_mapFactoryHeads;
		}

		vector<vector<ComponentHandle>> splitWaveByObject( const vector<ComponentHandle>& wave )
		{
			vector<vector<ComponentHandle>> subwaves;
			vector<unordered_set<uint64>>	occupied;
			for ( const ComponentHandle& handle : wave )
			{
				if ( handle.isValid() == false )
					continue;
				const uint64 objectId = handle.objectId();
				size_t		 slot{ 0 };
				for ( ; slot < subwaves.size(); ++slot )
				{
					if ( occupied[slot].count( objectId ) == 0 )
						break;
				}
				if ( slot == subwaves.size() )
				{
					subwaves.emplace_back();
					occupied.emplace_back();
				}
				subwaves[slot].push_back( handle );
				occupied[slot].insert( objectId );
			}
			return subwaves;
		}

		void resolveAndTickComponent( GameObjectManager* pManager, float32 deltaTime, const ComponentHandle& handle )
		{
			Component* pComp = pManager->resolveComponent( handle );
			if ( pComp == nullptr || pComp->isActive() == false || pComp->canEverTick() == false )
				return;
			GameObject* pOwner = pComp->getOwner();
			if ( pOwner == nullptr || pOwner->isActiveInHierarchy() == false || pOwner->isPendingKill() )
				return;
			pComp->onTick( deltaTime );
		}

		struct ComponentWaveTick
		{
			GameObjectManager*	   _pManager{ nullptr };
			const ComponentHandle* _pRawHandles{ nullptr };
			float32				   _deltaTime{ 0.0f };
			uint32				   _totalCount{ 0 };

			void tickIndex( uint32 index )
			{
				if ( index < _totalCount )
					resolveAndTickComponent( _pManager, _deltaTime, _pRawHandles[index] );
			}
		};

		void dispatchWave( GameObjectManager* pManager, float32 deltaTime, const vector<ComponentHandle>& wave )
		{
			if ( wave.empty() )
				return;

			constexpr uint32 kParallelThreshold = 16;
			if ( wave.size() < kParallelThreshold || engine::areEngineServicesBound() == false )
			{
				for ( const ComponentHandle& handle : wave )
					resolveAndTickComponent( pManager, deltaTime, handle );
				return;
			}

			ComponentWaveTick waveTick{};
			waveTick._pManager	  = pManager;
			waveTick._deltaTime	  = deltaTime;
			waveTick._pRawHandles = wave.data();
			waveTick._totalCount  = static_cast<uint32>( wave.size() );

			TaskStageHandle stage  = engine::getTaskManager().createAnonymousStage( "ComponentWave" );
			TaskHandle		handle = engine::getTaskManager().emplaceParallel(
				waveTick._totalCount, SW_DELEGATE_METHOD( ParallelTaskDelegate, &ComponentWaveTick::tickIndex, &waveTick ) );
			stage.addTask( handle );
			handle.submit();
			engine::getTaskManager().waitStage( stage );
		}
	} // namespace

	static ComponentFactoryRegistrar* _s_engineHead{ nullptr };
	static bool						  _s_engineHeadSealed{ false };

	ComponentFactoryRegistrar*& ComponentFactoryRegistrar::getHead()
	{
		static ComponentFactoryRegistrar* s_pHead{ nullptr };
		return s_pHead;
	}

	ComponentFactoryRegistrar::ComponentFactoryRegistrar( void ( *registerFunc )( GameObjectManager& ) )
		: _registerFunc{ registerFunc }
		, _pNext{ getHead() }
	{
		getHead() = this;
		if ( _s_engineHeadSealed == false )
			_s_engineHead = this;
	}

	ComponentFactoryRegistrar::ComponentFactoryRegistrar( void ( *registerFunc )( GameObjectManager& ), ComponentFactoryRegistrar*& moduleHead )
		: _registerFunc{ registerFunc }
		, _pNext{ moduleHead }
	{
		moduleHead = this;
		if ( _s_engineHeadSealed == false && &moduleHead == &getHead() )
			_s_engineHead = this;
	}

	/**
	 * @brief GameObjectManager 생성자: 기본 엔진 및 모듈 컴포넌트 팩토리들을 등록합니다.
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
		, _listPendingAdds{}
		, _listPendingDestroyObjects{}
		, _listPendingDestroyComponents{}
		, _listProcessingDestroyObjects{}
		, _listProcessingDestroyComponents{}
		, _listRootSceneComponents{}
		, _mutex{}
		, _nextId{ 1 }
		, _physicsWorld{}
		, _bParallelTransformReadOnly{ false }
		, _bTicking{ false }
		, _bIsTickWavesDirty{ true }
		, _listCachedTickWaves{}
		, _deferredTransformMutex{}
		, _listDeferredTransformUpdates{}
		, _listProcessingTransforms{}
		, _deferredPostTickMutex{}
		, _listDeferredPostTickUpdates{}
		, _listProcessingPostTicks{}
		, _mapFactories{}
		, _mapFactoryModules{}
		, _activeModuleName{}
		, _dirtyTransformGeneration{ 1 }
		, _lastFlushedTransformGeneration{ 0 }
	{
		_listDeferredTransformUpdates.reserve( 128 );
		_listProcessingTransforms.reserve( 128 );
		_listDeferredPostTickUpdates.reserve( 128 );
		_listProcessingPostTicks.reserve( 128 );

		ComponentFactoryRegistrar* pEngineHead = ComponentFactoryRegistrar::getHead();
		if ( pEngineHead == nullptr )
			pEngineHead = _s_engineHead;
		registerPendingFactories( "Engine", pEngineHead );

		vector<std::pair<string, ComponentFactoryRegistrar*>> listModuleHeads;
		{
			std::scoped_lock<mutex> lock{ getModuleFactoryHeadsMutex() };
			for ( const auto& [mod, head] : getModuleFactoryHeads() )
				listModuleHeads.push_back( { mod, head } );
		}
		for ( const auto& [mod, head] : listModuleHeads )
		{
			if ( head == nullptr )
				continue;
			if ( mod == "Engine" && pEngineHead != nullptr )
				continue;
			registerPendingFactories( mod.c_str(), head );
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

			_mapNameToObject.insert_or_assign( uniqueName, pObj );
			_mapIdToObject.insert_or_assign( id, pObj );

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
		const vector<GameObject*> listObjects = getAllGameObjects();
		for ( GameObject* pObj : listObjects )
		{
			if ( pObj != nullptr && pObj->isActiveInHierarchy() )
				pObj->beginPlay();
		}
	}

	void GameObjectManager::endPlay()
	{
		const vector<GameObject*> listObjects = getAllGameObjects();
		for ( GameObject* pObj : listObjects )
		{
			if ( pObj != nullptr && pObj->isActiveInHierarchy() )
				pObj->endPlay();
		}
	}

	void GameObjectManager::tick( float32 deltaTime )
	{
		if ( engine::areEngineServicesBound() )
			engine::getTaskManager().dispatchMainThreadTasks();
		processDeferredDestruction();
		mergePendingAdds();

		if ( _listGameObjects.empty() )
			return;

		flushSceneTransforms();

		_bParallelTransformReadOnly.store( true, std::memory_order_relaxed );
		_bTicking.store( true, std::memory_order_release );

		tickComponents( deltaTime );

		if ( engine::areEngineServicesBound() )
			engine::getTaskManager().waitAll();

		_bParallelTransformReadOnly.store( false, std::memory_order_relaxed );
		_bTicking.store( false, std::memory_order_release );

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
		for ( SceneComponent* pRoot : _listRootSceneComponents )
		{
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
		for ( const SceneComponent* pRoot : _listRootSceneComponents )
		{
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
		if ( pComp == nullptr )
			return;
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		for ( SceneComponent* pExisting : _listRootSceneComponents )
		{
			if ( pExisting == pComp )
				return;
		}
		_listRootSceneComponents.push_back( pComp );
	}

	void GameObjectManager::unregisterRootSceneComponent( SceneComponent* pComp )
	{
		if ( pComp == nullptr )
			return;
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		for ( size_t rootIndex = 0; rootIndex < _listRootSceneComponents.size(); ++rootIndex )
		{
			if ( _listRootSceneComponents[rootIndex] == pComp )
			{
				_listRootSceneComponents[rootIndex] = _listRootSceneComponents.back();
				_listRootSceneComponents.pop_back();
				return;
			}
		}
	}

	Component* GameObjectManager::resolveComponent( sw::ComponentHandle handle )
	{
		if ( handle.isValid() == false )
			return nullptr;
		GameObject* pObj = findGameObjectById( handle.objectId() );
		if ( pObj == nullptr )
			return nullptr;
		return pObj->findComponentById( handle.componentId(), true );
	}

	void GameObjectManager::destroyObject( GameObject* pObj, bool bDestroyChildren )
	{
		if ( pObj != nullptr && pObj->isPendingKill() == false )
		{
			vector<GameObject*> listChildren;
			if ( bDestroyChildren )
				listChildren = pObj->getChildren();

			pObj->markPendingKill();
			for ( Component* pComp : pObj->getAllComponents() )
			{
				if ( pComp != nullptr )
					pComp->markPendingKill();
			}
			pObj->refreshActiveInHierarchy();

			if ( bDestroyChildren )
			{
				for ( GameObject* pChild : listChildren )
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
			std::unique_lock<std::shared_mutex> lock{ _mutex };
			_listPendingDestroyComponents.push_back( pComp );
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

		for ( Component* pComp : _listProcessingDestroyComponents )
		{
			if ( pComp == nullptr )
				continue;
			GameObject* pOwner = pComp->getOwner();
			if ( pOwner != nullptr )
				pOwner->removeComponent( pComp );
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
			_listRootSceneComponents.clear();
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

		_listCachedTickWaves.clear();
	}

	void GameObjectManager::rebindAllCachedTypeInfo()
	{
		TypeRegistry&			  typeRegistry = engine::getTypeRegistry();
		const vector<GameObject*> listObjects  = getAllGameObjects();
		for ( GameObject* pObj : listObjects )
		{
			if ( pObj == nullptr )
				continue;
			for ( Component* pComp : pObj->getAllComponents() )
			{
				if ( pComp == nullptr )
					continue;
				const hashed_string typeKey = pComp->getComponentName();
				if ( typeKey.empty() )
					continue;
				pComp->applyTypeDefaults( typeRegistry.findType( typeKey ) );
			}
		}
		markTickWavesDirty();
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
		if ( pHead == nullptr )
			return;

		{
			std::scoped_lock<mutex> lock{ getModuleFactoryHeadsMutex() };
			getModuleFactoryHeads()[string( moduleName )] = pHead;
		}
		_activeModuleName					= hashed_string( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
		ComponentFactoryRegistrar* pCurrent = pHead;
		while ( pCurrent != nullptr )
		{
			if ( pCurrent->_registerFunc != nullptr )
				pCurrent->_registerFunc( *this );
			pCurrent = pCurrent->_pNext;
		}
		_activeModuleName = hashed_string();
	}

	void GameObjectManager::unregisterFactoriesByModule( string_view moduleName )
	{
		const hashed_string hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
		for ( auto it = _mapFactoryModules.begin(); it != _mapFactoryModules.end(); )
		{
			if ( it->second == hashModule )
			{
				_mapFactories.erase( it->first );
				it = _mapFactoryModules.erase( it );
			}
			else
				++it;
		}
	}

	void GameObjectManager::registerModuleFactoryHead( string_view moduleName, sw::ComponentFactoryRegistrar* pHead )
	{
		_s_engineHeadSealed = true;
		std::scoped_lock<mutex> lock{ getModuleFactoryHeadsMutex() };
		if ( pHead == nullptr )
			getModuleFactoryHeads().erase( string( moduleName ) );
		else
			getModuleFactoryHeads()[string( moduleName )] = pHead;
	}

	void GameObjectManager::unregisterModuleFactoryHead( string_view moduleName )
	{
		std::scoped_lock<mutex> lock{ getModuleFactoryHeadsMutex() };
		getModuleFactoryHeads().erase( string( moduleName ) );
	}

	Component* GameObjectManager::addComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning )
	{
		if ( pGameObject == nullptr )
			return nullptr;

		hashed_string factoryName = typeName;
		auto		  it		  = _mapFactories.find( factoryName );
		if ( it == _mapFactories.end() )
		{
			const utf8* pRawName = typeName.c_str();
			if ( pRawName != nullptr )
			{
				const utf8* pHash = nullptr;
				for ( const utf8* pCursor = pRawName; *pCursor != '\0'; ++pCursor )
				{
					if ( *pCursor == '#' )
						pHash = pCursor;
				}
				if ( pHash != nullptr && pHash != pRawName )
					factoryName = hashed_string( pRawName, static_cast<uint32>( pHash - pRawName ) );
			}
			it = _mapFactories.find( factoryName );
		}
		if ( it != _mapFactories.end() )
		{
			if ( it->second.isBound() == false )
			{
				if ( bLogWarning )
					SW_LOG_WARNING( "[GameObjectManager] Component factory for type '%#' is unbound", typeName.c_str() );
				return nullptr;
			}
			return it->second( pGameObject );
		}
		if ( bLogWarning )
			SW_LOG_WARNING( "[GameObjectManager] Component factory for type '%#' not found (%# factories registered)", typeName.c_str(), static_cast<uint32>( _mapFactories.size() ) );
		return nullptr;
	}

	vector<hashed_string> GameObjectManager::getRegisteredComponentTypeNames() const
	{
		vector<hashed_string> listNames;
		listNames.reserve( _mapFactories.size() );
		for ( const auto& [name, factory] : _mapFactories )
		{
			(void)factory;
			listNames.push_back( name );
		}
		return listNames;
	}

	void GameObjectManager::tickComponents( float32 deltaTime )
	{
		if ( _bIsTickWavesDirty.exchange( false, std::memory_order_acq_rel ) )
		{
			array<vector<Component*>, 4> groupList;
			const vector<GameObject*>	 listObjects = getAllGameObjects();
			for ( GameObject* pObj : listObjects )
			{
				if ( pObj == nullptr || pObj->isPendingKill() )
					continue;
				for ( Component* pComp : pObj->getAllComponents() )
				{
					if ( pComp == nullptr || pComp->canEverTick() == false )
						continue;
					const uint8 groupIndex = static_cast<uint8>( pComp->getTickGroup() );
					if ( groupIndex < groupList.size() )
						groupList[groupIndex].push_back( pComp );
				}
			}

			_listCachedTickWaves.clear();
			for ( const vector<Component*>& wave : groupList )
			{
				if ( wave.empty() )
					continue;
				vector<ComponentHandle> listTargets;
				listTargets.reserve( wave.size() );
				for ( Component* pComp : wave )
					listTargets.push_back( pComp->getHandle() );
				vector<vector<ComponentHandle>> subwaves = splitWaveByObject( listTargets );
				for ( vector<ComponentHandle>& subwave : subwaves )
				{
					if ( subwave.empty() == false )
						_listCachedTickWaves.push_back( std::move( subwave ) );
				}
			}
		}

		for ( const vector<ComponentHandle>& wave : _listCachedTickWaves )
			dispatchWave( this, deltaTime, wave );
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
