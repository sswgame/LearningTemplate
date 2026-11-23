/**
 * @file FrontierTownGame.cpp
 * @brief 키트 조립의 둘째 테스트 게임 — 도시(GF_CityBuilder)와 RTS(GF_RealTimeStrategy)를 한 씬 · 한 공유 상태(땅 · 지갑 · 시계)에 섞습니다.
 * @details 실행: `App.exe -game=FrontierTown`(`Ninja-Debug` 빌드). 씬(`game/playground/frontiertown/maps/frontier.scene.xml`)의 `TownState` 오브젝트에
 *          `GameStateComponent` → `WalletComponent` → `CityGameModeComponent` → `CityViewComponent` → `FrontierSkirmishGameModeComponent` 가 그 순서로 붙어 시계 → 도시 → RTS 순서로 돈다.
 *          판은 Playground 미니게임 frontiertown 이 돌린다 — 이 게임은 다음 커밋이 지운다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 둘째 조립 테스트 게임 인스턴스입니다. */
    class FrontierTownGame : public GameInstance
    {
    public:
        FrontierTownGame();
        ~FrontierTownGame() override;
    };
} // namespace sw

namespace sw
{
    FrontierTownGame::FrontierTownGame()
    {
        // 공유 상태(`GameStateComponent`)는 프레임워크가 먼저 올렸다 — 복원할 때 디렉터보다 먼저 바이트를 받아 두고, 첫 디렉터가 판을 열며 적용한다.
        getStateSnapshot().registerGameMode<CityGameModeComponent>();
    }

    FrontierTownGame::~FrontierTownGame() = default;
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::FrontierTownGame );
