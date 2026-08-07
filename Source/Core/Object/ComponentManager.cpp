/**
 * @file ComponentManager.cpp
 * @brief ComponentManager 구현
 */
#include "pch.h"
#include "ComponentManager.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Task/TaskManager.h"

namespace sw
{
	Component* ComponentManager::createComponentByName( hashed_string typeName ) const
	{
		std::unordered_map<hashed_string, ComponentFactoryDelegate>::const_iterator iter = _factories.find( typeName );
		if ( iter != _factories.end() && iter->second.isBound() )
		{
			return iter->second();
		}
		SW_LOG_WARNING( "[ComponentManager] Component type not registered in factory: %#", typeName.c_str() );
		return nullptr;
	}

	void ComponentManager::tickAllComponentsParallel( const std::vector<Component*>& activeComponents, float32 deltaTime )
	{
		if ( activeComponents.empty() )
			return;

		uint32 totalCount = static_cast<uint32>( activeComponents.size() );

		ParallelTaskDelegate delegate = SW_DELEGATE_LAMBDA( ParallelTaskDelegate, [&activeComponents, deltaTime]( uint32 index )
		{
			if ( index < activeComponents.size() )
			{
				Component* comp = activeComponents[index];
				if ( comp != nullptr && comp->isActive() )
				{
					comp->onTick( deltaTime );
				}
			}
		} );

		sw::getTaskManager().emplaceParallel( totalCount, delegate );
		sw::getTaskManager().waitAll();
	}

	void ComponentManager::clear()
	{
		_factories.clear();
		_registeredTypes.clear();
	}

	void ComponentManager::registerPendingFactories( ComponentFactoryRegistrar* head )
	{
		ComponentFactoryRegistrar* curr = head;
		while ( curr != nullptr )
		{
			if ( curr->_registerFunc != nullptr )
				curr->_registerFunc( *this );
			curr = curr->_next;
		}
	}

	ComponentFactoryRegistrar*& ComponentFactoryRegistrar::getHead()
	{
		static ComponentFactoryRegistrar* s_head = nullptr;
		return s_head;
	}

	ComponentFactoryRegistrar::ComponentFactoryRegistrar( void ( *registerFunc )( ComponentManager& ) )
		: ComponentFactoryRegistrar( registerFunc, getHead() )
	{
	}

	ComponentFactoryRegistrar::ComponentFactoryRegistrar( void ( *registerFunc )( ComponentManager& ), ComponentFactoryRegistrar*& moduleHead )
		: _registerFunc{ registerFunc }
		, _next{ nullptr }
	{
		_next	   = moduleHead;
		moduleHead = this;
	}
} // namespace sw
