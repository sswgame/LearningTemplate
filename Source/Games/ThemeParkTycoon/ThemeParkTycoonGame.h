/**
 * @file ThemeParkTycoonGame.h
 * @brief 테마파크 키트(GF_ThemePark)를 실제로 쓰는 시험 게임 — 코스터를 짓고(시험 운행이 평가를 매긴다) 손님을 받아 돈 · 평점을 키우는 경영 게임입니다.
 *
 * @details 빌드: `cmake --preset <preset> -DSW_ACTIVE_GAME=ThemeParkTycoon`. 조작은 `Source/Games/ThemeParkTycoon/README.md`.
 *          `-gv_parkAutoBuild=1` 이면 돈이 모이는 대로 다음 놀이기구를 짓는다(입력 없이 공원이 크는 확인).
 */
#pragma once
#include "GameFramework/Framework/GameInstanceBase.h"

#include "Games/ThemeParkTycoon/ParkWorld.h"

namespace sw
{
    /** @brief 공원 한 판을 듭니다. */
    class ThemeParkTycoonGame : public GameInstanceBase
    {
    public:
        ThemeParkTycoonGame();
        ~ThemeParkTycoonGame() override;

    protected:
        bool onInitialize() override;
        void onShutdown() override;
        void onUpdate( float32 deltaTime ) override;
        void onBeforeStateSerialize() override;
        void onAfterStateDeserialize() override;

    private:
        ParkWorld _parkWorld;
    };
} // namespace sw
