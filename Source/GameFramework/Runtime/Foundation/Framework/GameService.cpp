#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Engine/Foundation/EngineScope.h"

#include "GameFramework/GameFrameworkExports.h"

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
            // 카탈로그의 공유 캐시는 그 카탈로그를 가진 모듈(GameFramework · 키트 · 게임)의 모듈 서브시스템이 서며 에셋 캐시 레지스트리에 스스로 오른다(`SW_MODULE_SHARED_ASSET_CACHE`).
            s_gameServiceState._pHostServices = &hostServices;
        }

        SW_GAMESERVICE_API void unbindGameService()
        {
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
                // 호스트가 게임 서비스를 묶지 않은 실행(쿠커 · 테스트 하네스)은 게임 모듈을 돌리지 않으므로 엔진 범위를 그대로 본다.
                if ( s_gameServiceState._pHostServices == nullptr )
                    return engine::getEngineSubsystems();
                return s_gameServiceState._pHostServices;
            }
        } // namespace internal
    } // namespace game
} // namespace sw
