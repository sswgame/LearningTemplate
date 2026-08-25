#include "pch.h"

#include "Engine/Common/EngineServices.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/ECS/Registry.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Task/TaskManager.h"

#include "RuntimeAPI/PluginAPI.h"

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
		}

		void unbindEngineServices()
		{
			s_services = {};
		}

		bool areEngineServicesBound()
		{
			return s_services._pCommandLineManager != nullptr && s_services._pGlobalVariableManager != nullptr &&
				   s_services._pTaskManager != nullptr && s_services._pTypeRegistry != nullptr &&
				   s_services._pSceneManager != nullptr && s_services._pInputManager != nullptr &&
				   s_services._pAudioSystem != nullptr && s_services._pEventDispatcher != nullptr &&
				   s_services._pResourceManager != nullptr;
		}

		CommandLineManager& getCommandLineManager()
		{
			SW_LOG_ASSERT( s_services._pCommandLineManager != nullptr, "CommandLineManager is not bound" );
			return *s_services._pCommandLineManager;
		}

		GlobalVariableManager& getGlobalVariableManager()
		{
			SW_LOG_ASSERT( s_services._pGlobalVariableManager != nullptr, "GlobalVariableManager is not bound" );
			return *s_services._pGlobalVariableManager;
		}

		LocalizationManager& getLocalizationManager()
		{
			SW_LOG_ASSERT( s_services._pLocalizationManager != nullptr, "LocalizationManager is not bound" );
			return *s_services._pLocalizationManager;
		}

		TaskManager& getTaskManager()
		{
			SW_LOG_ASSERT( s_services._pTaskManager != nullptr, "TaskManager is not bound" );
			return *s_services._pTaskManager;
		}

		TypeRegistry& getTypeRegistry()
		{
			SW_LOG_ASSERT( s_services._pTypeRegistry != nullptr, "TypeRegistry is not bound" );
			return *s_services._pTypeRegistry;
		}

		SceneManager& getSceneManager()
		{
			SW_LOG_ASSERT( s_services._pSceneManager != nullptr, "SceneManager is not bound" );
			return *s_services._pSceneManager;
		}

		InputManager& getInputManager()
		{
			SW_LOG_ASSERT( s_services._pInputManager != nullptr, "InputManager is not bound" );
			return *s_services._pInputManager;
		}

		IAudioSystem& getAudioSystem()
		{
			SW_LOG_ASSERT( s_services._pAudioSystem != nullptr, "IAudioSystem is not bound" );
			return *s_services._pAudioSystem;
		}

		EventDispatcher& getEventDispatcher()
		{
			SW_LOG_ASSERT( s_services._pEventDispatcher != nullptr, "EventDispatcher is not bound" );
			return *s_services._pEventDispatcher;
		}

		ResourceManager& getResourceManager()
		{
			SW_LOG_ASSERT( s_services._pResourceManager != nullptr, "ResourceManager is not bound" );
			return *s_services._pResourceManager;
		}

		MemoryProfiler* getMemoryProfiler()
		{
			return s_services._pMemoryProfiler;
		}

		const EngineData& getEngineData()
		{
			SW_LOG_ASSERT( s_services._pEngineData != nullptr, "EngineData is not bound" );
			return *s_services._pEngineData;
		}

		AssetStreamingQueue& getAssetStreamingQueue()
		{
			SW_LOG_ASSERT( s_services._pAssetStreamingQueue != nullptr, "AssetStreamingQueue is not bound" );
			return *s_services._pAssetStreamingQueue;
		}

		CommandStack& getCommandStack()
		{
			SW_LOG_ASSERT( s_services._pCommandStack != nullptr, "CommandStack is not bound" );
			return *s_services._pCommandStack;
		}

		DebugOverlayState& getDebugOverlayState()
		{
			SW_LOG_ASSERT( s_services._pDebugOverlayState != nullptr, "DebugOverlayState is not bound" );
			return *s_services._pDebugOverlayState;
		}

		FrameDoubleBuffer& getFrameDoubleBuffer()
		{
			SW_LOG_ASSERT( s_services._pFrameDoubleBuffer, "FrameDoubleBuffer is null." );
			return *s_services._pFrameDoubleBuffer;
		}

		DebugDrawQueue& getDebugDrawQueue()
		{
			SW_LOG_ASSERT( s_services._pDebugDrawQueue != nullptr, "DebugDrawQueue is not bound" );
			return *s_services._pDebugDrawQueue;
		}

		RHIBackendRegistry& getRHIBackendRegistry()
		{
			SW_LOG_ASSERT( s_services._pRHIBackendRegistry != nullptr, "RHIBackendRegistry is not bound" );
			return *s_services._pRHIBackendRegistry;
		}

		struct ModuleHeadRecord
		{
			TypeRegistrar*				   _pTypeHead{ nullptr };
			EnumRegistrar*				   _pEnumHead{ nullptr };
			sw::ComponentFactoryRegistrar* _pFactoryHead{ nullptr };
			ScriptSystemRegistrar*		   _pScriptHead{ nullptr };
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
			ScriptSystemRegistrar*		   pScriptHead	= ScriptSystemRegistrar::getHead();

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

				if ( pScriptHead == nullptr )
					pScriptHead = it->second._pScriptHead;
				else
					it->second._pScriptHead = pScriptHead;
			}
			else if ( pTypeHead != nullptr || pEnumHead != nullptr || pFactoryHead != nullptr || pScriptHead != nullptr )
			{
				cache[modStr] = ModuleHeadRecord{ pTypeHead, pEnumHead, pFactoryHead, pScriptHead };
			}

			registerModuleTypes( moduleName, pTypeHead, pEnumHead, pFactoryHead, pScriptHead );

			TypeRegistrar::getHead()				 = nullptr;
			EnumRegistrar::getHead()				 = nullptr;
			sw::ComponentFactoryRegistrar::getHead() = nullptr;
			ScriptSystemRegistrar::getHead()		 = nullptr;
		}

		void registerModuleTypes( string_view					 moduleName,
								  TypeRegistrar*				 pTypeHead,
								  EnumRegistrar*				 pEnumHead,
								  sw::ComponentFactoryRegistrar* pFactoryHead,
								  ScriptSystemRegistrar*		 pScriptHead )
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

				if ( pScriptHead != nullptr )
					it->second._pScriptHead = pScriptHead;
				else
					pScriptHead = it->second._pScriptHead;
			}
			else if ( pTypeHead != nullptr || pEnumHead != nullptr || pFactoryHead != nullptr || pScriptHead != nullptr )
			{
				cache[modStr] = ModuleHeadRecord{ pTypeHead, pEnumHead, pFactoryHead, pScriptHead };
			}

			getTypeRegistry().registerPendingTypes( moduleName, pTypeHead, pEnumHead );
			sw::Registry::registerModuleFactoryHead( moduleName, pFactoryHead );
			GameObjectManager::registerModuleScriptSystemHead( moduleName, pScriptHead );

			for ( const auto& scene : getSceneManager().getLoadedScenes() )
			{
				if ( scene && scene->getObjectManager() )
				{
					scene->getObjectManager()->registerPendingFactories( moduleName, pFactoryHead );
					scene->getObjectManager()->registerPendingScriptSystems( moduleName, pScriptHead );
					scene->getObjectManager()->rebindAllCachedTypeInfo();
				}
			}
		}

		void unregisterModuleTypes( string_view moduleName )
		{
			sw::Registry::unregisterModuleFactoryHead( moduleName );
			GameObjectManager::unregisterModuleScriptSystemHead( moduleName );
			for ( const auto& scene : getSceneManager().getLoadedScenes() )
			{
				if ( scene && scene->getObjectManager() )
				{
					scene->getObjectManager()->unregisterFactoriesByModule( moduleName );
					scene->getObjectManager()->clearAllCachedTypeInfo();
					scene->getObjectManager()->reinitScriptSystems();
				}
			}
			getTypeRegistry().unregisterTypesByModule( moduleName );
			getGlobalVariableManager().unregisterVariablesByModule( moduleName );
			if ( moduleName != "Engine" )
				getModuleHeadCache().erase( string( moduleName ) );
		}
	} // namespace engine
} // namespace sw

namespace sw
{
	FrameDoubleBuffer& getFrameDoubleBuffer()
	{
		return engine::getFrameDoubleBuffer();
	}

	TypeRegistry& getTypeRegistry()
	{
		return engine::getTypeRegistry();
	}

	TaskManager& getTaskManager()
	{
		return engine::getTaskManager();
	}
} // namespace sw
