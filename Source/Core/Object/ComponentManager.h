#pragma once
/**
 * @file ComponentManager.h
 * @brief 컴포넌트 등록·tick 그룹 정렬·일괄 업데이트
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

#include "Core/Utility/Delegate/Delegate.h"
#include "Core/Utility/String/hashed_string.h"
#include "Core/Object/Component.h"

namespace sw
{
	using ComponentFactoryDelegate = Delegate<Component*()>;

	class SW_API ComponentManager
	{
	public:
		/** @brief 등록된 팩토리를 비웁니다. */
		void shutdown() { clear(); }

		/**
		 * @brief 컴포넌트 타입 T를 이름과 함께 팩토리에 등록합니다.
		 * @tparam T Component 파생 타입
		 */
		template <typename T>
		void registerComponentType( hashed_string typeName )
		{
			static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );

			ComponentFactoryDelegate creator = SW_DELEGATE_LAMBDA( ComponentFactoryDelegate, []() -> Component*
			{
				return new T();
			} );

			auto [iter, inserted] = _factories.try_emplace( typeName, std::move( creator ) );
			if ( inserted )
			{
				_registeredTypes.push_back( typeName );
			}
		}

		/** @brief 등록된 타입 이름으로 Component를 생성합니다. */
		Component* createComponentByName( hashed_string typeName ) const;

		/** @brief 등록된 컴포넌트 타입 이름 목록을 반환합니다. */
		const std::vector<hashed_string>& getRegisteredComponentTypes() const { return _registeredTypes; }

		/** @brief 활성 컴포넌트를 TickGroup·순서에 맞춰 병렬 tick합니다. */
		void tickAllComponentsParallel( const std::vector<Component*>& activeComponents, float32 deltaTime );

		/** @brief 팩토리·등록 타입 목록을 비웁니다. */
		void clear();

		/** @brief 정적 ComponentFactoryRegistrar 링크드 리스트를 드레인합니다. */
		void registerPendingFactories( struct ComponentFactoryRegistrar* head );

	public:
		ComponentManager()	= default;
		~ComponentManager() = default;

		ComponentManager( const ComponentManager& )			   = delete;
		ComponentManager& operator=( const ComponentManager& ) = delete;
		ComponentManager( ComponentManager&& )				   = delete;
		ComponentManager& operator=( ComponentManager&& )	   = delete;

	private:
		std::unordered_map<hashed_string, ComponentFactoryDelegate> _factories;
		std::vector<hashed_string>									_registeredTypes;
	};

	/**
	 * @brief 정적 초기 시 ComponentManager가 아직 없을 수 있으므로,
	 *        TypeRegistrar와 같이 링크만 하고 registerCoreReflectionTypes에서 드레인합니다.
	 */
	struct SW_API ComponentFactoryRegistrar
	{
		void ( *_registerFunc )( ComponentManager& );
		ComponentFactoryRegistrar* _next;

		static ComponentFactoryRegistrar*& getHead();

		ComponentFactoryRegistrar( void ( *registerFunc )( ComponentManager& ) );
		ComponentFactoryRegistrar( void ( *registerFunc )( ComponentManager& ), ComponentFactoryRegistrar*& moduleHead );
	};

#ifndef SW_COMPONENT_FACTORY_MODULE_HEAD
	#define SW_COMPONENT_FACTORY_MODULE_HEAD() (::sw::ComponentFactoryRegistrar::getHead())
#endif
} // namespace sw
