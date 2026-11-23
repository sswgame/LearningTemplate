/**
 * @file VoxelMesher.h
 * @brief 청크 하나를 삼각형 목록으로 만듭니다. 드러난 면만 만들고, 같은 블록 면은 사각형 하나로 합치며(그리디 메싱), 타일 칸 UV, 면 밝기, 꼭짓점 그늘(AO)을 넣습니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"

namespace sw
{
    class VoxelWorld;

    /**
     * @brief 메시 정점 하나입니다. 엔진 정점(`RHIVertex`)과 속성 네 개가 같아서 게임이 그대로 옮겨 담을 수 있습니다.
     * @details `_uv` 는 아틀라스 좌표가 아니라 `타일 칸(열, 행) × VoxelMesher::kTileUVStride + 면 안 블록 좌표` 다. 셰이더(voxel_blocks.hlsl)가 풀어 타일을 블록마다 되풀이한다.
     */
    struct VoxelMeshVertex
    {
        float3 _position{};
        float3 _normal{};
        float2 _uv{};
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 청크 메시입니다. 반투명 블록(색 알파가 1보다 작은 물과 유리)은 따로 모아 투명 머티리얼로 그립니다. */
    struct VoxelChunkMesh
    {
        vector<VoxelMeshVertex> _listOpaqueVertex{};
        vector<VoxelMeshVertex> _listTranslucentVertex{};

        void clear()
        {
            _listOpaqueVertex.clear();
            _listTranslucentVertex.clear();
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VoxelMesher
     * @brief 청크 메시를 만듭니다.
     * @details 면이 드러나는 조건은 세 가지이다. 블록이 공기가 아니고, 이웃이 불투명이 아니며, 이웃이 같은 반투명 블록(물 옆의 물)이 아니어야 한다.
     *          청크 경계에서는 이웃 청크의 블록을 본다. 그래서 경계 블록이 바뀌면 이웃 청크도 다시 만든다(`VoxelWorld::setBlock`).
     *
     *          정점은 청크 원점(`chunkX × 16, 0, chunkZ × 16`) 기준이다. 삼각형은 바깥에서 볼 때 엔진의 앞면 감김 순서이며, `(b - a) × (c - a)` 가 노멀 쪽을 향한다.
     *          꼭짓점 그늘은 면 앞 평면의 이웃 셋(옆 둘과 모서리)으로 0~3단계를 매긴다. 그늘이 한쪽으로 쏠린 사각형은 대각선을 바꿔 줄무늬를 없앤다.
     *
     *          같은 평면의 이웃한 면이 블록이 같고 네 꼭짓점 그늘이 모두 같은 단계이면 사각형 하나로 합친다(0fps "Meshing in a Minecraft Game" 의 그리디 메싱).
     *          그늘이 고르지 않은 면은 합치면 보간이 블록 하나보다 길게 늘어나 모양이 바뀌므로 홀로 둔다. 합친 면은 타일을 늘이지 않고 블록마다 되풀이한다(UV 는 `VoxelMeshVertex`).
     *          메싱은 청크와 이웃 한 칸 테두리를 지역 배열에 한 번 옮겨 읽는다 — 블록마다 월드 사전을 찾지 않는다.
     */
    struct SW_KIT_CLIENT_API VoxelMesher
    {
        /** @brief UV 의 타일 칸 간격입니다. 면 안 블록 좌표(최대 `kVoxelChunkHeight`)보다 커야 하고 voxel_blocks.hlsl 의 `kVoxelTileUVStride` 와 같습니다. */
        static constexpr float32 kTileUVStride = 128.0f;

        static void fillChunkMesh( const VoxelWorld& world, int32 chunkX, int32 chunkZ, VoxelChunkMesh& outMesh );
        /** @brief 면 하나의 밝기입니다. 위쪽은 1, 옆쪽은 0.85와 0.75, 아래쪽은 0.6입니다. 해가 없어도 블록 모서리가 보이게 합니다. */
        static float32 getFaceShade( VoxelFace face );
        /** @brief 그늘 단계의 밝기입니다. 0이 가장 어둡고 3이 트인 상태입니다. */
        static float32 getOcclusionShade( int32 occlusionLevel );
    };
} // namespace sw
