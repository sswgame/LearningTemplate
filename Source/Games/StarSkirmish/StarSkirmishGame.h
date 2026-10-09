/**
 * @file StarSkirmishGame.h
 * @brief 실시간 전략 키트(GF_RealTimeStrategy)를 실제로 쓰는 시험 게임 — 일꾼으로 광물 · 가스를 캐고 보급고 · 병영을 지어 병력으로 상대 건물을 모두 부수는 1 대 1 입니다.
 *
 * @details 빌드: `cmake --preset Ninja-Debug-StarSkirmish`. 조작은 `Source/Games/StarSkirmish/README.md`.
 *          전장은 씬(`game/starskirmish/maps/skirmish.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `SkirmishDirectorComponent` 가 섭니다.
 *          이 클래스는 첫 씬을 열고, 상태 저장 전에 디렉터의 판을 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 판을 돌려줍니다.
 *          기본은 사람(파랑) 대 `RtsAiCommander`(빨강), `-gv_skirmishAutoPlay=1` 이면 AI 대 AI 로 승패까지 돌린다(입력 없이 한 판을 끝내는 확인).
 */
#pragma once
#include "GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.h"

namespace sw
{
    /** @brief 전장 게임 인스턴스입니다. */
    class StarSkirmishGame : public GameInstanceBase
    {
    public:
        StarSkirmishGame();
        ~StarSkirmishGame() override;

    protected:
        bool onInitialize() override;
        void onShutdown() override;
    };
} // namespace sw
