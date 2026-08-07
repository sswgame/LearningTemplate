#pragma once

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/Delegate/Delegate.h"

/**
 * @file Component.h
 * @brief GameObject에 부착되는 컴포넌트의 기반 클래스 및 TickGroup 열거형 정의
 */

namespace sw
{
	/**
	 * @enum TickGroup
	 * @brief 프레임 내 컴포넌트 tick 실행 순서 슬롯
	 * @details 이름은 물리 파이프라인 단계를 따르지만, 현재 엔진에 물리 시스템이 없어도 정렬용으로 사용합니다.
	 */
	enum class TickGroup : uint8
	{
		PrePhysics,    ///< 이른 업데이트 단계
		DuringPhysics, ///< 기본 tick 단계
		PostPhysics,   ///< 늦은 업데이트 단계
		PostUpdate,    ///< 렌더 직전 등 최종 단계
	};

	/**
	 * @class Component
	 * @brief GameObject의 기능 및 데이터를 분리 확장하기 위한 컴포넌트 기반 클래스
	 */
	REFLECT()
	class SW_API Component
	{
		friend class GameObject;

	public:
		Component();
		virtual ~Component() = default;

		Component( const Component& )			 = delete;
		Component& operator=( const Component& ) = delete;

		/** @brief 런타임 타입 리플렉션 정보(TypeInfo) 반환 */
		virtual const TypeInfo* getTypeInfo() const;

		/** @brief 게임플레이 시작 시 초기화 콜백 */
		virtual void onBeginPlay();

		/** @brief 프레임 단위 업데이트 콜백 */
		virtual void onTick( float32 deltaTime );

		/** @brief 컴포넌트 소멸 및 해제 시 콜백 */
		virtual void onDestroy();

		/** @brief 프로퍼티 변경 시 이벤트 콜백 */
		virtual void onPropertyChanged( hashed_string propertyName );

		/** @brief 소유자 GameObject 설정 */
		void setOwner( GameObject* owner ) { _owner = owner; }

		/** @brief 소유자 GameObject 반환 */
		GameObject* getOwner() const { return _owner; }

		/** @brief 컴포넌트 활성화/비활성화 상태 설정 */
		void setActive( bool bActive );

		/** @brief 활성화 상태 확인 */
		bool isActive() const { return _bActive; }

		/** @brief Tick 그룹 변경 */
		void	  setTickGroup( TickGroup group );
		/** @brief 현재 Tick 그룹 반환 */
		TickGroup getTickGroup() const { return static_cast<TickGroup>( _tickGroup ); }

		/** @brief 다른 컴포넌트가 먼저 Tick 실행되도록 선행 의존성 추가 */
		void						   addTickDependency( Component* targetComp );
		/** @brief 선행 의존 컴포넌트 목록 반환 */
		const std::vector<Component*>& getTickDependencies() const { return _tickDependencies; }

		/** @brief 컴포넌트 고유 ID 반환 */
		uint64 getComponentId() const { return _componentId; }

		/** @brief 컴포넌트 해시 명칭 반환 */
		hashed_string getComponentName() const { return _componentName; }

		/** @brief 컴포넌트 해시 명칭 설정 */
		void setComponentName( hashed_string name ) { _componentName = name; }

	public:
		using ComponentTickDelegate = Delegate<void( float32 )>;
		ComponentTickDelegate _onTickDelegate; ///< 델리게이트 기반 틱 연동 핸들러

	protected:
		GameObject* _owner = nullptr; ///< 소유자 GameObject 포인터 참조

		PROPERTY()
		uint64 _componentId = 0; ///< 컴포넌트 고유 시리얼 ID

		PROPERTY()
		hashed_string _componentName; ///< 컴포넌트 식별 이름

		std::vector<Component*> _tickDependencies; ///< 틱 선행 순서 종속성 목록

		PROPERTY()
		uint8 _tickGroup : 3; ///< TickGroup 슬롯
		uint8 _bActive	 : 1;											   ///< 컴포넌트 개별 활성화
		[[maybe_unused]] uint8 _reserved	 : 4;

	private:
		static uint64 _s_nextComponentId; ///< ID 생성 카운터
	};
}
