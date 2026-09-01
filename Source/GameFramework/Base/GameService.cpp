#include "pch.h"

#include "GameFramework/Base/GameService.h"

#include "Core/Container/map.h"

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
		ModuleService	   s_gameService{};
		void*			   s_arrLocalHostOverrides[kModuleServiceCount]{ nullptr };
		map<uint64, void*> s_mapLocalService{};
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
				s_arrLocalHostOverrides[index] = nullptr;
			s_mapLocalService.clear();
		}

		SW_GAMESERVICE_API bool areGameServicesBound()
		{
			return s_gameService.getService != nullptr;
		}

		namespace internal
		{
			SW_GAMESERVICE_API void bindRawLocalService( uint64 typeHash, void* pService )
			{
				if ( pService != nullptr )
					s_mapLocalService[typeHash] = pService;
				else
					s_mapLocalService.erase( typeHash );
			}

			SW_GAMESERVICE_API void* getRawLocalService( uint64 typeHash )
			{
				const auto it = s_mapLocalService.find( typeHash );
				return it != s_mapLocalService.end() ? it->second : nullptr;
			}

			SW_GAMESERVICE_API void* getRawService( ModuleServiceId id )
			{
				const uint32 rawId = toRawServiceId( id );
				if ( rawId < kModuleServiceCount && s_arrLocalHostOverrides[rawId] != nullptr )
					return s_arrLocalHostOverrides[rawId];

				if ( s_gameService.getService == nullptr )
					return nullptr;

				if ( rawId >= kModuleServiceCount )
					return nullptr;
				return s_gameService.getService( rawId );
			}
		} // namespace internal
	} // namespace game
} // namespace sw
