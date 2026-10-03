/**
 * @file VoxelRaycast.h
 * @brief 블록 격자를 따라 걷는 광선(Amanatides–Woo DDA) — 바라보는 블록 · 맞은 면 · 놓을 자리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Voxel/VoxelBlock.h"

namespace sw
{
    class VoxelWorld;

    /** @brief 광선이 맞은 블록입니다. */
    struct VoxelRayHit
    {
        VoxelCoord      _block{};    ///< 맞은 블록
        VoxelCoord      _previous{}; ///< 맞기 바로 전 칸 — 블록을 놓을 자리
        VoxelCoord      _normal{};   ///< 맞은 면의 바깥 방향(시작 칸 안에서 맞으면 0)
        float32         _distance{ 0.0f };
        VoxelBlockIndex _blockIndex{ kVoxelAirBlock };
    };

    /**
     * @struct VoxelRaycast
     * @brief 광선이 지나는 칸을 차례로 걸어 첫 블록(공기 · 물처럼 단단하지 않은 것은 지난다)에서 멈춥니다. 칸을 건너뛰지 않으므로 모서리를 스치는
     *        광선도 놓치지 않습니다.
     */
    struct SW_GF_API VoxelRaycast
    {
        /**
         * @param direction 길이가 0 이 아니면 된다(안에서 정규화한다).
         * @param bHitNonSolid true 면 단단하지 않은 블록(물)에서도 멈춘다.
         */
        [[nodiscard]] static bool raycast( const VoxelWorld& world, const float3& origin, const float3& direction, float32 maxDistance, VoxelRayHit& outHit,
                                           bool bHitNonSolid = false );
    };
} // namespace sw
