/**
 * @file GameService.h
 * @brief App이 채우고 게임 모듈이 쓰는 서비스 포인터 묶음.
 * @note GameFramework 헤더를 포함하지 않습니다. 구현은 GameFramework.dll이 export합니다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include <type_traits>

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

	enum class GameServiceId : uint32_t
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
		LocalizationManager*   _pLocalizationManager{ nullptr };
		EventDispatcher*	   _pEventDispatcher{ nullptr };
		GlobalVariableManager* _pGlobalVariableManager{ nullptr };
		IAudioSystem*		   _pAudioSystem{ nullptr };
		TypeRegistry*		   _pTypeRegistry{ nullptr };
		InputManager*		   _pInputManager{ nullptr };
		SceneManager*		   _pSceneManager{ nullptr };
		ResourceManager*	   _pResourceManager{ nullptr };
		DebugDrawQueue*		   _pDebugDrawQueue{ nullptr };
		DebugOverlayState*	   _pDebugOverlayState{ nullptr };
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
		/** @brief 필수 서비스 포인터가 모두 바인딩되었는지 확인합니다. */
		SW_GAMESERVICE_API bool areGameServicesBound();

		SW_GAMESERVICE_API const GameService& getBoundGameService();

		SW_GAMESERVICE_API void* getRawService( GameServiceId id );

		template <typename T>
		T* getService()
		{
			const GameService& gs = getBoundGameService();
			if constexpr ( std::is_same_v<T, LocalizationManager> )
				return gs._pLocalizationManager;
			else if constexpr ( std::is_same_v<T, EventDispatcher> )
				return gs._pEventDispatcher;
			else if constexpr ( std::is_same_v<T, GlobalVariableManager> )
				return gs._pGlobalVariableManager;
			else if constexpr ( std::is_same_v<T, IAudioSystem> )
				return gs._pAudioSystem;
			else if constexpr ( std::is_same_v<T, TypeRegistry> )
				return gs._pTypeRegistry;
			else if constexpr ( std::is_same_v<T, InputManager> )
				return gs._pInputManager;
			else if constexpr ( std::is_same_v<T, SceneManager> )
				return gs._pSceneManager;
			else if constexpr ( std::is_same_v<T, ResourceManager> )
				return gs._pResourceManager;
			else if constexpr ( std::is_same_v<T, DebugDrawQueue> )
				return gs._pDebugDrawQueue;
			else if constexpr ( std::is_same_v<T, DebugOverlayState> )
				return gs._pDebugOverlayState;
			else
				return static_cast<T*>( getRawService( GameServiceTraits<T>::id ) );
		}
	} // namespace game
} // namespace sw
