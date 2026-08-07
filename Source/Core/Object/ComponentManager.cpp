/**
 * @file ComponentManager.cpp
 * @brief Auto-generated documentation header
 */
#include "pch.h"
#include "ComponentManager.h"
#include "Core/Utility/Log/Logger.h"

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
}
