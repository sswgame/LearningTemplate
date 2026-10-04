/**
 * @file AbilityArenaGame.h
 * @brief 어빌리티 시스템(GameFramework/Ability)을 실제로 쓰는 시험 게임 — 웨이브를 막는 탑다운 아레나입니다.
 *
 * @details 빌드: `cmake --preset Ninja-Debug-AbilityArena`. 조작: WASD 이동, J/Space 근접, K/2 화염구(마나 · 화상), L/3 회복(데이터만),
 *          LeftShift/4 대시(무적). `-gv_arenaAutoPlay=1` 이면 플레이어도 AI 가 움직인다. 자세한 것은 `Source/Games/AbilityArena/README.md`.
 *          아레나는 씬(`game/abilityarena/maps/arena.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `ArenaDirectorComponent` 가 섭니다.
 *          이 클래스는 어빌리티 카탈로그를 들고 게임 서비스로 걸고, 첫 씬을 열고, 상태 저장 전에 판의 진행(웨이브 · 처치 수)을 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 진행을 돌려줍니다.
 */
#pragma once
#include "GameFramework/Ability/AbilityCatalog.h"
#include "GameFramework/Framework/GameInstanceBase.h"

namespace sw
{
    /** @brief 카탈로그(데이터 + 클래스 등록)를 들고 게임 서비스로 겁니다 — 모듈이 다시 올라오면 새 인스턴스가 다시 건다. */
    class AbilityArenaGame : public GameInstanceBase
    {
    public:
        AbilityArenaGame();
        ~AbilityArenaGame() override;

    protected:
        bool onInitialize() override;
        void onShutdown() override;

    private:
        AbilityCatalog _abilityCatalog;
    };
} // namespace sw
