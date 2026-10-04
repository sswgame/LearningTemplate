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

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Simulation/Voxel/VoxelWorld.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class VoxelDirectorComponent
     * @brief 월드 한 판입니다. 플레이가 시작되면 씨앗으로 지형을 짓고 청크 오브젝트를 세웁니다.
     * @details 블록 상태는 런타임 상태라 핫 리로드에서 씨앗대로 다시 섭니다(PROPERTY 만 남는다). 세운 청크는 핸들로 들고 상태 저장 전에 걷습니다
     *          (`despawnRuntime`) — 남은 디렉터는 다음 틱에 청크를 다시 세우고 모두 다시 짓는다(블록은 그대로).
     */
    REFLECT( Category = "VoxelCraft", DisplayName = "Voxel Director", Tooltip = "Owns the voxel world: terrain, block edits, chunk spawns and rebuild scheduling" )
    class VoxelDirectorComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kChunkCountX = 8;
        static constexpr int32 kChunkCountZ = 8;

        VoxelDirectorComponent();
        virtual ~VoxelDirectorComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 세운 청크를 모두 지웁니다(상태 저장 전). */
        void despawnRuntime();
        /** @brief 블록 하나를 바꿉니다 — 틱 뒤 게임 스레드에서만(플레이어 · 청크가 월드를 읽는 동안 쓰지 않는다). 월드 밖이면 false 입니다. */
        [[nodiscard]] bool applyBlockEdit( const VoxelCoord& coord, VoxelBlockIndex block );

        // ---- 다른 컴포넌트가 읽는 것(PrePhysics 뒤의 그룹) ----
        const VoxelWorld& getWorld() const { return _world; }
        /** @brief 가운데부터 나선으로 찾은 첫 땅(물 위가 아닌) 위의 자리입니다. */
        float3 findSpawnPosition() const;
        bool   isWorldReady() const { return _bWorldReady == SW_TRUE; }
        /** @brief 걷기 · 부수기 · 놓기도 AI 가 하면 true 입니다(`_bAutoPlay` 또는 `-gv_voxelAutoPlay=1`). */
        bool isAutoPlayOn() const;

        /** @brief 핸들의 오브젝트에 붙은 디렉터입니다. 없으면 nullptr 입니다. 매니저 조회는 잠그지 않습니다. */
        static const VoxelDirectorComponent* resolveDirector( const GameObjectManager& manager, GameObjectHandle director );

    private:
        void               initializeWorld();
        void               decorateTerrain();
        void               scheduleFlush();
        void               flushPending();
        void               spawnChunks( GameObjectManager& manager );
        void               scheduleRebuilds();
        void               logStatus( float32 deltaTime );
        GameObjectManager* getObjectManager() const;

    private:
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _chunkPrefab;
        PROPERTY( Category = "Scene", DisplayName = "Player", Tooltip = "Object with the VoxelPlayerComponent" )
        GameObjectHandle _player;
        PROPERTY( Category = "World", DisplayName = "Terrain Seed", Tooltip = "Same seed, same island" )
        int32 _terrainSeed;
        PROPERTY( Category = "World", DisplayName = "Chunk Builds Per Frame", Tooltip = "Changed chunks handed out for meshing each frame, nearest to the body first", Min = 1 )
        int32 _chunkBuildsPerFrame;
        PROPERTY( Category = "World", DisplayName = "Auto Play", Tooltip = "Walk, break and place by AI (-gv_voxelAutoPlay=1 also turns it on)" )
        bool _bAutoPlay;

        VoxelWorld               _world;
        vector<GameObjectHandle> _listChunk; ///< 청크 번호(z × 청크 수 X + x) 순
        float32                  _statusTimer;
        uint8                    _bWorldReady     : 1;
        uint8                    _bChunksSpawned  : 1; ///< 청크 오브젝트가 서 있다(걷으면 다음 틱이 다시 세운다)
        uint8                    _bFlushScheduled : 1;
        uint8                    _reserved        : 5;
    };
} // namespace sw
