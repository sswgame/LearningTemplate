/**
 * @file ComponentManager.cpp
 * @brief ComponentManager 구현
 */
#include "pch.h"
#include "ComponentManager.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Common/CoreServices.h"

namespace sw
{
	Component* ComponentManager::createComponentByName( hashed_string typeName ) const
	{
		std::unordered_map<hashed_string, ComponentFactoryDelegate>::const_iterator iter = _factories.find( typeName );
		if ( iter != _factories.end() && iter->second.isBound() )
		{
			Component* created = iter->second();
			if ( created != nullptr )
			{
				const TypeInfo* typeInfo = core::getTypeRegistry().findType( typeName );
				created->setCachedTypeInfo( typeInfo );
			}
			return created;
		}
		SW_LOG_WARNING( "[ComponentManager] Component type not registered in factory: %#", typeName.c_str() );
		return nullptr;
	}

	void ComponentManager::tickAllComponentsParallel( const std::vector<Component*>& activeComponents, float32 deltaTime )
	{
		if ( activeComponents.empty() )
			return;

		// TickGroup order, then dependency waves within each group; parallel only inside a wave.
		std::vector<std::vector<Component*>> waves;
		buildComponentTickWaves( activeComponents, waves );

		for ( const std::vector<Component*>& wave : waves )
		{
			if ( wave.empty() )
				continue;

			if ( wave.size() == 1 )
			{
				Component* comp = wave.front();
				if ( comp != nullptr && comp->isActive() )
					comp->onTick( deltaTime );
				continue;
			}

			const uint32 totalCount = static_cast<uint32>( wave.size() );
			ParallelTaskDelegate delegate = SW_DELEGATE_LAMBDA( ParallelTaskDelegate, [&wave, deltaTime]( uint32 index )
			{
				if ( index < wave.size() )
				{
					Component* comp = wave[index];
					if ( comp != nullptr && comp->isActive() )
						comp->onTick( deltaTime );
				}
			} );

			core::getTaskManager().emplaceParallel( totalCount, delegate );
			core::getTaskManager().waitAll();
		}
	}

	void ComponentManager::clear()
	{
		_factories.clear();
		_factoryModules.clear();
		_registeredTypes.clear();
		_activeModuleName = hashed_string();
	}

	void ComponentManager::registerPendingFactories( const std::string_view moduleName, ComponentFactoryRegistrar* head )
	{
		_activeModuleName = hashed_string( moduleName.data(), static_cast<uint32>( moduleName.size() ) );

		ComponentFactoryRegistrar* curr = head;
		while ( curr != nullptr )
		{
			if ( curr->_registerFunc != nullptr )
				curr->_registerFunc( *this );
			curr = curr->_next;
		}

		_activeModuleName = hashed_string();
	}

	void ComponentManager::unregisterFactoriesByModule( const std::string_view moduleName )
	{
		const hashed_string hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );

		for ( auto it = _factoryModules.begin(); it != _factoryModules.end(); )
		{
			if ( it->second != hashModule )
			{
				++it;
				continue;
			}

			const hashed_string typeName = it->first;
			_factories.erase( typeName );
			it = _factoryModules.erase( it );

			for ( auto typeIt = _registeredTypes.begin(); typeIt != _registeredTypes.end(); ++typeIt )
			{
				if ( *typeIt == typeName )
				{
					_registeredTypes.erase( typeIt );
					break;
				}
			}
		}
	}

	void ComponentManager::clearAllCachedTypeInfo()
	{
		for ( const std::unique_ptr<Scene>& scene : core::getSceneManager().getLoadedScenes() )
		{
			if ( scene == nullptr )
				continue;
			GameObjectManager* objects = scene->getObjectManager();
			if ( objects != nullptr )
				objects->clearAllCachedTypeInfo();
		}
	}

	void ComponentManager::rebindAllCachedTypeInfo()
	{
		for ( const std::unique_ptr<Scene>& scene : core::getSceneManager().getLoadedScenes() )
		{
			if ( scene == nullptr )
				continue;
			GameObjectManager* objects = scene->getObjectManager();
			if ( objects != nullptr )
				objects->rebindAllCachedTypeInfo();
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
