/**
 * @file WildWestGame.h
 * @brief 서부 평원 승마 테스트 게임 — 레드 데드 리뎀션 2 · 젤다 BotW 식으로 걷다가 말에 올라타고, 걸음새와 스태미나로 몰고, 내립니다.
 * @details 실행: `App.exe -game=WildWest`(`Ninja-Debug` 빌드). 씬(`game/wildwest/maps/prairie.scene.xml`)의 `Ranch` 오브젝트에 `WildWestDirectorComponent` 가 있고,
 *          말 오브젝트마다 `WildWestHorseComponent` 가 말 상태를 가진다. 이 클래스는 디렉터와 말 컴포넌트를 상태 스냅숏에 올리고 첫 씬을 청한다.
 *          탑승 단계 `Mount` · `Dismount` 와 탐침 `Pawn.Seat` 을 실제 게임에서 돌리는 자리이다(`automation/mount.scenario.xml`).
 */
#pragma once
#include "GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.h"

namespace sw
{
    /** @brief 서부 평원 승마 게임 인스턴스입니다. */
    class WildWestGame : public GameInstanceBase
    {
    public:
        WildWestGame();
        ~WildWestGame() override;

    protected:
        bool onInitialize() override;
    };
} // namespace sw
