#include "pch.h"

#include "RuntimeAPI/PluginAPI.h"
#include "RuntimeAPI/Service/GameService.h"

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
		ModuleService s_gameService{};
	} // namespace

	namespace game
	{
		SW_GAMESERVICE_API void bindGameService( const ModuleService& service )
		{
			s_gameService = service;
		}

		SW_GAMESERVICE_API void unbindGameService() { s_gameService = {}; }

		SW_GAMESERVICE_API bool areGameServicesBound()
		{
			return s_gameService.getService != nullptr;
		}

		SW_GAMESERVICE_API void* getRawService( ModuleServiceId id )
		{
			SW_LOG_ASSERT( s_gameService.getService != nullptr, "GameService is not bound" );
			const uint32 rawId = toRawServiceId( id );
			if ( rawId >= kModuleServiceCount )
				return nullptr;
			return s_gameService.getService( rawId );
		}
	} // namespace game
} // namespace sw
