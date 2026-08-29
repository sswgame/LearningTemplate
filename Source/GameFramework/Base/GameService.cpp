#include "pch.h"

#include "RuntimeAPI/Service/GameService.h"

#include "GameFramework/GameFrameworkExports.h"

#include "RuntimeAPI/PluginAPI.h"

SW_LOG_CALLER( "GameService" );
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
		void*		  s_arrLocalServices[kModuleServiceCount]{ nullptr };
	} // namespace

	namespace game
	{
		SW_GAMESERVICE_API void bindGameService( const ModuleService& service )
		{
			s_gameService = service;
		}

		SW_GAMESERVICE_API void unbindGameService()
		{
			s_gameService = {};
			for ( uint32 index = 0; index < kModuleServiceCount; ++index )
				s_arrLocalServices[index] = nullptr;
		}

		SW_GAMESERVICE_API bool areGameServicesBound()
		{
			return s_gameService.getService != nullptr;
		}

		SW_GAMESERVICE_API void bindLocalService( ModuleServiceId id, void* pService )
		{
			const uint32 rawId = toRawServiceId( id );
			if ( rawId < kModuleServiceCount )
				s_arrLocalServices[rawId] = pService;
		}

		SW_GAMESERVICE_API void* getRawService( ModuleServiceId id )
		{
			const uint32 rawId = toRawServiceId( id );
			if ( rawId < kModuleServiceCount && s_arrLocalServices[rawId] != nullptr )
				return s_arrLocalServices[rawId];

			if ( s_gameService.getService == nullptr )
				return nullptr;

			if ( rawId >= kModuleServiceCount )
				return nullptr;
			return s_gameService.getService( rawId );
		}
	} // namespace game
} // namespace sw
