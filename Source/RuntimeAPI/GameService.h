/**
 * @file GameService.h
 * @brief App이 채우고 게임 모듈이 쓰는 서비스 로케이터.
 * @note GameFramework 헤더를 포함하지 않습니다. 구현은 GameFramework.dll이 export합니다.
 */
#pragma once
#include "Core/Common/Macros.h"

#if defined( SW_PLATFORM_WINDOWS )
	#if defined( SW_GF_EXPORTS )
		#define SW_GAMESERVICE_API __declspec( dllexport )
	#elif defined( SW_GF_IMPORTS )
		#define SW_GAMESERVICE_API __declspec( dllimport )
	#else
		#define SW_GAMESERVICE_API
	#endif
#else
	#define SW_GAMESERVICE_API __attribute__( ( visibility( "default" ) ) )
#endif

namespace sw
{
	class LocalizationManager;
	class EventDispatcher;
	class GlobalVariableManager;
	class IAudioSystem;
	class TypeRegistry;
	class InputManager;
	class SceneManager;
	class ResourceManager;
	class DebugDrawQueue;
	struct DebugOverlayState;

	enum class GameServiceId : uint32
	{
		LocalizationManager = 0,
		EventDispatcher,
		GlobalVariableManager,
		AudioSystem,
		TypeRegistry,
		InputManager,
		SceneManager,
		ResourceManager,
		DebugDrawQueue,
		DebugOverlayState,
	};

	struct GameService
	{
		void* ( *getService )(GameServiceId id){ nullptr };
	};

	template <typename T>
	struct GameServiceTraits;

#define SW_DECLARE_GAME_SERVICE( Type, Id )     \
	template <>                                 \
	struct GameServiceTraits<Type>              \
	{                                           \
		static constexpr GameServiceId id = Id; \
	}

	SW_DECLARE_GAME_SERVICE( LocalizationManager, GameServiceId::LocalizationManager );
	SW_DECLARE_GAME_SERVICE( EventDispatcher, GameServiceId::EventDispatcher );
	SW_DECLARE_GAME_SERVICE( GlobalVariableManager, GameServiceId::GlobalVariableManager );
	SW_DECLARE_GAME_SERVICE( IAudioSystem, GameServiceId::AudioSystem );
	SW_DECLARE_GAME_SERVICE( TypeRegistry, GameServiceId::TypeRegistry );
	SW_DECLARE_GAME_SERVICE( InputManager, GameServiceId::InputManager );
	SW_DECLARE_GAME_SERVICE( SceneManager, GameServiceId::SceneManager );
	SW_DECLARE_GAME_SERVICE( ResourceManager, GameServiceId::ResourceManager );
	SW_DECLARE_GAME_SERVICE( DebugDrawQueue, GameServiceId::DebugDrawQueue );
	SW_DECLARE_GAME_SERVICE( DebugOverlayState, GameServiceId::DebugOverlayState );

	namespace game
	{
		SW_GAMESERVICE_API void bindGameService( const GameService& service );
		SW_GAMESERVICE_API void unbindGameService();
		/** @brief 서비스 콜백이 바인딩되었는지 확인합니다. */
		SW_GAMESERVICE_API bool areGameServicesBound();

		SW_GAMESERVICE_API void* getRawService( GameServiceId id );

		template <typename T>
		T* getService()
		{
			return static_cast<T*>( getRawService( GameServiceTraits<T>::id ) );
		}
	} // namespace game
} // namespace sw
