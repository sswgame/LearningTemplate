/**
 * @file MeadowVillageGame.cpp
 * @brief 키트 조립 테스트 게임 — 농장(GF_Farming)과 생물 마을(GF_CreatureLife)을 한 씬 · 한 공유 상태에 섞습니다.
 * @details 실행: `App.exe -game=MeadowVillage`(`Ninja-Debug` 빌드). 씬(`game/meadowvillage/maps/meadow.scene.xml`)의 `VillageState` 오브젝트에
 *          `SessionStateComponent` → `MeadowFarmGameModeComponent` → `MeadowTownGameModeComponent` 가 그 순서로 붙어 시계 → 밭 → 마을 순서로 돈다.
 *          이 클래스는 카탈로그 넷을 읽어 게임 범위에 걸고(디렉터가 찾는다), 공유 상태와 두 디렉터를 상태 스냅숏에 올린다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureLifeCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/MeadowVillage/MeadowFarmGameModeComponent.h"
#include "Games/MeadowVillage/MeadowTownGameModeComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 조립 테스트 게임 인스턴스입니다. */
    class MeadowVillageGame : public GameInstance
    {
    public:
        MeadowVillageGame();
        ~MeadowVillageGame() override;

    protected:
        bool onInitialize() override;

    private:
        shared_ptr<const CropCatalog>         _pCropCatalog;       ///< 공유 캐시의 한 벌(게임 범위 서비스)
        shared_ptr<const CreatureLifeCatalog> _pCreatureCatalog;   ///< 공유 캐시의 한 벌(게임 범위 서비스)
        shared_ptr<const ReputationCatalog>   _pFriendshipCatalog; ///< 공유 캐시의 한 벌(게임 범위 서비스)
        shared_ptr<const QuestCatalog>        _pQuestCatalog;      ///< 공유 캐시의 한 벌(게임 범위 서비스)
    };
} // namespace sw

namespace sw
{
    MeadowVillageGame::MeadowVillageGame()
        : _pCropCatalog{}
        , _pCreatureCatalog{}
        , _pFriendshipCatalog{}
        , _pQuestCatalog{}
    {
        // 공유 상태가 먼저 — 복원할 때 디렉터보다 먼저 바이트를 받아 두고, 첫 디렉터가 판을 열며 적용한다.
        getStateSnapshot().registerStatefulComponent<SessionStateComponent>();
        getStateSnapshot().registerGameMode<MeadowFarmGameModeComponent>();
        getStateSnapshot().registerGameMode<MeadowTownGameModeComponent>();
    }

    MeadowVillageGame::~MeadowVillageGame() = default;

    bool MeadowVillageGame::onInitialize()
    {
        // 마을 데이터 넷은 필수다 — 하나라도 못 읽으면 게임을 띄우지 않는다. 읽은 것은 디렉터가 찾는 게임 범위에 걸린다(작물이 적을 계절은 작물 파일의 루트가 정한다).
        const bool bLoaded = loadRequiredCatalog( _pCropCatalog ) && loadRequiredCatalog( _pCreatureCatalog ) && loadRequiredCatalog( _pFriendshipCatalog ) &&
                             loadRequiredCatalog( _pQuestCatalog );
        if ( bLoaded == false )
            return false;
        (void)getSubsystem<GameSceneFlow>().requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::MeadowVillageGame );
