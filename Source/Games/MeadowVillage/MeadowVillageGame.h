/**
 * @file MeadowVillageGame.h
 * @brief 키트 조립 시험 게임 — 농장(GF_Farming)과 생물 마을(GF_CreatureLife)을 한 씬 · 한 공유 상태에 섞습니다.
 * @details 빌드: `cmake --preset Ninja-Debug-MeadowVillage`. 씬(`game/meadowvillage/maps/meadow.scene.xml`)의 `VillageState` 오브젝트에
 *          `GameStateComponent` → `MeadowFarmDirectorComponent` → `MeadowTownDirectorComponent` 가 그 순서로 붙어 시계 → 밭 → 마을 순서로 돈다.
 *          이 클래스는 카탈로그 넷을 읽어 게임 로컬 서비스로 걸고(디렉터가 찾는다), 공유 상태와 두 디렉터를 상태 스냅숏에 올린다.
 */
#pragma once
#include "GameFramework/Base/Foundation/Framework/GameInstanceBase.h"
#include "GameFramework/Base/Gameplay/Progression/Reputation.h"
#include "GameFramework/Base/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Kits/Simulation/CreatureLife/CreatureLifeCatalog.h"
#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"

namespace sw
{
    /** @brief 조립 시험 게임 인스턴스입니다. */
    class MeadowVillageGame : public GameInstanceBase
    {
    public:
        MeadowVillageGame();
        ~MeadowVillageGame() override;

    protected:
        bool onInitialize() override;
        void onShutdown() override;

    private:
        CropCatalog         _cropCatalog;
        CreatureLifeCatalog _creatureCatalog;
        ReputationCatalog   _friendshipCatalog;
        QuestCatalog        _questCatalog;
        uint8               _bServicesBound; ///< 카탈로그를 로컬 서비스로 걸었다(내릴 때 푼다)
    };
} // namespace sw
