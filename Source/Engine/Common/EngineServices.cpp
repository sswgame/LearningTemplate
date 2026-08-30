#include "pch.h"

#include "Engine/Common/EngineServices.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Graphics/Shader/ShaderCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "RuntimeAPI/PluginAPI.h"

SW_LOG_CALLER( "EngineServices" );
namespace sw
{
	namespace
	{

		EngineServices s_services{};

	} // namespace

	namespace engine
	{
		void bindEngineServices( const EngineServices& services )
		{
			s_services = services;
			if ( services._pFrameDoubleBuffer != nullptr )
				FrameDoubleBuffer::bind( services._pFrameDoubleBuffer );
		}

		void unbindEngineServices()
		{
			s_services = {};
			FrameDoubleBuffer::bind( nullptr );
		}

		bool areEngineServicesBound()
		{
#define SW_ENGINE_SERVICE( member, Type, getter, required ) \
	if constexpr ( ( required ) != 0 )                      \
	{                                                       \
		if ( s_services.member == nullptr )                 \
			return false;                                   \
	}
#define SW_ENGINE_SERVICE_CONST( member, Type, getter, required ) SW_ENGINE_SERVICE( member, Type, getter, required )
#define SW_ENGINE_SERVICE_OPT( member, Type, getter )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
			return true;
		}

#define SW_ENGINE_SERVICE( member, Type, getter, required )                   \
	Type& getter()                                                            \
	{                                                                         \
		SW_LOG_ASSERT( s_services.member != nullptr, #Type " is not bound" ); \
		return *s_services.member;                                            \
	}
#define SW_ENGINE_SERVICE_CONST( member, Type, getter, required )             \
	const Type& getter()                                                      \
	{                                                                         \
		SW_LOG_ASSERT( s_services.member != nullptr, #Type " is not bound" ); \
		return *s_services.member;                                            \
	}
#define SW_ENGINE_SERVICE_OPT( member, Type, getter ) \
	Type* getter()                                    \
	{                                                 \
		return s_services.member;                     \
	}
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

		FrameDoubleBuffer& getFrameDoubleBuffer()
		{
			return FrameDoubleBuffer::get();
		}

		struct ModuleHeadRecord
		{
			TypeRegistrar*				   _pTypeHead{ nullptr };
			EnumRegistrar*				   _pEnumHead{ nullptr };
			sw::ComponentFactoryRegistrar* _pFactoryHead{ nullptr };
		};

		static unordered_map<string, ModuleHeadRecord>& getModuleHeadCache()
		{
			static unordered_map<string, ModuleHeadRecord> s_mapModuleHeads;
			return s_mapModuleHeads;
		}

		void registerModuleTypes( string_view moduleName )
		{
			auto&		 cache = getModuleHeadCache();
			const string modStr{ moduleName };

			TypeRegistrar*				   pTypeHead	= TypeRegistrar::getHead();
			EnumRegistrar*				   pEnumHead	= EnumRegistrar::getHead();
			sw::ComponentFactoryRegistrar* pFactoryHead = sw::ComponentFactoryRegistrar::getHead();

			auto it = cache.find( modStr );
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
			auto&		 cache = getModuleHeadCache();
			const string modStr{ moduleName };
			auto		 it = cache.find( modStr );
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
			getModuleHeadCache().erase( string( moduleName ) );
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

namespace sw
{
	TypeRegistry& getTypeRegistry()
	{
		return engine::getTypeRegistry();
	}

	TaskManager& getTaskManager()
	{
		return engine::getTaskManager();
	}
} // namespace sw
