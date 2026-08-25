/**
 * @file GameObject.h
 * @brief 엔진 월드 액터/엔티티의 기반 클래스인 GameObject 클래스 정의
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/ECS/Entity.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <atomic>
#include <tuple>
#include <utility>

namespace sw
{
	/**

	 * @brief 엔티티 상태용 순수 ECS 데이터 (계층 활성 등).
	 */
	REFLECT()
	struct SW_API EntityStateData
	{
		REFLECT_BODY();
		std::atomic<bool> bIsActiveInHierarchy;
		std::atomic<bool> bIsPendingKill;

		/** @brief 계층에서 활성인 기본 상태입니다. */
		EntityStateData()
			: bIsActiveInHierarchy{ true }
			, bIsPendingKill{ false }
		{
		}

		EntityStateData( const EntityStateData& other )
			: bIsActiveInHierarchy{ other.bIsActiveInHierarchy.load( std::memory_order_relaxed ) }
			, bIsPendingKill{ other.bIsPendingKill.load( std::memory_order_relaxed ) }
		{
		}

		EntityStateData& operator=( const EntityStateData& other )
		{
			if ( this != &other )
			{
				bIsActiveInHierarchy.store( other.bIsActiveInHierarchy.load( std::memory_order_relaxed ), std::memory_order_relaxed );
				bIsPendingKill.store( other.bIsPendingKill.load( std::memory_order_relaxed ), std::memory_order_relaxed );
			}
			return *this;
		}
	};

	/**
	 * @class GameObject
	 * @brief 라이프사이클(beginPlay, tick), 리플렉션, 태그 시스템, 컴포넌트 컨테이너 기능을 제공하는 엔티티 클래스
	 */
	class SW_API GameObject
	{
		friend class GameObjectManager;

	public:
		// ------------------------------------------------------------------------------
		// 1) 수명 — 이름, beginPlay/endPlay, 지연 삭제
		// ------------------------------------------------------------------------------
		/** @brief 기본 게임 오브젝트를 만듭니다. */
		GameObject();
		/** @brief 이름을 지정해 만듭니다. */
		explicit GameObject( hashed_string name );

		/** @brief 복사를 금지합니다. */
		GameObject( const GameObject& ) = delete;
		/** @brief 대입을 금지합니다. */
		GameObject& operator=( const GameObject& ) = delete;

		/** @brief 런타임 TypeInfo를 반환합니다. */
		virtual const TypeInfo* getTypeInfo() const;

		/** @brief 게임플레이 시작 시 최초 1회 초기화. */
		virtual void beginPlay();

		/** @brief 게임플레이 종료 시 정리 (스냅샷 복원 전). */
		virtual void endPlay();

		/** @brief 에디터/직렬화가 프로퍼티를 바꿀 때 콜백. */
		virtual void onPropertyChanged( hashed_string propertyName );

		/** @brief 플레이 종료 시 안전하게 지연 파괴합니다. */
		void destroy();

		// ------------------------------------------------------------------------------
		// 2) 식별 · 활성 — 이름, ID, 매니저, 계층 활성
		// ------------------------------------------------------------------------------
		/** @brief 이름을 설정하고 매니저 이름 맵도 갱신합니다. */
		void setName( hashed_string name );

		/** @brief 게임 오브젝트 이름 반환 */
		hashed_string getName() const { return _name; }

		/** @brief 고유 오브젝트 ID (UID) 반환 */
		uint64 getObjectId() const { return _objectId; }

		/** @brief ECS 엔티티 ID 반환 */
		sw::Entity getEntityId() const { return _entityId; }

		/** @brief 매니저 인스턴스 반환 */
		class GameObjectManager* getManager() const { return _pOwnerManager; }

		/** @brief 지연 삭제 (Tombstone) 플래그 마킹 */
		void markPendingKill() { _bIsPendingKill.store( true, std::memory_order_release ); }

		/** @brief 현재 이 오브젝트가 삭제 예정인지 확인 */
		bool isPendingKill() const { return _bIsPendingKill.load( std::memory_order_acquire ); }

		/**
		 * @brief 활성화/비활성화 설정
		 * @details 자체 활성 플래그를 갱신하고 `isActiveInHierarchy`를 부모 계층에 맞게 재계산합니다.
		 *          소유 컴포넌트에는 자체 활성 값을 전파합니다 (SceneComponent 계층과 별개).
		 */
		void setActive( bool bActive );

		/** @brief 자체 활성화 여부 반환 */
		bool isActive() const { return _bActive.load( std::memory_order_relaxed ); }

		/**
		 * @brief 최종 활성화 여부 반환
		 * @details `_bActive && _bIsActiveInHierarchy`. 부모 GO가 비활성이면 자식도 false.
		 */
		bool isActiveInHierarchy() const { return _bActive.load( std::memory_order_relaxed ) && _bIsActiveInHierarchy.load( std::memory_order_relaxed ); }

		/**
		 * @brief 부모 GameObject에 부착 (사이클 방지).
		 * @details GO 트리와 SceneComponent 트리는 개념적으로 분리되어 있으나,
		 *          attach 시 양쪽의 primary SceneComponent가 있으면 Unreal AttachToActor처럼
		 *          자식 SC를 부모 SC 아래로 재부모합니다.
		 * @return 성공 시 true
		 */
		// ------------------------------------------------------------------------------
		// 3) 계층 — GO 트리 + primary SceneComponent transform
		// ------------------------------------------------------------------------------
		bool attachToParent( GameObject* pParent );

		/** @brief 부모로부터 부착 해제 */
		void detachFromParent();

		/** @brief 부모 GameObject 포인터 반환 (엔티티 ID로 조회) */
		GameObject* getParent() const;

		/** @brief 자식 GameObject 포인터 목록 반환 (엔티티 ID로 조회) */
		vector<GameObject*> getChildren() const;

		/**
		 * @brief transform 계층의 primary SceneComponent (부모 없는 첫 SceneComponent).
		 * @details GO↔SC 정합에 사용. 없으면 nullptr.
		 */
		class SceneComponent* getPrimarySceneComponent() const;

		// ------------------------------------------------------------------------------
		// 4) 태그
		// ------------------------------------------------------------------------------
		/** @brief 태그를 추가합니다. */
		void addTag( TagID tag );
		/** @brief 태그 제거 */
		void removeTag( TagID tag );
		/** @brief 모든 태그 제거 (직렬화 로드 전 정리용) */
		void clearTags();
		/** @brief 태그 소유 검사 */
		bool hasTag( TagID tag, bool bExactMatch = false ) const;
		/** @brief 태그 매칭 조건 검사 */
		bool matchTags( const TagContainer& required, const TagContainer& forbidden ) const;
		/** @brief TagQuery 복합 불리언 질의 조건 검사 */
		bool matchesTagQuery( const TagQuery& query ) const;
		/** @brief 소유한 태그 컨테이너 참조 반환 (태그 컴포넌트가 없으면 빈 컨테이너 반환) */
		const TagContainer& getTags() const;

		// ------------------------------------------------------------------------------
		// 5) 컴포넌트 — 추가/조회/제거, ECS 엔티티당 타입 1개
		// ------------------------------------------------------------------------------
		template <typename T>
		/** @brief REFLECT TypeInfo 단축명 키 (factory 등록키와 동일). */
		SW_INLINE static hashed_string getComponentTypeKey()
		{
			static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
			static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T>,
						   "T must declare its own REFLECT_BODY() (cannot slice to a base class StaticType)" );

			const TypeInfo* pInfo = nullptr;
			if constexpr ( HasStaticType_v<T> )
				pInfo = T::StaticType();
			else if constexpr ( HasReflectStaticType_v<T> )
				pInfo = ReflectTypeTraits<T>::StaticType();

			return pInfo != nullptr ? pInfo->_name : hashed_string{};
		}

		/**
		 * @brief 템플릿 타입 T 컴포넌트 동적 추가
		 * @tparam T 추가할 Component 파생 클래스 또는 ECS 구조체
		 * @param args 컴포넌트 생성자 인자
		 * @return 생성된 컴포넌트 포인터. 구조 동결(tick) 중이면 지연 큐에 넣고 nullptr.
		 */
		template <typename T, typename... Args>
		/** @brief 타입 T 컴포넌트를 동적으로 추가합니다. */
		T* addComponent( Args&&... args )
		{
			static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T> || sizeof( T ) == sizeof( Component ),
						   "T must declare its own REFLECT_BODY() or REFLECT_SCRIPT()! Component-derived classes with member variables MUST declare REFLECT_SCRIPT()." );

			if ( _pOwnerManager == nullptr )
			{
				SW_LOG_ERROR( "[GameObject] Cannot add component without an owner GameObjectManager!" );
				return nullptr;
			}

			if ( _pOwnerManager->isStructuralMutationFrozen() )
			{
				const uint64	   objectId = _objectId;
				GameObjectManager* pMgr		= _pOwnerManager;
				auto			   packedArgs =
					std::make_tuple( std::decay_t<Args>( std::forward<Args>( args ) )... );
				pMgr->deferPostTick( [pMgr, objectId, packedArgs = std::move( packedArgs )]() mutable
				{
					GameObject* pObj = pMgr->findGameObjectById( objectId );
					if ( pObj == nullptr )
						return;
					std::apply( [pObj]( auto&&... forwarded )
					{ pObj->addComponent<T>( std::forward<decltype( forwarded )>( forwarded )... ); },
								std::move( packedArgs ) );
				} );
				return nullptr;
			}

			_pOwnerManager->getRegistry().emplace<T>( _entityId, std::forward<Args>( args )... );

			T* pComp = _pOwnerManager->getRegistry().getPtr<T>( _entityId );

			if constexpr ( std::is_base_of_v<Component, T> )
			{
				if ( pComp != nullptr )
				{
					pComp->setOwner( this );
					const TypeInfo* pTypeInfo = nullptr;
					if constexpr ( HasStaticType_v<T> )
						pTypeInfo = T::StaticType();
					else if constexpr ( HasReflectStaticType_v<T> )
						pTypeInfo = ReflectTypeTraits<T>::StaticType();

					const hashed_string typeKey = pTypeInfo != nullptr ? pTypeInfo->_name : hashed_string{};
					pComp->setComponentName( typeKey );
					pComp->setCachedTypeInfo( pTypeInfo );

					registerComponentIfSceneRoot( pComp );
				}
			}

			return pComp;
		}

		/**
		 * @brief 동적 런타임 환경 등에서 타입 식별 명칭(해시)으로 ECS 컴포넌트 부착
		 * @param componentTypeName 등록된 컴포넌트 타입 이름 (예: "TransformComponent")
		 * @param bLogWarning 컴포넌트가 없을 시 로그 출력 여부
		 * @return 부착된 컴포넌트 포인터. 구조 동결(tick) 중이면 지연 후 nullptr.
		 */
		Component* addComponentByName( hashed_string componentTypeName, bool bLogWarning = true )
		{
			if ( _pOwnerManager == nullptr )
				return nullptr;

			if ( _pOwnerManager->isStructuralMutationFrozen() )
			{
				const uint64		objectId = _objectId;
				GameObjectManager*	pMgr	 = _pOwnerManager;
				const hashed_string typeName = componentTypeName;
				const bool			bWarn	 = bLogWarning;
				pMgr->deferPostTick( [pMgr, objectId, typeName, bWarn]()
				{
					GameObject* pObj = pMgr->findGameObjectById( objectId );
					if ( pObj != nullptr )
						pObj->addComponentByName( typeName, bWarn );
				} );
				return nullptr;
			}

			Component* pComp = _pOwnerManager->getRegistry().addComponentByName( _entityId, componentTypeName, bLogWarning );
			if ( pComp != nullptr )
			{
				pComp->setOwner( this );
				// TypeInfo 캐시는 팩토리 또는 이후 직렬화에서 채웁니다.
				registerComponentIfSceneRoot( pComp );
			}
			return pComp;
		}

		/** @brief 엔티티에 붙은 Component 개수. */
		size_t getComponentCount() const;

		/** @brief 스택 기반 버퍼를 사용하여 안전하게 모든 컴포넌트를 순회합니다. (힙 할당 0건) */
		template <typename Func>
		void forEachComponent( Func&& func ) const
		{
			if ( _pOwnerManager != nullptr && _entityId != sw::kNullEntity )
			{
				_pOwnerManager->getRegistry().forEachComponent( _entityId, [&]( Component* pComp )
				{
					if ( pComp != nullptr && pComp->isPendingKill() == false )
					{
						func( pComp );
					}
				} );
			}
		}

		/**
		 * @brief 엔티티의 모든 Component 포인터. pending-kill은 제외합니다.
		 * @details Registry::getAllComponents와 구현이 비슷해 보이지만,
		 *          자체 forEachComponent를 호출하여 삭제 대기 중(pending-kill)인 컴포넌트를 필터링하므로
		 *          게임 로직에서 안전하게 사용할 수 있습니다.
		 */
		vector<Component*> getAllComponents() const;

		/** @brief 엔티티의 컴포넌트를 모두 제거하고 새 엔티티를 발급합니다. (직렬화 복원용) */
		void clearComponents()
		{
			if ( _pOwnerManager != nullptr && _entityId != sw::kNullEntity )
			{
				_pOwnerManager->getRegistry().destroy( _entityId );
				_entityId = _pOwnerManager->getRegistry().create();
				_pOwnerManager->notifyEntityCreated( this );
			}
		}

		/** @brief 타입 T의 컴포넌트 핸들. 없거나 pending-kill이면 get()이 nullptr. */
		template <typename T>
		sw::TComponentHandle<T> getComponent() const
		{
			if ( _pOwnerManager != nullptr && _entityId != sw::kNullEntity )
				return _pOwnerManager->getRegistry().handleFor<T>( _entityId );
			return {};
		}

		/** @brief 풀 락을 유지한 채 안전하게 타입 T의 컴포넌트에 람다/함수를 실행합니다. 없거나 pending-kill이면 false를 반환합니다. */
		template <typename T, typename Func>
		bool withComponent( Func&& func )
		{
			if ( _pOwnerManager != nullptr && _entityId != sw::kNullEntity )
			{
				return _pOwnerManager->getRegistry().withComponent<T>( _entityId, [&]( T& comp )
				{
					if constexpr ( std::is_base_of_v<Component, T> )
					{
						if ( comp.isActive() && comp.isPendingKill() == false )
							func( comp );
					}
					else
					{
						func( comp );
					}
				} );
			}
			return false;
		}

		/** @brief 풀 락을 유지한 채 안전하게 타입 T의 const 컴포넌트에 람다/함수를 실행합니다. */
		template <typename T, typename Func>
		bool withComponentConst( Func&& func ) const
		{
			if ( _pOwnerManager != nullptr && _entityId != sw::kNullEntity )
			{
				return _pOwnerManager->getRegistry().withComponentConst<T>( _entityId, [&]( const T& comp )
				{
					if constexpr ( std::is_base_of_v<Component, T> )
					{
						if ( comp.isActive() && comp.isPendingKill() == false )
							func( comp );
					}
					else
					{
						func( comp );
					}
				} );
			}
			return false;
		}

		/** @brief 특정 컴포넌트 인스턴스 제거 */
		bool removeComponent( Component* pComp );

		// ------------------------------------------------------------------------------
		// 6) 루트 SC 등록 · 상태 이전 · Tick 순서
		// ------------------------------------------------------------------------------
		/** @brief 컴포넌트가 SceneComponent 루트일 경우 등록 (런타임 추가용) */
		void registerComponentIfSceneRoot( Component* pComp );

		/** @brief 컴포넌트가 SceneComponent 루트일 경우 등록 해제 (런타임 제거용) */
		void unregisterComponentIfSceneRoot( Component* pComp );

		/** @brief 다른 GameObject로부터 상태(이름, 활성화, 태그, 컴포넌트 등)를 이전해옵니다. (트랜잭션 복원용) */
		void moveStateFrom( GameObject& source );

		/** @brief Tick 웨이브를 다시 만들도록 표시합니다. */
		void markTickOrderDirty();

		/** @brief 에디터 등에서 속성 테이블을 띄울 때 사용되는 커스텀 프로퍼티 뷰어 */
		virtual void drawEditorProperties() {}

	protected:
		/** @brief 게임 오브젝트를 해제합니다. */
		virtual ~GameObject();

	private:
		/** @brief 부모 활성 상태를 반영해 `_bIsActiveInHierarchy`를 재계산하고 자식에 전파 */
		void refreshActiveInHierarchy();

		static std::atomic<uint64> _s_nextObjectId; ///< 다음 발급할 고유 ID 카운터

	private:
		uint64			   _objectId;					  ///< 오브젝트 고유 시리얼 번호
		hashed_string	   _name;						  ///< 오브젝트 식별 명칭
		GameObjectManager* _pOwnerManager;				  ///< registerGameObject 시 설정되는 소유 매니저
		sw::Entity		   _entityId;					  ///< ECS Entity ID
		uint32			   _managerIndex;				  ///< Manager의 _gameObjects 내 인덱스
		std::atomic<bool>  _bActive{ true };			  ///< 자체 활성화 비트
		std::atomic<bool>  _bIsActiveInHierarchy{ true }; ///< 계층 반영 최종 활성 비트
		std::atomic<bool>  _bIsPendingKill{ false };	  ///< 지연 삭제 대기 묘비 플래그
	};

} // namespace sw
