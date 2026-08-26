#include "pch.h"

#include "RuntimeAPI/GameService.h"
#include "RuntimeAPI/PluginAPI.h"

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
		}

		SW_GAMESERVICE_API void unbindGameService() { s_gameService = {}; }

		SW_GAMESERVICE_API bool areGameServicesBound()
		{
			return s_gameService.getService != nullptr;
		}

		SW_GAMESERVICE_API void* getRawService( GameServiceId id )
		{
			SW_LOG_ASSERT( s_gameService.getService != nullptr, "GameService is not bound" );
			return s_gameService.getService( id );
		}
	} // namespace game
} // namespace sw
