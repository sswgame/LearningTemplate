/**
 * @file MinigamePortalSpawnerComponent.h
 * @brief 팩의 미니게임 정의마다 입구 하나를 세우는 컴포넌트입니다 — 정의의 `portalSlot` 이 입구 자리다(허브 맵을 고치지 않고 입구를 더한다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/PrefabAssetKind.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class MinigameFlow;
    struct MinigameDefinition;

    /**
     * @class MinigamePortalSpawnerComponent
     * @brief 시작할 때 `portalSlot` 을 적은 미니게임 정의(`MinigameFlow::collectPortals`)마다 입구 프리팹(`MinigameTriggerComponent` 를 가진 것)을 자리 격자에 스폰하고 들어갈 미니게임을 넣습니다.
     * @details 언리얼 Lyra 허브의 `B_ExperienceList3D`(`ULyraUserFacingExperienceDefinition` 에셋마다 포털 하나)와 같은 자리다 — 미니게임을 더하는 일이 정의 파일 하나로 끝난다.
     *          자리는 이 컴포넌트의 월드 자리에서 X 로 `_columnCount` 칸, 넘치면 Z 로 한 줄씩이다. 자리 번호가 정의에 있어 미니게임을 더해도 있던 입구가 밀리지 않는다.
     *          세운 입구는 정의에서 다시 세우는 파생이라 스냅샷에 싣지 않는다(`GameObject::setTransient`) — 씬 복원이 이 컴포넌트를 다시 만들면 시작이 다시 세운다.
     *          오브젝트 이름은 `MinigamePortal_<미니게임>` 이다. 게임 인스턴스(흐름)가 없는 월드(에디터 편집 씬)에서는 아무것도 세우지 않는다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Minigame Portal Spawner", Tooltip = "Spawns one entrance per minigame definition that names a portal slot" )
    class SW_GF_API MinigamePortalSpawnerComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 세운 입구 오브젝트 이름의 앞부분입니다(`MinigamePortal_<미니게임>`). */
        static constexpr const utf8* kPortalNamePrefix = "MinigamePortal_";
        /** @brief 세운 미리 읽기 볼륨 오브젝트 이름의 앞부분입니다(`MinigamePreload_<미니게임>`). */
        static constexpr const utf8* kPreloadNamePrefix = "MinigamePreload_";

        MinigamePortalSpawnerComponent();
        ~MinigamePortalSpawnerComponent() override = default;

        /** @brief 입구를 세웁니다. 프리팹이 비었거나 스폰하지 못하면 오류를 남긴다. */
        void onBeginPlay() override;
        /** @brief 세운 입구를 지웁니다. */
        void onEndPlay() override;

        /** @brief 자리 @p slot 의 월드 위치입니다(0 부터). */
        float3 computeSlotPosition( int32 slot ) const;

    private:
        /** @brief 프리팹 @p prefab 을 이름 @p pName 으로 @p position 에 세우고 그 트리거 @p TTrigger 에 @p definition 의 이름을 넣고 그 트리거를 돌려줍니다. 지면 오류를 남기고 nullptr 입니다. */
        template <typename TTrigger>
        [[nodiscard]] TTrigger* spawnVolume( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const utf8* pName, const MinigameDefinition& definition );
        /** @brief 정의마다 입구(와 미리 읽기 볼륨)를 세웁니다. */
        void spawnPortals( GameObjectManager& manager, const MinigameFlow& flow );

    private:
        PROPERTY( Category = "Minigame", DisplayName = "Portal Prefab", Tooltip = "Entrance prefab with a MinigameTriggerComponent; its minigame name is filled per definition" )
        AssetRef<PrefabAsset> _portalPrefab;
        PROPERTY( Category = "Minigame", DisplayName = "Columns", Min = 1, Tooltip = "Portal slots per row along X; the next row goes along Z" )
        int32 _columnCount;
        PROPERTY( Category = "Minigame", DisplayName = "Preload Prefab", Tooltip = "Volume with a MinigamePreloadTriggerComponent spawned around each entrance (empty: map files are read on entry)" )
        AssetRef<PrefabAsset> _preloadPrefab;
        PROPERTY( Category = "Minigame", DisplayName = "Slot Spacing", Tooltip = "Distance between slots along X and Z", Units = m )
        float2 _slotSpacing;

        vector<GameObjectHandle> _listPortal; ///< 세운 입구와 미리 읽기 볼륨
    };
} // namespace sw
