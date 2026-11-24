/**
 * @file VoxelGameModeComponent.h
 * @brief 복셀 키트(`GF_Voxel`)의 디렉터입니다. 지형 생성, 꾸미기(광석, 눈), 블록 바꾸기, 청크 상주, 자동 플레이 빙의, 로그를 맡습니다(미니게임 `voxelcraft`).
 *
 * @details 언리얼의 GameMode와 GameState에 해당합니다. 씬에 하나만 둡니다. 키트 세 갈래의 가운데다: 규칙(블록 · 월드 · 지형 · 광선 — 키트 루트)을 들고 씬과 잇는다.
 *          몸, 손, 핫바는 플레이어 컴포넌트(`VoxelPlayerComponent`)가 맡고, 청크를 그리는 일은 클라이언트 모듈(`GF_Client_Voxel` 의 `VoxelWorldViewComponent` · `VoxelChunkComponent`)이
 *          이 디렉터의 읽기 API 로만 한다 — 전용 서버는 그 모듈을 짓지 않는다.
 *
 *          틱 규칙은 다음과 같습니다. 디렉터의 걸음은 `TickGroup::PrePhysics` 뒤의 틱 뒤 작업에서 몸 둘레의 청크만 월드에 올립니다(멀어진 청크는 내린다 — 바뀐 청크만 저장본에 남는다).
 *          플레이어와 청크(`DuringPhysics`)는 월드를 **읽기만** 하고, 블록 바꾸기는 틱이 끝난 뒤 게임 스레드에서만 합니다(`applyBlockEdit`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    struct VoxelBlockDef;
    class GameObjectManager;

    /**
     * @class VoxelGameModeComponent
     * @brief 월드 한 판을 맡습니다. 플레이가 시작되면 시드로 지형을 생성합니다.
     * @details 블록(부수거나 놓은 것 포함)은 PROPERTY 가 아니라 `writeState` 로 저장합니다.
     *          게임 상태 스냅샷의 컴포넌트 섹션에 실려서 핫 리로드와 세이브를 거쳐도 유지됩니다(게임 인스턴스의 `registerGameMode`).
     *
     *          **자동 플레이는 AI 조종자가 폰에 빙의하는 방식입니다.** 자동 플레이 스위치(게임의 `SW_GAME_AUTOPLAY` 변수, 씬의 `_bAutoPlay`)가 바뀌면
     *          틱이 끝난 뒤 플러시에서 플레이어 폰의 조종자를 바꿉니다.
     *          켜면 `VoxelAutoPlayControllerComponent`(스폰한 오브젝트)에게, 끄면 플레이어 0의 조종자(`ControlSystem::getOrCreatePlayerController`)에게 넘깁니다.
     */
    REFLECT( Category = "Voxel", DisplayName = "Voxel Game Mode", Tooltip = "Owns the voxel world: terrain, block edits and chunk residency" )
    class SW_KIT_API VoxelGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        VoxelGameModeComponent();
        virtual ~VoxelGameModeComponent() override;

        /** @brief 블록(부수거나 놓은 것 포함)을 기록합니다. `ComponentStateCodec::captureObject` 가 부릅니다. */
        void serializeGameState( StateIO& io ) override;
        /**
         * @brief 블록 하나를 바꿉니다. 플레이어와 청크가 월드를 읽는 동안에는 쓰면 안 되므로, 틱이 끝난 뒤 게임 스레드에서만 부릅니다.
         * @return 좌표가 월드 밖이면 false 입니다.
         */
        [[nodiscard]] bool applyBlockEdit( const VoxelCoord& coord, VoxelBlockIndex block );

        // ---- 다른 컴포넌트가 읽는 값입니다(PrePhysics 뒤의 그룹에서 읽습니다) ----
        const VoxelWorld& getWorld() const { return _world; }
        /** @brief 상주 범위의 가운데 청크(몸이 든 청크)입니다. */
        int32 getCenterChunkX() const { return _centerChunkX; }
        int32 getCenterChunkZ() const { return _centerChunkZ; }
        /** @brief 뷰가 그릴 반경(청크)입니다 — 상주 반경보다 한 칸 안쪽이라 그리는 청크의 이웃은 늘 올라와 있다. */
        int32 getViewChunkRadius() const { return _residentChunkRadius - 1; }
        /** @brief 가운데에서 시작해 나선으로 찾은 첫 땅(물이 아닌 곳)의 위쪽 위치입니다. */
        float3 findSpawnPosition() const;

    protected:
        /** @brief 시드로 지형을 생성합니다. 블록 카탈로그가 없거나 블록 이름 PROPERTY 가 카탈로그에 없으면 오류를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool startGame() override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        /** @brief 자동 플레이 스위치와 플레이어 폰의 조종자가 맞지 않으면 true 입니다. 이 값을 보고 베이스가 틱 끝에 플러시를 예약합니다. */
        bool hasPendingSpawn() const override;

    private:
        /** @brief 몸(놓기 전이면 월드 가운데) 둘레의 청크만 월드에 올립니다. */
        void updateResidency();
        void logStatus() const override;
        /** @brief 플레이어 폰의 조종자를 자동 플레이 스위치에 맞게 바꿉니다. 틱 밖의 게임 스레드에서 부릅니다. */
        void syncAutoPlayPossession( GameObjectManager& manager );

    private:
        PROPERTY( Required, Category = "Prefabs", DisplayName = "Auto Play Controller", Tooltip = "AI controller (with its behavior tree) that takes the player pawn while auto play is on" )
        AssetRef<PrefabAsset> _autoPlayPrefab;
        PROPERTY( Category = "Scene", DisplayName = "Player", Tooltip = "Object with the VoxelPlayerComponent" )
        GameObjectHandle _player;
        PROPERTY( Category = "World", DisplayName = "Terrain Seed", Tooltip = "Same seed, same island" )
        int32 _terrainSeed;
        PROPERTY( Required, Category = "World", DisplayName = "Chunk Count X", Tooltip = "World width in chunks (each chunk is 16 blocks wide)", Min = 1 )
        int32 _chunkCountX;
        PROPERTY( Required, Category = "World", DisplayName = "Chunk Count Z", Tooltip = "World depth in chunks (each chunk is 16 blocks wide)", Min = 1 )
        int32 _chunkCountZ;
        PROPERTY( Required, Category = "World", DisplayName = "Resident Chunk Radius", Tooltip = "Chunks within this many chunks of the body stay loaded; chunks one closer are drawn", Min = 2 )
        int32 _residentChunkRadius;
        PROPERTY( Required, Category = "World", DisplayName = "Chunk Loads Per Frame", Tooltip = "Missing chunks loaded (generated or read back) each frame, nearest to the body first", Min = 1 )
        int32 _chunkLoadsPerFrame;
        PROPERTY( Category = "Debug", DisplayName = "Status Log Interval", Tooltip = "Seconds between status log lines", Min = 0.1, Units = s )
        float32 _statusLogInterval;
        // ---- 블록 이름 · 지형 규칙(광석 확률 · 높이)입니다. 씬 데이터가 정본이고 C++ 기본값이 없다(필수 칸 — 빠지면 씬 로드 실패, 감사 K-4). 블록 id 는 게임을 시작할 때 모두 있는지 확인합니다 ----
        PROPERTY( Required, Category = "Blocks", DisplayName = "Water", Tooltip = "Spawn spots on this block are skipped" )
        CatalogKey<VoxelBlockDef> _waterBlock;
        PROPERTY( Required, Category = "Blocks", DisplayName = "Stone", Tooltip = "Ore replaces this block underground" )
        CatalogKey<VoxelBlockDef> _stoneBlock;
        PROPERTY( Required, Category = "Blocks", DisplayName = "Grass", Tooltip = "High peaks of this block get a snow cap" )
        CatalogKey<VoxelBlockDef> _grassBlock;
        PROPERTY( Required, Category = "Blocks", DisplayName = "Snow" )
        CatalogKey<VoxelBlockDef> _snowBlock;
        PROPERTY( Required, Category = "Blocks", DisplayName = "Coal Ore" )
        CatalogKey<VoxelBlockDef> _coalOreBlock;
        PROPERTY( Required, Category = "Blocks", DisplayName = "Iron Ore" )
        CatalogKey<VoxelBlockDef> _ironOreBlock;
        PROPERTY( Required, Category = "World", DisplayName = "Snow Height", Tooltip = "Grass tops at or above this height get a snow cap", Min = 0 )
        int32 _snowHeight;
        PROPERTY( Required, Category = "World", DisplayName = "Coal Ore Chance", Tooltip = "Chance that an underground stone block becomes coal", Min = 0.0, Max = 1.0 )
        float32 _coalOreChance;
        PROPERTY( Required, Category = "World", DisplayName = "Iron Ore Chance", Tooltip = "Chance that an underground stone block below the iron height becomes iron", Min = 0.0, Max = 1.0 )
        float32 _ironOreChance;
        PROPERTY( Required, Category = "World", DisplayName = "Iron Ore Max Height", Tooltip = "Iron is placed only below this height", Min = 0 )
        int32 _ironOreMaxHeight;

        SharedAssetRef<VoxelBlockCatalog> _pCatalog; ///< 게임 기능 Voxel 이 건 블록 카탈로그 — 월드가 빌린다
        VoxelWorld                        _world;
        GameObjectHandle                  _autoPlayController; ///< 자동 플레이 AI 조종자 오브젝트입니다. 스폰한 오브젝트라 정리 목록에 들어갑니다.
        int32                             _centerChunkX;       ///< 상주 범위의 가운데 청크입니다(몸이 든 청크).
        int32                             _centerChunkZ;
        int8                              _appliedAutoPlay; ///< 플레이어 폰에 적용한 자동 플레이 상태입니다(−1 아직 없음, 0 끔, 1 켬).
    };
} // namespace sw
