#include "pch.h"

#include "RuntimeAPI/GameService.h"
#include "RuntimeAPI/PluginAPI.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/ECS/Registry.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Localization/StringTable.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	SW_GF_API void registerGameFrameworkTypes()
	{
		if ( game::areGameServicesBound() )
		{
			engine::registerModuleTypes( "GameFramework" );
		}
	}

	namespace
	{
		GameService s_gameService{};
	} // namespace

	namespace game
	{
		SW_GAMESERVICE_API void bindGameService( const GameService& service )
		{
			s_gameService = service;
			if ( s_gameService.getService != nullptr )
			{
				if ( s_gameService._pLocalizationManager == nullptr )
					s_gameService._pLocalizationManager = static_cast<LocalizationManager*>( s_gameService.getService( GameServiceId::LocalizationManager ) );
				if ( s_gameService._pEventDispatcher == nullptr )
					s_gameService._pEventDispatcher = static_cast<EventDispatcher*>( s_gameService.getService( GameServiceId::EventDispatcher ) );
				if ( s_gameService._pGlobalVariableManager == nullptr )
					s_gameService._pGlobalVariableManager = static_cast<GlobalVariableManager*>( s_gameService.getService( GameServiceId::GlobalVariableManager ) );
				if ( s_gameService._pAudioSystem == nullptr )
					s_gameService._pAudioSystem = static_cast<IAudioSystem*>( s_gameService.getService( GameServiceId::AudioSystem ) );
				if ( s_gameService._pTypeRegistry == nullptr )
					s_gameService._pTypeRegistry = static_cast<TypeRegistry*>( s_gameService.getService( GameServiceId::TypeRegistry ) );
				if ( s_gameService._pInputManager == nullptr )
					s_gameService._pInputManager = static_cast<InputManager*>( s_gameService.getService( GameServiceId::InputManager ) );
				if ( s_gameService._pSceneManager == nullptr )
					s_gameService._pSceneManager = static_cast<SceneManager*>( s_gameService.getService( GameServiceId::SceneManager ) );
				if ( s_gameService._pResourceManager == nullptr )
					s_gameService._pResourceManager = static_cast<ResourceManager*>( s_gameService.getService( GameServiceId::ResourceManager ) );
				if ( s_gameService._pDebugDrawQueue == nullptr )
					s_gameService._pDebugDrawQueue = static_cast<DebugDrawQueue*>( s_gameService.getService( GameServiceId::DebugDrawQueue ) );
				if ( s_gameService._pDebugOverlayState == nullptr )
					s_gameService._pDebugOverlayState = static_cast<DebugOverlayState*>( s_gameService.getService( GameServiceId::DebugOverlayState ) );
			}
		}

		SW_GAMESERVICE_API void unbindGameService() { s_gameService = {}; }

		SW_GAMESERVICE_API bool areGameServicesBound()
		{
			return s_gameService.getService != nullptr;
		}

		SW_GAMESERVICE_API const GameService& getBoundGameService()
		{
			return s_gameService;
		}

		SW_GAMESERVICE_API void* getRawService( GameServiceId id )
		{
			SW_LOG_ASSERT( s_gameService.getService != nullptr, "GameService is not bound" );
			return s_gameService.getService( id );
		}
	} // namespace game
} // namespace sw
