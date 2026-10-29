/**
 * @file HarvestValleyGame.cpp
 * @brief 농장 키트(GF_Farming)를 실제로 쓰는 테스트 게임 — 밭을 갈고 물 주고 심어 거두고 출하하며 날 · 계절을 넘깁니다(하베스트 문 장르).
 *
 * @details 실행: `App.exe -game=HarvestValley`(`Ninja-Debug` 빌드). 조작은 `Source/Games/HarvestValley/README.md`.
 *          농장은 씬(`game/harvestvalley/maps/farm.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `FarmDirectorComponent` 가 섭니다.
 *          이 클래스는 첫 씬을 열고, 상태 저장 전에 디렉터의 농장 상태를 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 농장 상태를 돌려줍니다.
 *          `-gv_farmAutoPlay=1` 이면 농부도 AI 가 움직인다(입력 없이 날을 넘겨 보는 확인).
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"
#include "Games/HarvestValley/FarmVillageDirectorComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 농장 게임 인스턴스입니다. */
    class HarvestValleyGame : public GameInstance
    {
    public:
        HarvestValleyGame();
        ~HarvestValleyGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw

namespace sw
{
    HarvestValleyGame::HarvestValleyGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        getStateSnapshot().registerStatefulComponent<SessionStateComponent>(); // 공유 상태가 디렉터보다 먼저 돌아온다
        getStateSnapshot().registerDirector<FarmDirectorComponent>();
        getStateSnapshot().registerDirector<FarmVillageDirectorComponent>(); // 농장 디렉터 뒤 — 같은 시계로 일정을 다시 만든다
    }

    HarvestValleyGame::~HarvestValleyGame() = default;

    bool HarvestValleyGame::onInitialize()
    {
        // 사운드 이벤트(모두 2D — 위에서 내려다보는 농장)는 베이스가 gamesettings 의 audioEvents 로 올렸다.
        // 농장 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::HarvestValleyGame );
