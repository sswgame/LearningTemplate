/**
 * @file StarSkirmishGame.cpp
 * @brief 실시간 전략 키트(GF_RealTimeStrategy)를 실제로 쓰는 테스트 게임 — 일꾼으로 광물 · 가스를 캐고 보급고 · 병영을 지어 병력으로 상대 건물을 모두 부수는 1 대 1 입니다.
 *
 * @details 실행: `App.exe -game=StarSkirmish`(`Ninja-Debug` 빌드). 조작은 `Source/Games/StarSkirmish/README.md`.
 *          전장은 씬(`game/starskirmish/maps/skirmish.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `SkirmishDirectorComponent` 가 섭니다.
 *          이 클래스는 첫 씬을 열고, 상태 저장 전에 디렉터의 판을 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 판을 돌려줍니다.
 *          기본은 사람(파랑) 대 `RTSCommanderAI`(빨강), `-gv_skirmishAutoPlay=1` 이면 AI 대 AI 로 승패까지 돌린다(입력 없이 한 판을 끝내는 확인).
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"

#include "Games/StarSkirmish/SkirmishDirectorComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 전장 게임 인스턴스입니다. */
    class StarSkirmishGame : public GameInstance
    {
    public:
        StarSkirmishGame();
        ~StarSkirmishGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw

namespace sw
{
    StarSkirmishGame::StarSkirmishGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        registerDirector<SkirmishDirectorComponent>();
    }

    StarSkirmishGame::~StarSkirmishGame() = default;

    bool StarSkirmishGame::onInitialize()
    {
        // 사운드 이벤트(알림은 ui 버스 2D, 부서진 유닛은 그 자리)는 베이스가 gamesettings 의 audioEvents 로 올렸다.
        // 전장 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::StarSkirmishGame );
