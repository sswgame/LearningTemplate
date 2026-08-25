/**
 * @file GameObjectManager.h
 * @brief 씬 내 GameObject 생성·조회·지연 삭제 관리
 */
#pragma once
#include "Engine/ECS/Registry.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Physics/PhysicsWorld.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include <shared_mutex>

namespace sw
{
	class GameObject;
	class Component;
	class SceneComponent;
	class MeshComponent;
	class GameObjectManager;
	struct GameObjectManagerAccess;

	/**
	 * @struct ScriptSystemRegistrar
	 * @brief 모듈별 스크립트 시스템 정적 등록 연결 리스트 노드
	 */
	struct SW_API ScriptSystemRegistrar
	{
		using RegisterFunc = void ( * )( GameObjectManager* );
		RegisterFunc		   _registerFunc{ nullptr };
		ScriptSystemRegistrar* _pNext{ nullptr };

		static ScriptSystemRegistrar*& getHead();

		ScriptSystemRegistrar( RegisterFunc registerFunc );
		ScriptSystemRegistrar( RegisterFunc registerFunc, ScriptSystemRegistrar*& moduleHead );
	};

#ifndef SW_SCRIPT_SYSTEM_MODULE_HEAD
	#define SW_SCRIPT_SYSTEM_MODULE_HEAD() ( ::sw::ScriptSystemRegistrar::getHead() )
#endif

	/// @brief GameObject 등록, 지연 삭제, 씬 컴포넌트 루트
	class SW_API GameObjectManager
	{
		friend class GameObject;
		friend class SceneComponent;
		friend class MeshComponent;
		friend struct GameObjectManagerAccess;

	public:
		/** @brief 레지스트리와 이름 맵을 비운 채 시작합니다. */
		GameObjectManager();
		/** @brief 등록된 오브젝트를 모두 파괴합니다. */
		~GameObjectManager();

		/** @brief 새 GameObject를 생성하고 등록합니다. */
		GameObject* createGameObject( hashed_string name = hashed_string( "GameObject" ) );

		/**
		 * @brief 등록된 GameObject의 이름을 바꾸고 이름 맵을 갱신합니다.
		 * @details GameObject::setName이 내부적으로 호출합니다.
		 */
		void notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName );

		/** @brief 엔티티 ID가 지연 생성되었을 때 매핑을 갱신합니다. */
		void notifyEntityCreated( GameObject* pObj );

		/** @brief rename API — setName과 동일하게 이름 맵을 유지합니다. */
		bool renameGameObject( GameObject* pObj, hashed_string newName );

		/** @brief 이름으로 GameObject를 찾습니다. */
		GameObject* findGameObjectByName( hashed_string name ) const;

		/** @brief 오브젝트 ID로 GameObject를 찾습니다. */
		GameObject* findGameObjectById( uint64 objectId ) const;

		/** @brief ECS 엔티티 ID로 GameObject를 찾습니다. */
		GameObject* findGameObjectByEntity( sw::Entity entityId ) const;

		/** @brief 등록된 모든 GameObject 목록의 스냅샷을 반환합니다. pending-add를 포함하며 목록을 바꾸지 않습니다. */
		vector<GameObject*> getAllGameObjects() const;

		/** @brief 태그를 가진 첫 GameObject를 반환합니다. 없으면 nullptr. */
		GameObject* findGameObjectByTag( TagID tag ) const;

		/** @brief 태그를 가진 GameObject를 out에 넣습니다. */
		void findGameObjectsByTag( TagID tag, vector<GameObject*>& out ) const;

		/** @brief 모든 스크립트 시스템의 beginPlay 이벤트를 호출합니다. */
		void beginPlay();

		/** @brief 모든 스크립트 시스템의 endPlay 이벤트를 호출합니다. */
		void endPlay();

		/**
		 * @brief 계층-안전 병렬 tick
		 * @details 1) SceneComponent 월드 캐시 flush (루트→자식, dirty subtree만)
		 *          2) 병렬 Component/스크립트 tick (트랜스폼 캐시 읽기 전용, 구조 변경 지연)
		 *          3) 지연 트랜스폼 적용 후 CommandBuffer 실행, dirty면 다시 flush
		 */
		void tick( float32 deltaTime );

		/** @brief 모든 루트 SceneComponent 월드 캐시를 계층 순으로 갱신합니다 (dirty만). */
		void flushSceneTransforms();

		/** @brief 어떤 루트라도 dirty/descendant dirty가 있으면 true. */
		bool hasDirtySceneTransforms() const;

		/** @brief 현재 매니저가 병렬 틱(읽기 전용 트랜스폼) 구간인지 확인합니다. */
		bool isParallelTransformReadOnly() const { return _bParallelTransformReadOnly.load( std::memory_order_relaxed ); }

