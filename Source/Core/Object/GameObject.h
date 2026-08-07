#pragma once

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Core/Object/Component.h"
#include "Core/Object/TagSystem.h"

/**
 * @file GameObject.h
 * @brief 엔진 월드 액터/엔티티의 기반 클래스인 GameObject 클래스 정의
 */

namespace sw
{
	class GameObjectManager;

	/**
	 * @class GameObject
	 * @brief 라이프사이클(beginPlay, tick), 리플렉션, 태그 시스템, 컴포넌트 컨테이너 기능을 제공하는 엔티티 클래스
	 */
	class SW_API GameObject
	{
		friend class GameObjectManager;

	public:
		GameObject();
		/** @brief 이름을 지정하는 생성자 */
		explicit GameObject( hashed_string name );
		virtual ~GameObject();

		GameObject( const GameObject& )			   = delete;
		GameObject& operator=( const GameObject& ) = delete;

		/** @brief 런타임 타입 리플렉션 정보(TypeInfo) 반환 */
		virtual const TypeInfo* getTypeInfo() const;

		/** @brief 게임플레이 시작 시 최초 1회 호출되는 초기화 루틴 */
		virtual void beginPlay();

		/** @brief 게임플레이 종료 시 호출되는 정리 루틴 (스냅샷 복원 전) */
		virtual void endPlay();

		/** @brief 매 프레임 업데이트 루틴 */
		virtual void tick( float32 deltaTime );

		/** @brief 에디터 또는 직렬화에 의해 프로퍼티 변경 시 이벤트 콜백 */
		virtual void onPropertyChanged( hashed_string propertyName );

		/** @brief 지연 삭제 (프레임 종료 시 안전 해제) */
		void destroyDeferred();

		/** @brief 게임 오브젝트 이름 설정 (등록된 매니저 이름 맵도 갱신) */
		void setName( hashed_string name );

		/** @brief 게임 오브젝트 이름 반환 */
		hashed_string getName() const { return _name; }

		/** @brief 고유 오브젝트 ID (UID) 반환 */
		uint64 getObjectId() const { return _objectId; }

		/**
		 * @brief 활성화/비활성화 설정
		 * @details 자체 활성 플래그를 갱신하고 `isActiveInHierarchy`를 부모 계층에 맞게 재계산합니다.
		 *          소유 컴포넌트에는 자체 활성 값을 전파합니다 (SceneComponent 계층과 별개).
		 */
		void setActive( bool bActive );

		/** @brief 자체 활성화 여부 반환 */
		bool isActive() const { return _bActive; }

		/**
		 * @brief 최종 활성화 여부 반환
		 * @details `_bActive && _bIsActiveInHierarchy`. 부모 GO가 비활성이면 자식도 false.
		 */
		bool isActiveInHierarchy() const { return _bActive && _bIsActiveInHierarchy; }

		/**
		 * @brief 부모 GameObject에 부착 (사이클 방지). SceneComponent 계층과 독립.
		 * @return 성공 시 true
		 */
		bool attachToParent( GameObject* parent );

		/** @brief 부모에서 분리 (자식 관계는 유지하지 않음 — 이 GO만 detach) */
		void detachFromParent();

		/** @brief 부모 GameObject (없으면 nullptr) */
		GameObject* getParent() const { return _parent; }

		/** @brief 직계 자식 GameObject 목록 */
		const std::vector<GameObject*>& getChildren() const { return _children; }

		/** @brief 태그 추가 */
		void addTag( TagID tag ) { _tags.addTag( tag ); }
		/** @brief 태그 제거 */
		void removeTag( TagID tag ) { _tags.removeTag( tag ); }
		/** @brief 모든 태그 제거 (직렬화 로드 전 정리용) */
		void clearTags() { _tags.clear(); }
		/** @brief 태그 소유 검사 */
		bool hasTag( TagID tag, bool bExactMatch = false ) const { return _tags.hasTag( tag, bExactMatch ); }
		/** @brief 태그 매칭 조건 검사 */
		bool matchTags( const TagContainer& required, const TagContainer& forbidden ) const { return _tags.matchTags( required, forbidden ); }
		/** @brief 소유한 태그 컨테이너 참조 반환 */
		const TagContainer& getTags() const { return _tags; }

		/**
		 * @brief typeid(T).name()에서 MSVC `class `/`struct ` 접두사와 네임스페이스를 제거한 짧은 타입명 키
		 * @details CodeGenerator factory 등록키(typeInfo.name)와 템플릿 addComponent 경로를 일치시킵니다.
		 */
		static hashed_string makeComponentTypeKeyFromTypeidName( const char* typeIdName );

		/** @brief 템플릿 컴포넌트 타입의 해시 키 생성 헬퍼 */
		template <typename T>
		SW_INLINE static hashed_string getComponentTypeKey()
		{
			static const hashed_string s_typeKey = makeComponentTypeKeyFromTypeidName( typeid( T ).name() );
			return s_typeKey;
		}

