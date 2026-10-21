/**
 * @file FrontierTownGame.h
 * @brief 키트 조립의 둘째 테스트 게임 — 도시(GF_CityBuilder)와 RTS(GF_RealTimeStrategy)를 한 씬 · 한 공유 상태(땅 · 지갑 · 시계)에 섞습니다.
 * @details 실행: `App.exe -game=FrontierTown`(`Ninja-Debug` 빌드). 씬(`game/frontiertown/maps/frontier.scene.xml`)의 `TownState` 오브젝트에
 *          `SessionStateComponent` → `FrontierCityDirectorComponent` → `FrontierSkirmishDirectorComponent` 가 그 순서로 붙어 시계 → 도시 → RTS 순서로 돈다.
 *          이 클래스는 카탈로그 둘을 읽어 게임 범위에 걸고(디렉터가 찾는다), 공유 상태와 두 디렉터를 상태 스냅숏에 올린다.
 */
#pragma once
#include "GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"

namespace sw
{
    /** @brief 둘째 조립 테스트 게임 인스턴스입니다. */
    class FrontierTownGame : public GameInstanceBase
    {
    public:
        FrontierTownGame();
        ~FrontierTownGame() override;

    protected:
        bool onInitialize() override;

    private:
        CityCatalog _cityCatalog;
        RTSCatalog  _unitCatalog;
    };
} // namespace sw
