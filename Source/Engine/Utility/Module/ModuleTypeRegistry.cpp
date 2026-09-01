#include "pch.h"

#include "Engine/Utility/Module/ModuleTypeRegistry.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

SW_LOG_CALLER( "ModuleTypeRegistry" );
namespace sw
{
	namespace
	{
		struct ModuleHeadRecord
		{
			TypeRegistrar*				   _pTypeHead{ nullptr };
			EnumRegistrar*				   _pEnumHead{ nullptr };
			sw::ComponentFactoryRegistrar* _pFactoryHead{ nullptr };
		};

		unordered_map<string, ModuleHeadRecord>& getModuleHeadCache()
		{
			static unordered_map<string, ModuleHeadRecord> s_mapModuleHead;
			return s_mapModuleHead;
		}
	} // namespace

	namespace engine
	{
		void registerModuleTypes( string_view moduleName )
		{
			auto&		 cache	= getModuleHeadCache();
			const string modStr = string{ moduleName };

			TypeRegistrar*				   pTypeHead	= TypeRegistrar::getHead();
			EnumRegistrar*				   pEnumHead	= EnumRegistrar::getHead();
			sw::ComponentFactoryRegistrar* pFactoryHead = sw::ComponentFactoryRegistrar::getHead();

			const auto it = cache.find( modStr );
			if ( it != cache.end() )
			{
				if ( pTypeHead == nullptr )
					pTypeHead = it->second._pTypeHead;
				else
					it->second._pTypeHead = pTypeHead;

				if ( pEnumHead == nullptr )
					pEnumHead = it->second._pEnumHead;
				else
					it->second._pEnumHead = pEnumHead;

				if ( pFactoryHead == nullptr )
					pFactoryHead = it->second._pFactoryHead;
				else
					it->second._pFactoryHead = pFactoryHead;
			}
			else if ( pTypeHead != nullptr || pEnumHead != nullptr || pFactoryHead != nullptr )
			{
				cache[modStr] = ModuleHeadRecord{ pTypeHead, pEnumHead, pFactoryHead };
			}

			registerModuleTypes( moduleName, pTypeHead, pEnumHead, pFactoryHead );

			TypeRegistrar::getHead()				 = nullptr;
			EnumRegistrar::getHead()				 = nullptr;
			sw::ComponentFactoryRegistrar::getHead() = nullptr;
		}

		void registerModuleTypes( string_view					 moduleName,
								  TypeRegistrar*				 pTypeHead,
								  EnumRegistrar*				 pEnumHead,
								  sw::ComponentFactoryRegistrar* pFactoryHead )
		{
			auto&		 cache	= getModuleHeadCache();
			const string modStr = string{ moduleName };
			const auto	 it		= cache.find( modStr );
			if ( it != cache.end() )
			{
				if ( pTypeHead != nullptr )
					it->second._pTypeHead = pTypeHead;
				else
					pTypeHead = it->second._pTypeHead;

				if ( pEnumHead != nullptr )
					it->second._pEnumHead = pEnumHead;
				else
					pEnumHead = it->second._pEnumHead;

				if ( pFactoryHead != nullptr )
					it->second._pFactoryHead = pFactoryHead;
				else
					pFactoryHead = it->second._pFactoryHead;
			}
			else if ( pTypeHead != nullptr || pEnumHead != nullptr || pFactoryHead != nullptr )
			{
				cache[modStr] = ModuleHeadRecord{ pTypeHead, pEnumHead, pFactoryHead };
			}

			getTypeRegistry().registerPendingTypes( moduleName, pTypeHead, pEnumHead );
			GameObjectManager::registerModuleFactoryHead( moduleName, pFactoryHead );

			for ( const auto& scene : getSceneManager().getLoadedScenes() )
			{
				if ( scene && scene->getObjectManager() )
				{
					scene->getObjectManager()->registerPendingFactories( moduleName, pFactoryHead );
					scene->getObjectManager()->rebindAllCachedTypeInfo();
				}
			}
		}

		void unregisterModuleTypes( string_view moduleName )
		{
			getModuleHeadCache().erase( string{ moduleName } );
			GameObjectManager::unregisterModuleFactoryHead( moduleName );
			for ( const auto& scene : getSceneManager().getLoadedScenes() )
			{
				if ( scene && scene->getObjectManager() )
					scene->getObjectManager()->unregisterFactoriesByModule( moduleName );
			}
			getTypeRegistry().unregisterTypesByModule( moduleName );
			getGlobalVariableManager().unregisterVariablesByModule( moduleName );
		}
	} // namespace engine
} // namespace sw
