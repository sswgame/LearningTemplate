#pragma once
/**
 * @file GameObjectManager.h
 * @brief 씬 내 GameObject 생성·조회·지연 삭제 관리
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Core/Utility/String/hashed_string.h"

namespace sw
{
	class GameObject;
	class Component;

	class SW_API GameObjectManager
	{
	public:
		GameObjectManager();
		~GameObjectManager();

		/** @brief 새 GameObject를 생성하고 등록합니다. */
		GameObject* createGameObject( hashed_string name = hashed_string( "GameObject" ) );

		/** @brief 이미 생성된 GameObject를 매니저에 등록합니다. */
		void registerGameObject( GameObject* obj );

		/**
		 * @brief 등록된 GameObject의 이름을 바꾸고 이름 맵을 갱신합니다.
		 * @details GameObject::setName이 내부적으로 호출합니다.
		 */
		void notifyNameChanged( GameObject* obj, hashed_string oldName, hashed_string newName );

		/** @brief rename API — setName과 동일하게 이름 맵을 유지합니다. */
		bool renameGameObject( GameObject* obj, hashed_string newName );

		/** @brief 이름으로 GameObject를 찾습니다. */
		GameObject* findGameObjectByName( hashed_string name ) const;

		/** @brief 오브젝트 ID로 GameObject를 찾습니다. */
		GameObject* findGameObjectById( uint64 objectId ) const;

		/** @brief 등록된 모든 GameObject 목록을 반환합니다. */
		const std::vector<GameObject*>& getAllGameObjects() const { return _gameObjects; }

		/** @brief 등록된 GameObject에 순차 tick을 호출합니다. */
		void tick( float32 deltaTime );

		/**
		 * @brief 계층-안전 병렬 tick
		 * @details 1) SceneComponent 월드 캐시 flush (루트→자식)
		 *          2) 병렬 GameObject::tick (트랜스폼 캐시 읽기 전용)
		 *          3) tick 중 변경된 로컬 포즈를 반영하기 위해 다시 flush
		 */
		void tickParallel( float32 deltaTime );

		/** @brief 모든 SceneComponent 월드 캐시를 계층 순으로 갱신합니다. */
		void flushSceneTransforms();

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
} // namespace sw
