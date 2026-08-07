#pragma once
/**
 * @file GameObjectManager.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Core/Object/GameObject.h"

namespace sw
{

	class SW_API GameObjectManager
	{
	public:
		GameObjectManager();
		~GameObjectManager();

		GameObject* createGameObject( hashed_string name = hashed_string( "GameObject" ) );

		/**
		 * @brief registerGameObject 처리를 수행합니다.
		 */
		void registerGameObject( GameObject* obj );

		/**
		 * @brief findGameObjectByName 처리를 수행합니다.
		 */
		GameObject* findGameObjectByName( hashed_string name ) const;

		/**
		 * @brief findGameObjectById 처리를 수행합니다.
		 */
		GameObject* findGameObjectById( uint64 objectId ) const;

		const std::vector<GameObject*>& getAllGameObjects() const { return _gameObjects; }

		/**
		 * @brief tick 처리를 수행합니다.
		 */
		void tick( float32 deltaTime );

		/**
		 * @brief tickParallel 처리를 수행합니다.
		 */
		void tickParallel( float32 deltaTime );

		/**
		 * @brief destroyObjectDeferred 처리를 수행합니다.
		 */
		void destroyObjectDeferred( GameObject* obj );

		/**
		 * @brief destroyComponentDeferred 처리를 수행합니다.
		 */
		void destroyComponentDeferred( Component* comp );

		/**
		 * @brief processDeferredDestruction 처리를 수행합니다.
		 */
		void processDeferredDestruction();

		/**
		 * @brief clear 처리를 수행합니다.
		 */
		void clear();

	private:

		std::vector<GameObject*>					   _gameObjects;
		std::unordered_map<hashed_string, GameObject*> _mapNameToObject;
		std::unordered_map<uint64, GameObject*>		   _mapIdToObject;
		std::vector<GameObject*>					   _pendingObjects;
		std::vector<Component*>						   _pendingComponents;
		mutable std::mutex							   _mutex;
	};
}
