/**
 * @file VoxelCraftWorld.h
 * @brief VoxelCraft 의 게임 규칙 — 지형 꾸미기(광석 · 눈), 청크 메시를 엔진 메시로 옮기기, 1인칭 몸 · 시점, 부수기 · 놓기, 핫바입니다.
 *
 * @details 블록 · 월드 · 지형 · 광선 · 메싱 · 몸 충돌 · 핫바의 규칙은 키트(`GF_Voxel`)가, 시점은 기반의 `FirstPersonLook` 이 맡습니다.
 *          청크마다 불투명 · 반투명(물) 메시 오브젝트가 하나씩이고, 블록이 바뀐 청크만 한 프레임에 몇 개씩 다시 짓습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/FirstPersonLook.h"
#include "GameFramework/Base/PrimitiveStage.h"
#include "GameFramework/Kits/Voxel/VoxelBody.h"
#include "GameFramework/Kits/Voxel/VoxelHotbar.h"
#include "GameFramework/Kits/Voxel/VoxelMesher.h"
#include "GameFramework/Kits/Voxel/VoxelRaycast.h"
#include "GameFramework/Kits/Voxel/VoxelWorld.h"

namespace sw
{
    class InputManager;
    class Mesh;

    /**
     * @class VoxelCraftWorld
     * @brief 월드 한 판입니다. 블록 상태는 무대를 걷어도 남고(핫 리로드 · 씬 바뀜), 무대는 다음 갱신에서 모든 청크를 다시 지어 섭니다.
     */
    class VoxelCraftWorld
    {
    public:
        static constexpr int32   kChunkCountX         = 8;
        static constexpr int32   kChunkCountZ         = 8;
        static constexpr float32 kReachDistance       = 6.0f;
        static constexpr int32   kChunkBuildsPerFrame = 4;

        VoxelCraftWorld();
        ~VoxelCraftWorld();

        VoxelCraftWorld( const VoxelCraftWorld& )            = delete;
        VoxelCraftWorld& operator=( const VoxelCraftWorld& ) = delete;

        /** @brief 카탈로그를 빌리고 지형을 짓습니다(씨앗 고정). */
        void               initialize( const VoxelBlockCatalog* pCatalog );
        [[nodiscard]] bool spawn();
        void               despawn();
        void               update( float32 deltaTime );

    private:
        /** @brief 청크 하나의 모습입니다. */
        struct ChunkView
        {
            GameObjectHandle _opaque{};
            GameObjectHandle _translucent{};
        };

        void   decorateTerrain();
        float3 findSpawnPosition() const;
        void   rebuildDirtyChunks( int32 maxCount );
        void   rebuildChunk( int32 chunkX, int32 chunkZ );
        /** @brief 메시 오브젝트 하나에 정점을 옮겨 담습니다(비면 숨긴다). */
        void applyChunkMesh( GameObjectHandle& inoutHandle, const vector<VoxelMeshVertex>& listVertex, bool bTranslucent, const float3& origin );

        void updateInput( float32 deltaTime, const InputManager* pInput );
        void updateAutoPlayer( float32 deltaTime, float3& outWish, bool& outJump, bool& outBreak, bool& outPlace );
        void updateTarget();
        void updateBreaking( float32 deltaTime, bool bBreakHeld );
        void placeBlock();
        void updateCameraAndHighlight();
        void logStatus( float32 deltaTime );

        PrimitiveStage           _stage;
        VoxelWorld               _world;
        VoxelBody                _body;
        VoxelHotbar              _hotbar;
        FirstPersonLook          _look;
        VoxelChunkMesh           _scratchMesh;
        vector<ChunkView>        _listChunkView;
        const VoxelBlockCatalog* _pCatalog;
        VoxelRayHit              _target;
        GameObjectHandle         _highlight;
        float32                  _breakProgress;
        float32                  _placeCooldown;
        float32                  _autoTimer;
        float32                  _statusTimer;
        int32                    _highlightLevel;
        uint32                   _brokenCount;
        uint32                   _placedCount;
        uint8                    _bHasTarget;
        uint8                    _bMouseLocked;
        uint8                    _bSpawned;
    };
} // namespace sw
