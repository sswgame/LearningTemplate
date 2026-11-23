/**
 * @file FrontierTownGame.cpp
 * @brief 키트 조립의 둘째 테스트 게임 — 도시(GF_CityBuilder)와 RTS(GF_RealTimeStrategy)를 한 씬 · 한 공유 상태(땅 · 지갑 · 시계)에 섞습니다.
 * @details 실행: `App.exe -game=FrontierTown`(`Ninja-Debug` 빌드). 씬(`game/frontiertown/maps/frontier.scene.xml`)의 `TownState` 오브젝트에
 *          `GameStateComponent` → `WalletComponent` → `FrontierCityGameModeComponent` → `FrontierSkirmishGameModeComponent` 가 그 순서로 붙어 시계 → 도시 → RTS 순서로 돈다.
 *          이 클래스는 카탈로그 둘을 읽어 게임 범위에 걸고(디렉터가 찾는다), 두 디렉터를 상태 스냅숏에 올린다.
 */
#include "pch.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"

#include "Games/FrontierTown/FrontierCityGameModeComponent.h"
#include "Games/FrontierTown/FrontierSkirmishGameModeComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 둘째 조립 테스트 게임 인스턴스입니다. */
    class FrontierTownGame : public GameInstance
    {
    public:
        FrontierTownGame();
        ~FrontierTownGame() override;

    protected:
        bool onInitialize() override;

    private:
        SharedAssetRef<CityCatalog> _pCityCatalog; ///< 공유 캐시의 한 벌(게임 범위 서비스)
        SharedAssetRef<RTSCatalog>  _pUnitCatalog; ///< 공유 캐시의 한 벌(게임 범위 서비스)
    };
} // namespace sw

namespace sw
{
    FrontierTownGame::FrontierTownGame()
        : _pCityCatalog{}
        , _pUnitCatalog{}
    {
        // 공유 상태(`GameStateComponent`)는 프레임워크가 먼저 올렸다 — 복원할 때 디렉터보다 먼저 바이트를 받아 두고, 첫 디렉터가 판을 열며 적용한다.
        getStateSnapshot().registerGameMode<FrontierCityGameModeComponent>();
        getStateSnapshot().registerGameMode<FrontierSkirmishGameModeComponent>();
    }

    FrontierTownGame::~FrontierTownGame() = default;

    bool FrontierTownGame::onInitialize()
    {
        // 데이터 둘은 필수다 — 하나라도 못 읽으면 게임을 띄우지 않는다. 읽은 것은 디렉터가 찾는 게임 범위에 걸린다.
        const bool bLoaded = loadRequiredCatalog( _pCityCatalog ) && loadRequiredCatalog( _pUnitCatalog );
        if ( bLoaded == false )
            return false;
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::FrontierTownGame );