		/**
		 * @brief 템플릿 타입 T 컴포넌트 동적 추가
		 * @tparam T 추가할 Component 파생 클래스
		 * @param args 컴포넌트 생성자 인자
		 * @return 생성된 컴포넌트 포인터
		 */
		template <typename T, typename... Args>
		T* addComponent( Args&&... args )
		{
			static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );

			std::unique_ptr<T> newComp = std::make_unique<T>( std::forward<Args>( args )... );
			T*				   rawPtr  = newComp.get();
			rawPtr->setOwner( this );

			hashed_string typeKey = getComponentTypeKey<T>();
			rawPtr->setComponentName( typeKey );
			rawPtr->setCachedTypeInfo( core::getTypeRegistry().findType( typeKey ) );

			_components[typeKey].push_back( std::move( newComp ) );
			_flatComponents.push_back( rawPtr );
			_bIsTickOrderDirty = true;
			return rawPtr;
		}

		/**
		 * @brief 타입 이름을 통한 동적 컴포넌트 생성 (리플렉션용)
		 * @param componentTypeName 등록된 리플렉션 타입 이름
		 * @return 생성된 컴포넌트 포인터 (실패 시 nullptr)
		 */
		Component* addComponentByName( hashed_string componentTypeName );

		/**
		 * @brief 특정 타입 T의 모든 부착된 컴포넌트 목록 검색
		 * @tparam T 검색할 Component 파생 클래스
		 * @return T 포인터 벡터
		 */
		template <typename T>
		std::vector<T*> getComponents() const
		{
			std::vector<T*> result;
			hashed_string	typeKey = getComponentTypeKey<T>();

			std::unordered_map<hashed_string, std::vector<std::unique_ptr<Component>>>::const_iterator iter = _components.find( typeKey );
			if ( iter != _components.end() )
			{
				for ( const std::unique_ptr<Component>& comp : iter->second )
				{
					result.push_back( static_cast<T*>( comp.get() ) );
				}
			}
			return result;
		}

		/**
		 * @brief 특정 타입 T의 첫 번째 부착 컴포넌트 검색
		 * @tparam T 검색할 Component 파생 클래스
		 * @return 발견된 T 컴포넌트 포인터 (없을 시 nullptr)
		 */
		template <typename T>
		T* getComponent() const
		{
			hashed_string																			   typeKey = getComponentTypeKey<T>();
			std::unordered_map<hashed_string, std::vector<std::unique_ptr<Component>>>::const_iterator iter	   = _components.find( typeKey );
			if ( iter != _components.end() && iter->second.empty() == false )
			{
				return static_cast<T*>( iter->second.front().get() );
			}
			return nullptr;
		}

		/** @brief 특정 컴포넌트 인스턴스 제거 */
		bool removeComponent( Component* comp );

		/** @brief 부착된 전체 컴포넌트 개수 반환 */
		uint32 getComponentCount() const;

		/** @brief 플랫 컴포넌트 목록(틱·트랜스폼 flush용) */
		const std::vector<Component*>& getAllComponents() const { return _flatComponents; }

		/** @brief 소유한 모든 컴포넌트 일괄 제거 */
		void clearComponents();

		/** @brief 컴포넌트 Tick 우선순위 정렬 플래그 갱신 마킹 */
		void markTickOrderDirty() { _bIsTickOrderDirty = true; }

	private:
		/** @brief 부모 활성 상태를 반영해 `_bIsActiveInHierarchy`를 재계산하고 자식에 전파 */
		void refreshActiveInHierarchy();

		std::unordered_map<hashed_string, std::vector<std::unique_ptr<Component>>> _components;		///< 타입별 컴포넌트 맵
		std::vector<Component*>													   _flatComponents; ///< 플랫 접근용 컴포넌트 벡터
		TagContainer															   _tags;			///< 태그 정보

		GameObject*				 _parent = nullptr; ///< 부모 GameObject (SceneComponent 계층과 별개)
		std::vector<GameObject*> _children;			///< 직계 자식 GameObject

		uint64 _objectId = 0; ///< 오브젝트 고유 시리얼 번호

		hashed_string _name; ///< 오브젝트 식별 명칭

		GameObjectManager* _ownerManager = nullptr; ///< registerGameObject 시 설정되는 소유 매니저

		uint8				   _bActive				 : 1; ///< 자체 활성화 비트
		uint8				   _bIsActiveInHierarchy : 1; ///< 계층 반영 최종 활성 비트
		uint8				   _bIsTickOrderDirty	 : 1; ///< Tick 우선순위 변경 마크
		[[maybe_unused]] uint8 _reservedFlags		 : 5;

		static uint64 _s_nextObjectId; ///< 다음 발급할 고유 ID 카운터
	};
} // namespace sw
