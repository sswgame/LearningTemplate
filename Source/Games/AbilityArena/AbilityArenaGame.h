/**
 * @file AbilityArenaGame.h
 * @brief 어빌리티 시스템(GameFramework/Ability)을 실제로 쓰는 시험 게임 — 웨이브를 막는 탑다운 아레나입니다.
 *
 * @details 빌드: `cmake --preset <preset> -DSW_ACTIVE_GAME=AbilityArena` (기존 빌드 디렉터리는 옛 값을 들고 있으니 다시 구성한다).
 *          조작: WASD 이동, J/Space 근접, K/2 화염구(마나 · 화상), L/3 회복(데이터만), LeftShift/4 대시(무적). `-gv_arenaAutoPlay=1` 이면
 *          플레이어도 AI 가 움직인다. 자세한 것은 `Source/Games/AbilityArena/README.md`.
 */
#pragma once
#include "GameFramework/Ability/AbilityCatalog.h"
#include "GameFramework/Framework/GameInstanceBase.h"

#include "Games/AbilityArena/ArenaWorld.h"

namespace sw
{
    /** @brief 카탈로그(데이터 + 클래스 등록)와 아레나 규칙을 들고, 둘을 게임 서비스로 겁니다. */
    class AbilityArenaGame : public GameInstanceBase
    {
    public:
        AbilityArenaGame();
        ~AbilityArenaGame() override;

    protected:
        bool onInitialize() override;
        void onShutdown() override;
        void onUpdate( float32 deltaTime ) override;
        /** @brief 상태 스냅샷 직전 — 아레나는 절차 생성물이라 걷어서 스냅샷에 싣지 않는다(벤치와 같다). */
        void onBeforeStateSerialize() override;
        /** @brief 상태 복원이 씬을 갈아 끼운 뒤 — 아레나를 다시 세운다. */
        void onAfterStateDeserialize() override;

    private:
        AbilityCatalog _abilityCatalog;
        ArenaWorld     _arenaWorld;
    };
} // namespace sw
