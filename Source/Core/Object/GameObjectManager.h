#pragma once
/**
 * @file GameObjectManager.h
 * @brief 씬 내 GameObject 생성·조회·지연 삭제 관리
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

		/** @brief 새 GameObject를 생성하고 등록합니다. */
		GameObject* createGameObject( hashed_string name = hashed_string( "GameObject" ) );

		/** @brief 이미 생성된 GameObject를 매니저에 등록합니다. */
		void registerGameObject( GameObject* obj );

		/** @brief 이름으로 GameObject를 찾습니다. */
		GameObject* findGameObjectByName( hashed_string name ) const;

		/** @brief 오브젝트 ID로 GameObject를 찾습니다. */
		GameObject* findGameObjectById( uint64 objectId ) const;

		/** @brief 등록된 모든 GameObject 목록을 반환합니다. */
		const std::vector<GameObject*>& getAllGameObjects() const { return _gameObjects; }

		/** @brief 등록된 GameObject에 순차 tick을 호출합니다. */
		void tick( float32 deltaTime );

		/** @brief 활성 컴포넌트 기준 병렬 tick을 수행합니다. */
		void tickParallel( float32 deltaTime );

		/** @brief GameObject를 지연 삭제 큐에 넣습니다. */
		void destroyObjectDeferred( GameObject* obj );

		/** @brief Component를 지연 삭제 큐에 넣습니다. */
		void destroyComponentDeferred( Component* comp );

		/** @brief 지연 삭제 큐의 오브젝트·컴포넌트를 실제로 해제합니다. */
		void processDeferredDestruction();

		/** @brief 등록·대기 목록을 모두 비웁니다. */
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
