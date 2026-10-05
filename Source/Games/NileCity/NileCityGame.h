/**
 * @file NileCityGame.h
 * @brief 도시 건설 키트(GF_CityBuilder)를 실제로 쓰는 시험 게임 — 나일 강 범람원에 농장을 두고 도로 · 우물 · 창고 · 시장 · 집을 지어 도시를 키우는 파라오 장르입니다.
 *
 * @details 빌드: `cmake --preset Ninja-Debug-NileCity`. 조작은 `Source/Games/NileCity/README.md`.
 *          도시는 씬(`game/nilecity/maps/nile.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `NileDirectorComponent` 가 섭니다.
 *          이 클래스는 첫 씬을 열고, 상태 저장 전에 디렉터의 도시 상태를 싣고 디렉터가 세운 런타임 오브젝트를 걷으며, 복원 뒤 도시 상태를 돌려줍니다.
 *          `-gv_nileAutoPlay=1` 이면 자동 계획표대로 짓고 달마다 `[Nile] month N pop P money M` 을 남긴다(입력 없이 도시가 크는 확인).
 */
#pragma once
#include "GameFramework/Framework/GameInstanceBase.h"

namespace sw
{
    /** @brief 나일 도시 게임 인스턴스입니다. */
    class NileCityGame : public GameInstanceBase
    {
    public:
        NileCityGame();
        ~NileCityGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw
