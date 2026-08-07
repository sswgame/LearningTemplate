#pragma once
/**
 * @file ComponentManager.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

#include "Core/Utility/Delegate/Delegate.h"
#include "Core/Utility/String/hashed_string.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Object/Component.h"

namespace sw
{
	using ComponentFactoryDelegate = Delegate<Component*()>;

	class SW_API ComponentManager
	{
	public:
		void shutdown() { clear(); }

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

		/**
		 * @brief createComponentByName 처리를 수행합니다.
		 */
		Component* createComponentByName( hashed_string typeName ) const;

		const std::vector<hashed_string>& getRegisteredComponentTypes() const { return _registeredTypes; }

		/**
		 * @brief tickAllComponentsParallel 처리를 수행합니다.
		 */
		void tickAllComponentsParallel( const std::vector<Component*>& activeComponents, float32 deltaTime );

		/**
		 * @brief clear 처리를 수행합니다.
		 */
		void clear();

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
}
