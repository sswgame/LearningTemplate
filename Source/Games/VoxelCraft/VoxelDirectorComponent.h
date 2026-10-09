/**
 * @file VoxelDirectorComponent.h
 * @brief VoxelCraft 의 월드를 드는 컴포넌트 — 지형 짓기 · 꾸미기(광석 · 눈), 블록 바꾸기, 청크 컴포넌트 스폰 · 다시 짓기 지시, 로그입니다.
 *
 * @details 언리얼 GameMode/GameState 의 자리입니다. 씬에 하나 둡니다. 블록 · 월드 · 지형 · 광선 · 메싱의 규칙은 키트(`GF_Voxel`)가 맡고, 몸 · 손 · 핫바는
 *          플레이어 컴포넌트(`VoxelPlayerComponent`), 청크 하나의 메시는 `VoxelChunkComponent` 가 맡습니다.
 *
 *          틱 규칙: 디렉터는 `TickGroup::PrePhysics` 에서 블록이 바뀐 청크 중 몸에서 가까운 것 몇 개에 다시 짓기를 맡깁니다. 플레이어 · 청크(`DuringPhysics`)는
 *          월드를 **읽기만** 합니다 — 청크는 같은 그룹에서 나란히 메싱한다. 블록 바꾸기는 틱 뒤(게임 스레드)에만 합니다(`applyBlockEdit`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class VoxelDirectorComponent
     * @brief 월드 한 판입니다. 플레이가 시작되면 씨앗으로 지형을 짓고 청크 오브젝트를 세웁니다.
     * @details 블록(부수고 놓은 것 포함)은 PROPERTY 가 아니라 `writeState` 로 게임 상태 스냅샷의 컴포넌트 섹션에 실려 핫 리로드 · 세이브를 넘깁니다
     *          (`VoxelCraftGame`). 세운 청크는 핸들로 들고 상태 저장 전에 걷습니다(`despawnViews`) — 남은 디렉터는 다음 틱에 청크를 다시 세우고
     *          모두 다시 짓는다(블록은 그대로).
     *
     *          **자동 플레이 = AI 조종자의 빙의**: 자동 플레이 스위치(`gv_voxelAutoPlay` · 씬의 `_bAutoPlay`)가 바뀌면 틱 뒤 플러시에서 플레이어 폰을
     *          `VoxelAutoPlayControllerComponent`(세운 오브젝트) 또는 플레이어 0 의 조종자(`ControlSystem::findOrCreatePlayerController`)에게 쥐어 준다.
     */
    REFLECT( Category = "VoxelCraft", DisplayName = "Voxel Director", Tooltip = "Owns the voxel world: terrain, block edits, chunk spawns and rebuild scheduling" )
    class VoxelDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kChunkCountX = 8;
        static constexpr int32 kChunkCountZ = 8;

        VoxelDirectorComponent();
        virtual ~VoxelDirectorComponent() override;

        /** @brief 블록(부수고 놓은 것 포함)을 씁니다 — `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const override;
        /** @brief 블록 하나를 바꿉니다 — 틱 뒤 게임 스레드에서만(플레이어 · 청크가 월드를 읽는 동안 쓰지 않는다). 월드 밖이면 false 입니다. */
        [[nodiscard]] bool applyBlockEdit( const VoxelCoord& coord, VoxelBlockIndex block );

        // ---- 다른 컴포넌트가 읽는 것(PrePhysics 뒤의 그룹) ----
        const VoxelWorld& getWorld() const { return _world; }
        /** @brief 가운데부터 나선으로 찾은 첫 땅(물 위가 아닌) 위의 자리입니다. */
        float3 findSpawnPosition() const;

    protected:
        /** @brief 씨앗으로 지형을 짓습니다. 블록 카탈로그가 없으면 false 입니다. */
        [[nodiscard]] bool startGame() override;
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               tickGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        /** @brief 자동 플레이 스위치와 플레이어 폰의 조종자가 어긋났으면 true 입니다 — 베이스가 틱 끝에 플러시를 잡는다. */
        bool hasPendingSpawn() const override;

    private:
        void decorateTerrain();
        void spawnChunks( GameObjectManager& manager );
        void scheduleRebuilds();
        void logStatus( float32 deltaTime );
        /** @brief 플레이어 폰을 자동 플레이 스위치에 맞는 조종자에게 쥐어 줍니다(게임 스레드, 틱 밖). */
        void syncAutoPlayPossession( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _chunkPrefab;
        PROPERTY( Category = "Scene", DisplayName = "Player", Tooltip = "Object with the VoxelPlayerComponent" )
        GameObjectHandle _player;
        PROPERTY( Category = "World", DisplayName = "Terrain Seed", Tooltip = "Same seed, same island" )
        int32 _terrainSeed;
        PROPERTY( Category = "World", DisplayName = "Chunk Builds Per Frame", Tooltip = "Changed chunks handed out for meshing each frame, nearest to the body first", Min = 1 )
        int32 _chunkBuildsPerFrame;
        PROPERTY( Category = "Debug", DisplayName = "Status Log Interval", Tooltip = "Seconds between status log lines", Min = 0.1, Units = s )
        float32 _statusLogInterval;

        VoxelWorld               _world;
        vector<GameObjectHandle> _listChunk;          ///< 청크 번호(z × 청크 수 X + x) 순
        GameObjectHandle         _autoPlayController; ///< 자동 플레이 AI 조종자 오브젝트(세운 것 — 걷을 목록에 든다)
        float32                  _statusTimer;
        int8                     _appliedAutoPlay; ///< 플레이어 폰에 맞춰 둔 자동 플레이(−1 아직, 0 끔, 1 켬)
    };
} // namespace sw
