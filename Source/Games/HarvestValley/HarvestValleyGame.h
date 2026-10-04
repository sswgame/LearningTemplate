/**
 * @file HarvestValleyGame.h
 * @brief 농장 키트(GF_Farming)를 실제로 쓰는 시험 게임 — 밭을 갈고 물 주고 심어 거두고 출하하며 날 · 계절을 넘깁니다(하베스트 문 장르).
 *
 * @details 빌드: `cmake --preset <preset> -DSW_ACTIVE_GAME=HarvestValley`. 조작은 `Source/Games/HarvestValley/README.md`.
 *          `-gv_farmAutoPlay=1` 이면 농부도 AI 가 움직인다(입력 없이 날을 넘겨 보는 확인).
 */
#pragma once
#include "GameFramework/Framework/GameInstanceBase.h"
#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"

#include "Games/HarvestValley/FarmWorld.h"

namespace sw
{
    /** @brief 작물 카탈로그와 농장 규칙을 들고, 카탈로그를 게임 서비스로 겁니다. */
    class HarvestValleyGame : public GameInstanceBase
    {
    public:
        HarvestValleyGame();
        ~HarvestValleyGame() override;

    protected:
        bool onInitialize() override;
        void onShutdown() override;
        void onUpdate( float32 deltaTime ) override;
        /** @brief 상태 스냅샷 직전 — 농장 무대는 절차 생성물이라 걷는다(밭 상태는 이 인스턴스에 남는다). */
        void onBeforeStateSerialize() override;
        void onAfterStateDeserialize() override;

    private:
        CropCatalog _cropCatalog;
        FarmWorld   _farmWorld;
    };
} // namespace sw
