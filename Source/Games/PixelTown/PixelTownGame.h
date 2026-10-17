/**
 * @file PixelTownGame.h
 * @brief 오버월드 키트(GF_Overworld)를 쓰는 탑다운 RPG 2D 테스트 게임 — 타일 마을을 한 칸씩 걷고, 물 · 집 · 나무에 막히며, 주민과 대화합니다.
 *
 * @details 실행: `App.exe -game=PixelTown`(`Ninja-Debug` 빌드). 조작은 `Source/Games/PixelTown/README.md`.
 *          마을은 씬(`game/pixeltown/maps/town.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `TownDirectorComponent` 가 섭니다.
 *          이 클래스는 첫 씬을 열고, 디렉터를 상태 스냅샷에 올립니다.
 */
#pragma once
#include "GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.h"

namespace sw
{
    /** @brief 픽셀 마을 게임 인스턴스입니다. */
    class PixelTownGame : public GameInstanceBase
    {
    public:
        PixelTownGame();
        ~PixelTownGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw
