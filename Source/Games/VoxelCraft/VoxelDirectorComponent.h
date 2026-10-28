/**
 * @file VoxelDirectorComponent.h
 * @brief VoxelCraft 의 월드를 담당하는 컴포넌트입니다. 지형 생성, 꾸미기(광석, 눈), 블록 바꾸기, 청크 컴포넌트 스폰과 다시 메싱 지시, 로그를 맡습니다.
 *
 * @details 언리얼의 GameMode와 GameState에 해당합니다. 씬에 하나만 둡니다.
 *          블록, 월드, 지형, 광선, 메싱의 규칙은 키트(`GF_Voxel`)가 맡습니다.
 *          몸, 손, 핫바는 플레이어 컴포넌트(`VoxelPlayerComponent`)가, 청크 하나의 메시는 `VoxelChunkComponent` 가 맡습니다.
 *
 *          틱 규칙은 다음과 같습니다. 디렉터는 `TickGroup::PrePhysics` 에서, 블록이 바뀐 청크 중 몸에서 가까운 몇 개에 다시 메싱을 요청합니다.
 *          플레이어와 청크(`DuringPhysics`)는 월드를 **읽기만** 하고, 청크는 같은 그룹에서 서로 나란히 메싱합니다.
 *          블록 바꾸기는 틱이 끝난 뒤 게임 스레드에서만 합니다(`applyBlockEdit`).
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class VoxelDirectorComponent
     * @brief 월드 한 판을 맡습니다. 플레이가 시작되면 시드로 지형을 생성하고 청크 오브젝트를 스폰합니다.
     * @details 블록(부수거나 놓은 것 포함)은 PROPERTY 가 아니라 `writeState` 로 저장합니다.
     *          게임 상태 스냅샷의 컴포넌트 섹션에 실려서 핫 리로드와 세이브를 거쳐도 유지됩니다(`VoxelCraftGame`).
     *          스폰한 청크는 핸들로 보관했다가 상태를 저장하기 전에 정리합니다(`despawnViews`).
     *          정리된 뒤 남은 디렉터는 다음 틱에 청크를 다시 스폰하고 전부 다시 메싱합니다. 블록은 그대로입니다.
     *
     *          **자동 플레이는 AI 조종자가 폰에 빙의하는 방식입니다.** 자동 플레이 스위치(`gv_voxelAutoPlay`, 씬의 `_bAutoPlay`)가 바뀌면
     *          틱이 끝난 뒤 플러시에서 플레이어 폰의 조종자를 바꿉니다.
     *          켜면 `VoxelAutoPlayControllerComponent`(스폰한 오브젝트)에게, 끄면 플레이어 0의 조종자(`ControlSystem::findOrCreatePlayerController`)에게 넘깁니다.
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

        /** @brief 블록(부수거나 놓은 것 포함)을 기록합니다. `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const override;
        /**
         * @brief 블록 하나를 바꿉니다. 플레이어와 청크가 월드를 읽는 동안에는 쓰면 안 되므로, 틱이 끝난 뒤 게임 스레드에서만 부릅니다.
         * @return 좌표가 월드 밖이면 false 입니다.
         */
        [[nodiscard]] bool applyBlockEdit( const VoxelCoord& coord, VoxelBlockIndex block );

        // ---- 다른 컴포넌트가 읽는 값입니다(PrePhysics 뒤의 그룹에서 읽습니다) ----
        const VoxelWorld& getWorld() const { return _world; }
        /** @brief 가운데에서 시작해 나선으로 찾은 첫 땅(물이 아닌 곳)의 위쪽 위치입니다. */
        float3 findSpawnPosition() const;

    protected:
        /** @brief 시드로 지형을 생성합니다. 블록 카탈로그가 없거나 블록 이름 PROPERTY 가 카탈로그에 없으면 오류를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool startGame() override;
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        /** @brief 자동 플레이 스위치와 플레이어 폰의 조종자가 맞지 않으면 true 입니다. 이 값을 보고 베이스가 틱 끝에 플러시를 예약합니다. */
        bool hasPendingSpawn() const override;

    private:
        void decorateTerrain();
        void spawnChunks( GameObjectManager& manager );
        void scheduleRebuilds();
        void logStatus( float32 deltaTime );
        /** @brief 플레이어 폰의 조종자를 자동 플레이 스위치에 맞게 바꿉니다. 틱 밖의 게임 스레드에서 부릅니다. */
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
        // ---- 블록 이름입니다. 블록 카탈로그(data/blocks.xml)의 id 이고, 게임을 시작할 때 모두 있는지 확인합니다 ----
        PROPERTY( Category = "Blocks", DisplayName = "Water", Tooltip = "Spawn spots on this block are skipped" )
        hashed_string _waterBlock;
        PROPERTY( Category = "Blocks", DisplayName = "Stone", Tooltip = "Ore replaces this block underground" )
        hashed_string _stoneBlock;
        PROPERTY( Category = "Blocks", DisplayName = "Grass", Tooltip = "High peaks of this block get a snow cap" )
        hashed_string _grassBlock;
        PROPERTY( Category = "Blocks", DisplayName = "Snow" )
        hashed_string _snowBlock;
        PROPERTY( Category = "Blocks", DisplayName = "Coal Ore" )
        hashed_string _coalOreBlock;
        PROPERTY( Category = "Blocks", DisplayName = "Iron Ore" )
        hashed_string _ironOreBlock;

        VoxelWorld               _world;
        vector<GameObjectHandle> _listChunk;          ///< 청크 번호(z × 청크 수 X + x) 순서입니다.
        GameObjectHandle         _autoPlayController; ///< 자동 플레이 AI 조종자 오브젝트입니다. 스폰한 오브젝트라 정리 목록에 들어갑니다.
        float32                  _statusTimer;
        int8                     _appliedAutoPlay; ///< 플레이어 폰에 적용한 자동 플레이 상태입니다(−1 아직 없음, 0 끔, 1 켬).
    };
} // namespace sw