		/** @brief beginTick/병렬 tick 구간이라 GO 생성·addComponent 등 구조 변경을 미뤄야 하면 true. */
		bool isStructuralMutationFrozen() const
		{
			return isParallelTransformReadOnly() || _registry.isTicking();
		}

		using TransformUpdateDelegate = Delegate<void()>;
		using PostTickDelegate		  = Delegate<void()>;

		/** @brief 트랜스폼/계층 구조 변경을 지연 큐에 넣습니다. */
		void deferTransformUpdate( TransformUpdateDelegate func );

		/**
		 * @brief 병렬 tick 이후(finishTick 뒤) 메인 스레드에서 실행할 작업을 넣습니다.
		 * @details GO 생성·addComponent·데미지·태그 변경 등 구조/공유 상태 변경에 사용합니다.
		 */
		void deferPostTick( PostTickDelegate func );

		/**
		 * @brief 구조 동결 중이면 deferPostTick, 아니면 즉시 실행합니다.
		 * @details createGameObject + addComponent + 초기화를 한 람다로 묶을 때 사용합니다.
		 */
		void executeOrDeferPostTick( PostTickDelegate func );

		/** @brief 루트 계층이 된 SceneComponent를 캐시에 등록합니다. */
		void registerRootSceneComponent( class SceneComponent* pComp );

		/** @brief 부모가 생기거나 파괴된 SceneComponent를 루트 캐시에서 제거합니다. */
		void unregisterRootSceneComponent( class SceneComponent* pComp );

		/** @brief 핸들이 가리키는 컴포넌트를 찾습니다. pending-kill이면 nullptr. */
		Component* resolveComponent( sw::ComponentHandle handle );

		/** @brief 이 씬의 AABB 질의 월드입니다. */
		PhysicsWorld& getPhysicsWorld() { return _physicsWorld; }
		/** @brief 이 씬의 AABB 질의 월드입니다. */
		const PhysicsWorld& getPhysicsWorld() const { return _physicsWorld; }

		/**
		 * @brief GameObject를 지연 삭제 큐에 넣습니다.
		 * @param pObj 삭제할 게임 오브젝트
		 * @param bDestroyChildren 자식 계층 오브젝트들을 함께 연쇄 삭제할지 여부
		 */
		void destroyObject( GameObject* pObj, bool bDestroyChildren = true );

		/** @brief Component를 지연 삭제 큐에 넣습니다. */
		void destroyComponent( Component* pComp );

		/** @brief 지연 삭제 큐의 오브젝트·컴포넌트를 실제로 해제합니다. */
		void processDeferredDestruction();

		/** @brief 등록·대기 목록을 모두 비웁니다. */
		void clear();

		/**
		 * @brief 소유 컴포넌트의 TypeInfo 캐시를 모두 null로 만듭니다.
		 * @details unregisterTypesByModule 직전에 호출해 댕글링 TypeInfo 포인터를 제거합니다.
		 */
		void clearAllCachedTypeInfo();

		/**
		 * @brief 컴포넌트 이름 키로 TypeRegistry에서 TypeInfo를 다시 바인딩합니다.
		 * @details registerPendingTypes 직후에 호출합니다.
		 */
		void rebindAllCachedTypeInfo();

		/** @brief 이번 프레임에 추가된 GO를 활성 목록에 합칩니다. */
		void mergePendingAdds();

		/** @brief 모듈 컴포넌트 팩토리를 이 매니저의 ECS에 등록합니다. */
		void registerPendingFactories( string_view moduleName, sw::ComponentFactoryRegistrar* pHead );

		/** @brief 해당 모듈이 등록한 컴포넌트 팩토리를 제거합니다. */
		void unregisterFactoriesByModule( string_view moduleName );

		/** @brief 에디터 등에서 추가 가능한 컴포넌트 타입 이름 목록입니다. */
		vector<hashed_string> getRegisteredComponentTypeNames() const;

		// --- Script System Delegates ---
		using ScriptSystemTickDelegate		= MulticastDelegate<void( GameObjectManager*, sw::Registry&, float32, bool /*bParallel*/ )>;
		using ScriptSystemLifecycleDelegate = MulticastDelegate<void( GameObjectManager*, sw::Registry& )>;

		/** @brief 추가합니다. */
		void registerScriptSystemTick( const Delegate<void( GameObjectManager*, sw::Registry&, float32, bool )>& delegate ) { _scriptSystemTick.add( delegate ); }
		/** @brief 추가합니다. */
		void registerScriptSystemBeginPlay( const Delegate<void( GameObjectManager*, sw::Registry& )>& delegate ) { _scriptSystemBeginPlay.add( delegate ); }
		/** @brief 추가합니다. */
		void registerScriptSystemEndPlay( const Delegate<void( GameObjectManager*, sw::Registry& )>& delegate ) { _scriptSystemEndPlay.add( delegate ); }

