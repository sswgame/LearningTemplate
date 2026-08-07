/**
 * @file GameObjectManager.cpp
 * @brief GameObjectManager 구현
 */
#include "pch.h"
#include "GameObjectManager.h"
#include "Core/Object/Component.h"
#include "Core/Utility/Task/TaskManager.h"
namespace sw
{

	GameObjectManager::GameObjectManager()
	{
	}

	GameObjectManager::~GameObjectManager()
	{
		clear();
	}

	GameObject* GameObjectManager::createGameObject( hashed_string name )
	{
		GameObject* newObj = new GameObject( name );
		registerGameObject( newObj );
		return newObj;
	}

	void GameObjectManager::registerGameObject( GameObject* obj )
	{
		if ( obj == nullptr )
			return;

		std::lock_guard<std::mutex> lock( _mutex );
		auto [iter, inserted] = _mapIdToObject.try_emplace( obj->getObjectId(), obj );
		if ( inserted == false )
			return;

		if ( _gameObjects.capacity() <= _gameObjects.size() )
		{
			_gameObjects.reserve( _gameObjects.size() + 16 );
		}
		_gameObjects.push_back( obj );
		_mapNameToObject.insert_or_assign( obj->getName(), obj );
	}

	GameObject* GameObjectManager::findGameObjectByName( hashed_string name ) const
	{
		std::lock_guard<std::mutex> lock( _mutex );
		auto						it = _mapNameToObject.find( name );
		return it != _mapNameToObject.end() ? it->second : nullptr;
	}

	GameObject* GameObjectManager::findGameObjectById( uint64 objectId ) const
	{
		std::lock_guard<std::mutex> lock( _mutex );
		auto						it = _mapIdToObject.find( objectId );
		return it != _mapIdToObject.end() ? it->second : nullptr;
	}

	void GameObjectManager::tick( float32 deltaTime )
	{
		std::vector<GameObject*> activeObjs;
		{
			std::lock_guard<std::mutex> lock( _mutex );
			activeObjs.reserve( _gameObjects.size() );
			activeObjs = _gameObjects;
		}

		for ( GameObject* obj : activeObjs )
		{
			if ( obj != nullptr && obj->isActiveInHierarchy() )
			{
				obj->tick( deltaTime );
			}
		}

		processDeferredDestruction();
	}

	void GameObjectManager::tickParallel( float32 deltaTime )
	{
		std::vector<GameObject*> activeObjs;
		{
			std::lock_guard<std::mutex> lock( _mutex );
			activeObjs = _gameObjects;
		}

		if ( activeObjs.empty() )
			return;

		for ( GameObject* obj : activeObjs )
		{
			if ( obj != nullptr && obj->isActiveInHierarchy() )
			{
				TaskDelegate del = SW_DELEGATE_LAMBDA( TaskDelegate, [obj, deltaTime]()
				{
					obj->tick( deltaTime );
				} );
				sw::getTaskManager().emplaceTask( del );
			}
		}

		sw::getTaskManager().dispatch();
		sw::getTaskManager().waitAll();

		processDeferredDestruction();
	}

	void GameObjectManager::destroyObjectDeferred( GameObject* obj )
	{
		if ( obj == nullptr )
			return;

		std::lock_guard<std::mutex> lock( _mutex );
		for ( GameObject* existing : _pendingObjects )
		{
			if ( existing == obj )
				return;
		}
		_pendingObjects.push_back( obj );
	}

	void GameObjectManager::destroyComponentDeferred( Component* comp )
	{
		if ( comp == nullptr )
			return;

		std::lock_guard<std::mutex> lock( _mutex );
		for ( Component* existing : _pendingComponents )
		{
			if ( existing == comp )
				return;
		}
		_pendingComponents.push_back( comp );
	}

	void GameObjectManager::processDeferredDestruction()
	{
		std::vector<GameObject*> objsToDestroy;
		std::vector<Component*>	 compsToDestroy;

		{
			std::lock_guard<std::mutex> lock( _mutex );
			objsToDestroy.swap( _pendingObjects );
			compsToDestroy.swap( _pendingComponents );
		}

		for ( Component* comp : compsToDestroy )
		{
			if ( comp != nullptr )
			{
				GameObject* owner = comp->getOwner();
				if ( owner != nullptr )
				{
					owner->removeComponent( comp );
				}
				else
				{
					delete comp;
				}
			}
		}

		for ( GameObject* obj : objsToDestroy )
		{
			if ( obj != nullptr )
			{
				{
					std::lock_guard<std::mutex> lock( _mutex );
					auto						it = std::find( _gameObjects.begin(), _gameObjects.end(), obj );
					if ( it != _gameObjects.end() )
					{
						_gameObjects.erase( it );
					}
					_mapNameToObject.erase( obj->getName() );
					_mapIdToObject.erase( obj->getObjectId() );
				}
				delete obj;
			}
		}
	}

	void GameObjectManager::clear()
	{
		std::lock_guard<std::mutex> lock( _mutex );

		_pendingObjects.clear();
		_pendingComponents.clear();

		for ( GameObject* obj : _gameObjects )
		{
			if ( obj != nullptr )
			{
				delete obj;
			}
		}
		_gameObjects.clear();
		_mapNameToObject.clear();
		_mapIdToObject.clear();
	}
}
