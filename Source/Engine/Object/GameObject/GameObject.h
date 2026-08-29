/**
 * @file GameObject.h
 * @brief 엔진 월드 액터의 기반 클래스인 GameObject 클래스 정의
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
	namespace generated
	{
		struct sw_GameObject_Registrar;
	} // namespace generated

	class ObjectStateSerializer;
	class SceneComponent;

	/**
	 * @class GameObject
	 * @brief 라이프사이클(beginPlay), 태그, 컴포넌트 목록을 가진 월드 액터
	 */
	REFLECT()
	class SW_API GameObject
	{
		friend class GameObjectManager;
		friend class ObjectStateSerializer;
		friend struct ::sw::generated::sw_GameObject_Registrar;

	public:
		REFLECT_BODY();

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

		/** @brief 이름을 설정하고 매니저 이름 맵도 갱신합니다. */
		void setName( hashed_string name );

		/** @brief 게임 오브젝트 이름 반환 */
		hashed_string getName() const { return _name; }

		/** @brief 고유 오브젝트 ID (UID) 반환 */
		uint64 getObjectId() const { return _objectId; }

		/** @brief 매니저 인스턴스 반환 */
		class GameObjectManager* getManager() const { return _pOwnerManager; }

		/** @brief 지연 삭제 (Tombstone) 플래그 마킹 */
		void markPendingKill();

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
		 * @details primary SceneComponent 계층을 공유합니다. 병렬 트랜스폼 구간에서는 지연합니다.
		 * @return 성공 시 true. 지연된 경우에도 true.
		 */
		bool attachToParent( GameObject* pParent );

		/** @brief 부모로부터 부착 해제 */
		void detachFromParent();

		/** @brief 부모 GameObject. primary SceneComponent의 부모 소유자입니다. */
		GameObject* getParent() const;

		/** @brief 자식 GameObject 목록. primary SceneComponent의 자식 소유자입니다. */
		vector<GameObject*> getChildren() const;

		/**
		 * @brief 이 오브젝트가 pAncestor와 같거나 그 자식 계층에 있으면 true.
		 * @details Unity Transform.IsChildOf와 같이 자기 자신도 true입니다. pAncestor가 nullptr이면 false.
		 */
		bool isDescendantOf( const GameObject* pAncestor ) const;

		/**
		 * @brief transform 계층의 primary SceneComponent (목록에서 첫 SceneComponent 파생).
		 * @details 없으면 nullptr.
		 */
		class SceneComponent* getPrimarySceneComponent() const;

		/** @brief 태그를 추가합니다. TagComponent가 없으면 만듭니다. */
		void addTag( TagID tag );
		/** @brief 태그 제거. TagComponent가 없으면 no-op. */
		void removeTag( TagID tag );
		/** @brief 모든 태그 제거 (직렬화 로드 전 정리용) */
		void clearTags();
		/** @brief 태그 소유 검사. TagComponent가 없으면 false. */
		bool hasTag( TagID tag, bool bExactMatch = false ) const;
		/** @brief TagQuery 복합 불리언 질의 조건 검사 */
		bool matchesTagQuery( const TagQuery& query ) const;
		/** @brief TagComponent의 태그 컨테이너. 없으면 추가한 뒤 반환합니다. */
		TagContainer& getTags();
		/** @brief TagComponent의 태그 컨테이너. 없으면 빈 컨테이너. */
		const TagContainer& getTags() const;

		/**
		 * @brief 타입 T 컴포넌트를 동적으로 추가합니다.
		 * @return 생성된 컴포넌트. 구조 동결(tick) 중이면 지연 큐에 넣고 nullptr.
		 */
		template <typename T, typename... Args>
		T* addComponent( Args&&... args )
		{
			static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
			static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T> || sizeof( T ) == sizeof( Component ),
						   "T must declare its own REFLECT_BODY()." );

			if ( _pOwnerManager == nullptr )
			{
				SW_LOG_ERROR( "Cannot add component without an owner GameObjectManager!" );
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

			T* pComp = sw_new T( std::forward<Args>( args )... );
			if ( pComp == nullptr )
				return nullptr;

			pComp->setOwner( this );
			const TypeInfo* pTypeInfo = nullptr;
			if constexpr ( HasStaticType_v<T> )
				pTypeInfo = T::StaticType();
			else if constexpr ( HasReflectStaticType_v<T> )
				pTypeInfo = ReflectTypeTraits<T>::StaticType();

			const hashed_string typeKey = pTypeInfo != nullptr ? pTypeInfo->_name : hashed_string{};
			pComp->setComponentName( typeKey );
			pComp->applyTypeDefaults( pTypeInfo );

			_listComponent.push_back( pComp );
			registerComponentIfSceneRoot( pComp );
			markTickOrderDirty();
			return pComp;
		}

		/** @brief 소유 Component 개수. pending-kill은 제외합니다. */
		size_t getComponentCount() const;

		/** @brief 소유 Component 포인터. pending-kill은 제외합니다. */
		vector<Component*> getAllComponents() const;

		/** @brief 소유 컴포넌트를 모두 해제합니다. (직렬화 복원용) */
		void clearComponents();

		/**
		 * @brief 타입 T의 첫 컴포넌트. 파생 타입을 포함합니다.
		 * @return 없거나 pending-kill이면 nullptr.
		 */
		template <typename T>
		T* getComponent() const
		{
			static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
			static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T>,
						   "T must declare its own REFLECT_BODY() (cannot slice to a base class StaticType)" );

			for ( Component* pComp : _listComponent )
			{
				if ( pComp == nullptr || pComp->isPendingKill() )
					continue;
				T* pCast = castTo<T>( pComp );
				if ( pCast != nullptr )
					return pCast;
			}
			return nullptr;
		}

		/**
		 * @brief TypeInfo 이름 또는 컴포넌트 이름으로 첫 컴포넌트를 찾습니다.
		 * @return 없거나 pending-kill이면 nullptr.
		 */
		Component* findComponentByTypeName( hashed_string typeName ) const;

		/** @brief 특정 컴포넌트 인스턴스 제거 */
		bool removeComponent( Component* pComp );

		/** @brief componentId로 소유 컴포넌트를 찾습니다. */
		Component* findComponentById( uint64 componentId, bool bIncludePendingKill = false ) const;

		/** @brief Tick 웨이브를 다시 만들도록 표시합니다. */
		void markTickOrderDirty();

		/** @brief 직렬화 직전에 SceneComponent Attach* 를 `_pParent`에서 채웁니다. */
		void prepareSerialize() const;
		/** @brief 로드된 Attach 필드로 SceneComponent 계층을 복원합니다. */
		void applyLoadedHierarchy();

		/** @brief 게임 오브젝트를 해제합니다. */
		virtual ~GameObject();

	private:
		/** @brief 부모 활성 상태를 반영해 `_bIsActiveInHierarchy`를 재계산하고 자식에 전파 */
		void refreshActiveInHierarchy();
		/** @brief 컴포넌트가 SceneComponent 루트일 경우 매니저에 등록합니다. */
		void registerComponentIfSceneRoot( Component* pComp );
		/** @brief 컴포넌트가 SceneComponent 루트일 경우 매니저에서 해제합니다. */
		void unregisterComponentIfSceneRoot( Component* pComp );

		static atomic<uint64> _s_nextObjectId; ///< 다음 발급할 고유 ID 카운터

	private:
		uint64 _objectId; ///< 오브젝트 고유 시리얼 번호
		PROPERTY()
		hashed_string	   _name;		   ///< 오브젝트 식별 명칭
		GameObjectManager* _pOwnerManager; ///< registerGameObject 시 설정되는 소유 매니저
		PROPERTY()
		atomic<bool> _bActive;				///< 자체 활성화 비트
		atomic<bool> _bIsActiveInHierarchy; ///< 계층 반영 최종 활성 비트
		atomic<bool> _bIsPendingKill;		///< 지연 삭제 대기 묘비 플래그
		PROPERTY()
		vector<Component*> _listComponent; ///< 이 액터가 소유한 컴포넌트
		uint32			   _managerIndex;  ///< Manager의 _gameObjects 내 인덱스
	};

} // namespace sw