		/** @brief 모든 스크립트 시스템 델리게이트를 비웁니다 (핫리로드/모듈 언로드 시 안전 확보). */
		void clearScriptSystems()
		{
			_scriptSystemTick.clear();
			_scriptSystemBeginPlay.clear();
			_scriptSystemEndPlay.clear();
		}

		/** @brief 모듈의 스크립트 시스템들을 등록합니다. */
		void registerPendingScriptSystems( string_view moduleName, ScriptSystemRegistrar* pHead );

		/** @brief 현재 유효한 스크립트 시스템들을 다시 등록합니다. */
		void reinitScriptSystems();

		/** @brief 전역 모듈별 스크립트 시스템 헤드를 등록합니다. */
		static void registerModuleScriptSystemHead( string_view moduleName, ScriptSystemRegistrar* pHead );
		/** @brief 전역 모듈별 스크립트 시스템 헤드를 해제합니다. */
		static void unregisterModuleScriptSystemHead( string_view moduleName );

		/** @brief 트랜스폼이 변경되었음을 매니저에 알려 세대 카운터를 원자적으로 갱신합니다. */
		void notifyTransformDirtied() { _dirtyTransformGeneration.fetch_add( 1, std::memory_order_relaxed ); }
		/** @brief 현재 트랜스폼 더티 세대 번호를 반환합니다. */
		uint64 getTransformGeneration() const { return _dirtyTransformGeneration.load( std::memory_order_relaxed ); }

		/** @brief 이미 생성된 GameObject를 매니저에 등록하고 소유권을 가져갑니다. */
		void registerGameObject( GameObject* pObj );

	private:
		/** Object 레이어·GameObjectManagerAccess 전용. Game 모듈 API가 아닙니다. */
		/** @brief ECS 레지스트리를 반환합니다. */
		sw::Registry& getRegistry() { return _registry; }
		/** @brief ECS 레지스트리를 반환합니다. */
		const sw::Registry& getRegistry() const { return _registry; }

		/** @brief 서브트리 월드 행렬을 플러시합니다. */
		void flushSceneComponentSubtree( SceneComponent* pRoot, bool bParentChanged );
		/** @brief 새 ObjectId를 발급합니다. */
		uint64 generateNewId();
		/** @brief 잠금 없이 고유 이름을 만듭니다. */
		hashed_string makeUniqueNameUnlocked( hashed_string requested ) const;

	private:
		struct GameObjectChunk
		{
			static constexpr size_t kChunkSize = 256;
			GameObject*				_pMemory;
			GameObjectChunk();
			~GameObjectChunk();
		};
		vector<unique_ptr<GameObjectChunk>> _listGoChunks;
		vector<GameObject*>					_listGoFree;

		vector<GameObject*>						  _listGameObjects;
		unordered_map<hashed_string, GameObject*> _mapNameToObject;
		unordered_map<uint64, GameObject*>		  _mapIdToObject;
		unordered_map<sw::Entity, GameObject*>	  _mapEntityToObject;
		vector<GameObject*>						  _listPendingAdds;
		vector<GameObject*>						  _listPendingDestroyObjects;
		vector<sw::ComponentHandle>				  _listPendingDestroyComponents;

		// Double buffering queues for zero-allocation destruction
		vector<GameObject*>			_listProcessingDestroyObjects;
		vector<sw::ComponentHandle> _listProcessingDestroyComponents;

		vector<sw::Entity>		  _listRootSceneEntities;
		mutable std::shared_mutex _mutex;
		std::atomic<uint64>		  _nextId;

		sw::Registry _registry;
		PhysicsWorld _physicsWorld;

		ScriptSystemTickDelegate	  _scriptSystemTick;
		ScriptSystemLifecycleDelegate _scriptSystemBeginPlay;
		ScriptSystemLifecycleDelegate _scriptSystemEndPlay;

		std::atomic<bool>				_bParallelTransformReadOnly;
		mutex							_deferredTransformMutex;
		vector<TransformUpdateDelegate> _listDeferredTransformUpdates;
		vector<TransformUpdateDelegate> _listProcessingTransforms;
		mutex							_deferredPostTickMutex;
		vector<PostTickDelegate>		_listDeferredPostTickUpdates;
		vector<PostTickDelegate>		_listProcessingPostTicks;

		std::atomic<uint64> _dirtyTransformGeneration;
		uint64				_lastFlushedTransformGeneration;
	};
} // namespace sw
