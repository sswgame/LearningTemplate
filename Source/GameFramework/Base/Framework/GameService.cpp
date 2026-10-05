#include "pch.h"

#include "GameFramework/Base/Framework/GameService.h"

#include "Core/Container/map.h"

#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Resource/AssetManager.h"

#include "GameFramework/Base/Data/GameDataCache.h"
#include "GameFramework/GameFrameworkExports.h"

SW_LOG_CALLER( "GameService" );
namespace sw
{
    namespace
    {
        ModuleService      s_gameService{};
        map<uint64, void*> s_mapLocalService{};
    } // namespace

    namespace game
    {
        SW_GAMESERVICE_API void bindGameService( const ModuleService& service )
        {
            s_gameService = service;
            // 데이터 표 캐시(상호작용 · 원소 규칙)를 호스트의 에셋 캐시 등록부에 올린다 — 핫 리로드 · 비우기 · 진단이 그 등록부를 훑는다.
            GameDataCacheRegistry::attach( getService<AssetManager>() );
        }

        SW_GAMESERVICE_API void unbindGameService()
        {
            GameDataCacheRegistry::detach();
            s_gameService = {};
            s_mapLocalService.clear();
        }

        SW_GAMESERVICE_API bool areGameServicesBound()
        {
            return s_gameService.arrServices[sw::internal::toRawServiceId( sw::internal::ModuleServiceId::SceneManager )] != nullptr;
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

            SW_GAMESERVICE_API void* getRawService( sw::internal::ModuleServiceId id )
            {
                const uint32 rawId = sw::internal::toRawServiceId( id );
                if ( rawId >= sw::internal::kModuleServiceCount )
                    return nullptr;
                return const_cast<void*>( s_gameService.arrServices[rawId] );
            }
        } // namespace internal
    } // namespace game
} // namespace sw
