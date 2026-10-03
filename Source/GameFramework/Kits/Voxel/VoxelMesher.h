/**
 * @file VoxelMesher.h
 * @brief 청크 하나를 삼각형 목록으로 — 드러난 면만, 면마다 아틀라스 UV · 면 밝기 · 꼭짓점 그늘(AO)을 넣습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Voxel/VoxelBlock.h"

namespace sw
{
    class VoxelWorld;

    /** @brief 메시 정점 하나입니다. 엔진 정점(`RHIVertex`)과 같은 네 속성이라 게임이 그대로 옮겨 담습니다. */
    struct VoxelMeshVertex
    {
        float3 _position{};
        float3 _normal{};
        float2 _uv{};
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    /** @brief 청크 메시입니다. 반투명 블록(색 알파 < 1 — 물 · 유리)은 따로 모아 투명 머티리얼로 그립니다. */
    struct VoxelChunkMesh
    {
        vector<VoxelMeshVertex> _listOpaqueVertex{};
        vector<VoxelMeshVertex> _listTranslucentVertex{};

        void clear()
        {
            _listOpaqueVertex.clear();
            _listTranslucentVertex.clear();
        }
        uint32 getFaceCount() const { return static_cast<uint32>( ( _listOpaqueVertex.size() + _listTranslucentVertex.size() ) / 6 ); }
    };

    /**
     * @struct VoxelMesher
     * @brief 면이 드러나는 규칙: 블록이 공기가 아니고, 이웃이 불투명이 아니고, 이웃이 같은 반투명 블록(물 옆 물)이 아니다. 청크 경계에서는 이웃 청크의
     *        블록을 봅니다 — 그래서 경계 블록이 바뀌면 이웃 청크도 다시 짓습니다(`VoxelWorld::setBlock`).
     * @details 정점은 청크 원점(`chunkX × 16, 0, chunkZ × 16`) 기준이고, 삼각형은 바깥에서 볼 때 엔진의 앞면 감김입니다(`(b - a) × (c - a)` 가 노멀 쪽).
     *          꼭짓점 그늘은 면 앞 평면의 이웃 셋(옆 둘 · 모서리)으로 0..3 단계를 매기고, 그늘이 한쪽으로 쏠린 사각형은 대각선을 바꿔 줄무늬를 없앱니다.
     */
    struct SW_GF_API VoxelMesher
    {
        static void buildChunkMesh( const VoxelWorld& world, int32 chunkX, int32 chunkZ, VoxelChunkMesh& outMesh );
        /** @brief 면 하나의 밝기입니다(위 1 · 옆 0.85 / 0.75 · 아래 0.6). 해 없이도 블록 모서리가 읽히게 합니다. */
        static float32 getFaceShade( VoxelFace face );
        /** @brief 그늘 단계(0 = 가장 어둡다 … 3 = 트였다)의 밝기입니다. */
        static float32 getOcclusionShade( int32 occlusionLevel );
    };
} // namespace sw
