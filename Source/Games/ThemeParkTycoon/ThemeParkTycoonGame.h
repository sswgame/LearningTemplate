/**
 * @file ThemeParkTycoonGame.h
 * @brief 테마파크 키트(GF_ThemePark)를 실제로 쓰는 시험 게임 — 코스터를 짓고(시험 운행이 평가를 매긴다) 손님을 받아 돈 · 평점을 키우는 경영 게임입니다.
 *
 * @details 빌드: `cmake --preset <preset> -DSW_ACTIVE_GAME=ThemeParkTycoon`. 조작은 `Source/Games/ThemeParkTycoon/README.md`.
 *          공원은 씬(`game/themepark/maps/park.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `ParkDirectorComponent` 가 섭니다.
 *          이 클래스는 첫 씬을 열고, 상태 저장 전에 디렉터의 공원 상태를 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 공원 상태를 돌려줍니다.
 *          `-gv_parkAutoBuild=1` 이면 돈이 모이는 대로 다음 놀이기구를 짓는다(입력 없이 공원이 크는 확인).
 */
#pragma once
#include "GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.h"

namespace sw
{
    /** @brief 공원 게임 인스턴스입니다. */
    class ThemeParkTycoonGame : public GameInstanceBase
    {
    public:
        ThemeParkTycoonGame();
        ~ThemeParkTycoonGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw
