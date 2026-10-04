/**
 * @file HarvestValleyGame.h
 * @brief 농장 키트(GF_Farming)를 실제로 쓰는 시험 게임 — 밭을 갈고 물 주고 심어 거두고 출하하며 날 · 계절을 넘깁니다(하베스트 문 장르).
 *
 * @details 빌드: `cmake --preset Ninja-Debug-HarvestValley`. 조작은 `Source/Games/HarvestValley/README.md`.
 *          농장은 씬(`game/harvestvalley/maps/farm.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `FarmDirectorComponent` 가 섭니다.
 *          이 클래스는 첫 씬을 열고, 상태 저장 전에 디렉터의 농장 상태를 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 농장 상태를 돌려줍니다.
 *          `-gv_farmAutoPlay=1` 이면 농부도 AI 가 움직인다(입력 없이 날을 넘겨 보는 확인).
 */
#pragma once
#include "GameFramework/Framework/GameInstanceBase.h"

namespace sw
{
    /** @brief 농장 게임 인스턴스입니다. */
    class HarvestValleyGame : public GameInstanceBase
    {
    public:
        HarvestValleyGame();
        ~HarvestValleyGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw
