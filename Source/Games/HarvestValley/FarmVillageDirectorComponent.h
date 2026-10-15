/**
 * @file FarmVillageDirectorComponent.h
 * @brief HarvestValley 마을 사람의 하루 일정을 돌리는 디렉터입니다. 일정 데이터(`data/villagers.schedules.xml`)를 GameFramework `ScheduleSystem` 으로 돌리고 마을 사람마다 몸(프리팹)을 세웁니다.
 * @details 농장 디렉터와 같은 오브젝트의 **뒤**에 붙입니다. 판 상태(`SessionStateComponent`)의 시계를 잠금 없이 읽으려면 같은 오브젝트여야 하고, 농장 디렉터가 판 상태를 연 뒤에 시작해야 한다.
 *          일정은 시계의 함수라 상태 바이트에는 머리만 싣는다 — 복원하면 그 시각의 계획을 다시 세워 같은 자리에 선다(끼어들기 · 호감도를 쓰게 되면 `ScheduleSaveState` 를 싣는다).
 *          몸은 `FarmVillagerComponent` 가 `PostUpdate` 에서 이 디렉터의 자리를 읽어 옮긴다. 자동화는 `Schedule.*` 탐침(NPC id)과 `Object.*` 탐침(`Villager.<id>`)으로 본다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"

namespace sw
{
    class Archive;
    class GameObjectManager;

    /**
     * @class FarmVillageDirectorComponent
     * @brief 마을 사람 일정의 디렉터입니다. 틱(PrePhysics)에서 일정을 공유 시계까지 흘리고 마을 사람마다 일정상 자리를 적어 둡니다. 뷰는 그것만 읽습니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Village Director", Tooltip = "Runs the villagers' daily schedules on the shared game clock and spawns their bodies" )
    class FarmVillageDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        FarmVillageDirectorComponent();
        virtual ~FarmVillageDirectorComponent() override;

        void writeState( Archive& outArchive ) const override;

        /** @brief 마을 사람 @p npcIndex 의 일정상 자리(지면)입니다. 뷰가 PostUpdate 에서 읽습니다. 범위 밖이면 원점입니다. */
        float3 getVillagerPosition( int32 npcIndex ) const;
        int32  getVillagerCount() const { return static_cast<int32>( _listPosition.size() ); }

    protected:
        [[nodiscard]] bool startGame() override;
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        PROPERTY( Category = "Data", DisplayName = "Schedule Data", AssetPath, Tooltip = "Schedule catalog XML (places, routines, NPCs)" )
        string _scheduleDataPath;
        PROPERTY( Category = "Prefabs", DisplayName = "Villager Prefab", AssetPath, AssetType = "Prefab", Tooltip = "Body spawned for each schedule NPC (needs a FarmVillagerComponent)" )
        string _villagerPrefab;

        ScheduleCatalog _catalog;
        ScheduleSystem  _schedule;
        vector<float3>  _listPosition; ///< 마을 사람마다 이번 틱의 일정상 자리(뷰가 읽는다)
    };
} // namespace sw
