#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "Engine/Resource/AssetManager.h"

#include "GameFramework/Base/Foundation/Data/GameDataCache.h"
#include "GameFramework/GameFrameworkExports.h"

SW_LOG_CALLER( "GameService" );
namespace sw
{
    namespace
    {
        /** @brief App 이 넘긴 호스트 서비스 범위와 지금 게임 범위입니다. GameFramework.dll 의 정적이라 게임 · 키트 이미지보다 오래 산다. */
        struct GameServiceState
        {
            const ISubsystemLocator* _pHostServices{ nullptr };
            SubsystemScope*          _pGameScope{ nullptr };
        };

        GameServiceState s_gameServiceState{};
    } // namespace

    namespace game
    {
        SW_GAMESERVICE_API void bindGameService( const ISubsystemLocator& hostServices )
        {
            s_gameServiceState._pHostServices = &hostServices;
            // 데이터 표 캐시(상호작용 · 원소 규칙)를 호스트의 에셋 캐시 레지스트리에 올린다 — 핫 리로드 · 비우기 · 진단이 그 레지스트리를 훑는다.
            GameDataCacheRegistry::attach( getService<AssetManager>() );
        }

        SW_GAMESERVICE_API void unbindGameService()
        {
            GameDataCacheRegistry::detach();
            s_gameServiceState._pHostServices = nullptr;
        }

        SW_GAMESERVICE_API SubsystemScope* findGameScope()
        {
            return s_gameServiceState._pGameScope;
        }

        SW_GAMESERVICE_API void setGameScope( SubsystemScope* pScope )
        {
            s_gameServiceState._pGameScope = pScope;
        }

        namespace internal
        {
            SW_GAMESERVICE_API const ISubsystemLocator* findHostServices()
            {
                return s_gameServiceState._pHostServices;
            }
        } // namespace internal
    } // namespace game
} // namespace sw
