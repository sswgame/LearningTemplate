/**
 * @file NileCityGame.h
 * @brief 도시 건설 키트(GF_CityBuilder)를 실제로 쓰는 시험 게임 — 나일 강 범람원에 농장을 두고 도로 · 우물 · 창고 · 시장 · 집을 지어 도시를 키우는 파라오 장르입니다.
 *
 * @details 빌드: `cmake --preset <preset> -DSW_ACTIVE_GAME=NileCity`. 조작은 `Source/Games/NileCity/README.md`.
 *          `-gv_nileAutoPlay=1` 이면 자동 계획표대로 짓고 달마다 `[Nile] month N pop P money M` 을 남긴다(입력 없이 도시가 크는 확인).
 */
#pragma once
#include "GameFramework/Framework/GameInstanceBase.h"
#include "GameFramework/Kits/Strategy/CityBuilder/CityCatalog.h"

#include "Games/NileCity/NileCityWorld.h"

namespace sw
{
    /** @brief 도시 데이터(`city.xml`)를 읽어 게임 서비스로 걸고 도시 한 판을 듭니다. */
    class NileCityGame : public GameInstanceBase
    {
    public:
        NileCityGame();
        ~NileCityGame() override;

    protected:
        bool onInitialize() override;
        void onShutdown() override;
        void onUpdate( float32 deltaTime ) override;
        void onBeforeStateSerialize() override;
        void onAfterStateDeserialize() override;

    private:
        CityCatalog   _cityCatalog;
        NileCityWorld _cityWorld;
    };
} // namespace sw
