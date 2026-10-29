/**
 * @file FrontierTownGame.cpp
 * @brief 키트 조립의 둘째 테스트 게임 — 도시(GF_CityBuilder)와 RTS(GF_RealTimeStrategy)를 한 씬 · 한 공유 상태(땅 · 지갑 · 시계)에 섞습니다.
 * @details 실행: `App.exe -game=FrontierTown`(`Ninja-Debug` 빌드). 씬(`game/frontiertown/maps/frontier.scene.xml`)의 `TownState` 오브젝트에
 *          `SessionStateComponent` → `FrontierCityDirectorComponent` → `FrontierSkirmishDirectorComponent` 가 그 순서로 붙어 시계 → 도시 → RTS 순서로 돈다.
 *          이 클래스는 카탈로그 둘을 읽어 게임 범위에 걸고(디렉터가 찾는다), 공유 상태와 두 디렉터를 상태 스냅숏에 올린다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"

#include "Games/FrontierTown/FrontierCityDirectorComponent.h"
#include "Games/FrontierTown/FrontierSkirmishDirectorComponent.h"

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
        CityCatalog _cityCatalog;
        RTSCatalog  _unitCatalog;
    };
} // namespace sw

namespace sw
{
    FrontierTownGame::FrontierTownGame()
        : _cityCatalog{}
        , _unitCatalog{}
    {
        // 공유 상태가 먼저 — 복원할 때 디렉터보다 먼저 바이트를 받아 두고, 첫 디렉터가 판을 열며 적용한다.
        getStateSnapshot().registerStatefulComponent<SessionStateComponent>();
        getStateSnapshot().registerDirector<FrontierCityDirectorComponent>();
        getStateSnapshot().registerDirector<FrontierSkirmishDirectorComponent>();
    }

    FrontierTownGame::~FrontierTownGame() = default;

    bool FrontierTownGame::onInitialize()
    {
        // 데이터 둘은 필수다 — 하나라도 못 읽으면 게임을 띄우지 않는다. 읽은 것은 디렉터가 찾는 게임 범위에 걸린다.
        const bool bLoaded = loadRequiredCatalog( _cityCatalog ) && loadRequiredCatalog( _unitCatalog );
        if ( bLoaded == false )
            return false;
        (void)getSubsystem<GameSceneFlow>().requestFirstScene(); // 실패는 함수가 알리고, 핫 리로드로 다시 선 인스턴스는 살아 있는 씬을 쓴다
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::FrontierTownGame );
