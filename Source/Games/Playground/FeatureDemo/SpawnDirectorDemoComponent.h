/**
 * @file SpawnDirectorDemoComponent.h
 * @brief 스폰 감독(`SpawnDirector`)의 시연입니다 — 예산 표(`*.spawns.xml`)가 낸 항목마다 프리팹을 하나 세우고, 그것이 사라지면 감독에 알립니다.
 * @details 세운 오브젝트의 이름은 `Spawn.<스폰 번호>` 다. 감독의 상태(예산 · 시계 · 살아 있는 개체 · 난수)는 세이브가 싣고, 오브젝트는 그 상태에서 다시 세운다.
 *          시나리오: `game/playground/automation/spawndirector.scenario.xml`(무대 maps/spawndirector).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/AI/SpawnDirector.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    /**
     * @class SpawnDirectorDemoComponent
     * @brief 감독을 프레임마다 돌려 낸 것을 세우고, 파괴된 것을 감독에 알려 동시 상한의 자리를 돌려줍니다.
     */
    REFLECT( Category = "FeatureDemo", DisplayName = "Spawn Director Demo", Tooltip = "Spawns a prefab for every entry the budgeted spawn director releases" )
    class SpawnDirectorDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        SpawnDirectorDemoComponent();
        virtual ~SpawnDirectorDemoComponent() override;

    protected:
        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool startGame() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override;

    private:
        /** @brief 세운 오브젝트 하나와 감독의 스폰 번호입니다. */
        struct SpawnedView
        {
            GameObjectHandle _object{};
            uint32           _spawnID{ 0 };
        };

        /** @brief 상태 칸(살아 있는 수 · 예산 · 다음 항목)을 감독에서 옮깁니다. */
        void refreshState();

    private:
        PROPERTY( Category = "Data", DisplayName = "Spawn Table", Tooltip = "Budget and entries of the spawn director" )
        AssetRef<SpawnTable> _spawnTable;
        PROPERTY( Category = "Data", DisplayName = "Prefab", Tooltip = "Prefab spawned for every entry" )
        AssetRef<PrefabAsset> _spawnPrefab;
        PROPERTY( Category = "Data", DisplayName = "Seed", Min = 1 )
        uint32 _seed;

        PROPERTY( RuntimeState, Min = 0 )
        int32 _spawnedTotal;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _despawnedTotal;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _aliveCount;
        PROPERTY( RuntimeState, Min = 0 )
        float32 _budget;

        SharedAssetRef<SpawnTable> _pTable; ///< 감독이 빌려 쓴다
        SpawnDirector              _director;
        vector<SpawnedView>        _listView;
    };
} // namespace sw
